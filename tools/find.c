#include "../src/sb.h"

// Minimal find subset (syscall-only):
// - Usage: find [PATH...] [EXPR]
// - PATH: defaults to '.' if none.
// - EXPR (AND-only; left-to-right):
//     -name PAT    (PAT supports '*' and '?' only; match against basename)
//     -type [f|d|l]
//     -mindepth N
//     -maxdepth N
//     -print       (default action)
// - Traversal is pre-order; does not follow symlinks when recursing.


struct find_expr {
	const char *name_pat;
	int has_type;
	sb_u32 type_mode; // SB_S_IFREG/SB_S_IFDIR/SB_S_IFLNK
	int has_mindepth;
	sb_i32 mindepth;
	int has_maxdepth;
	sb_i32 maxdepth;
};

static void find_print_err(const char *argv0, const char *path, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, path);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static void find_join_path_or_die(const char *argv0, const char *base, const char *name, char out[4096]) {
	sb_usize blen = sb_strlen(base);
	sb_usize nlen = sb_strlen(name);
	int need_slash = 1;

	if (blen == 0 || (blen == 1 && base[0] == '.')) {
		if (nlen + 1 > 4096) sb_die_errno(argv0, "path", (sb_i64)-SB_EINVAL);
		for (sb_usize i = 0; i < nlen; i++) out[i] = name[i];
		out[nlen] = 0;
		return;
	}
	if (blen > 0 && base[blen - 1] == '/') need_slash = 0;

	sb_usize total = blen + (need_slash ? 1u : 0u) + nlen;
	if (total + 1 > 4096) sb_die_errno(argv0, "path", (sb_i64)-SB_EINVAL);
	for (sb_usize i = 0; i < blen; i++) out[i] = base[i];
	sb_usize off = blen;
	if (need_slash) out[off++] = '/';
	for (sb_usize i = 0; i < nlen; i++) out[off + i] = name[i];
	out[total] = 0;
}

static const char *find_basename(const char *path) {
	if (!path) return "";
	const char *last = path;
	for (const char *p = path; *p; p++) {
		if (*p == '/') last = p + 1;
	}
	// If path ends with '/', last points to NUL, return empty.
	return last;
}

static int find_match_name(const char *pat, const char *s) {
	// Glob match with '*' and '?', no character classes.
	// Escape support: backslash escapes next char.
	const char *p = pat;
	const char *t = s;
	const char *star_p = 0;
	const char *star_t = 0;

	while (*t) {
		char pc = *p;
		if (pc == '\\' && p[1]) {
			pc = p[1];
			if (pc == *t) {
				p += 2;
				t++;
				continue;
			}
		} else if (pc == '?') {
			p++;
			t++;
			continue;
		} else if (pc == '*') {
			star_p = ++p;
			star_t = t;
			continue;
		} else if (pc && pc == *t) {
			p++;
			t++;
			continue;
		}

		if (star_p) {
			p = star_p;
			t = ++star_t;
			continue;
		}
		return 0;
	}

	// Consume remaining stars.
	while (*p == '*') p++;
	// Allow trailing escape to be treated literally (but it can't match empty).
	return *p == 0;
}

static int find_parse_i32(const char *s, sb_i32 *out) {
	sb_i32 v = 0;
	int neg = 0;
	if (!s || !*s) return -1;
	if (*s == '+' || *s == '-') {
		neg = (*s == '-');
		s++;
	}
	if (*s < '0' || *s > '9') return -1;
	while (*s >= '0' && *s <= '9') {
		v = (sb_i32)(v * 10 + (sb_i32)(*s - '0'));
		s++;
	}
	*out = neg ? -v : v;
	return *s ? -1 : 0;
}

static int find_should_print(const struct find_expr *e, const char *path, const struct sb_stat *st, sb_i32 depth) {
	if (e->has_mindepth && depth < e->mindepth) return 0;
	if (e->name_pat) {
		const char *bn = find_basename(path);
		if (!find_match_name(e->name_pat, bn)) return 0;
	}
	if (e->has_type) {
		sb_u32 t = st->st_mode & SB_S_IFMT;
		if (t != e->type_mode) return 0;
	}
	return 1;
}

static void find_print_path_or_die(const char *argv0, const char *path) {
	if (sb_write_str(1, path) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_all(1, "\n", 1) < 0) sb_die_errno(argv0, "write", -1);
}

static void find_walk(const char *argv0, const struct find_expr *e, const char *path, sb_i32 depth, int *any_fail) {
	if (depth > 64) {
		find_print_err(argv0, path, (sb_i64)-SB_ELOOP);
		*any_fail = 1;
		return;
	}

	struct sb_stat st;
	sb_i64 sr = sb_sys_newfstatat(SB_AT_FDCWD, path, &st, SB_AT_SYMLINK_NOFOLLOW);
	if (sr < 0) {
		find_print_err(argv0, path, sr);
		*any_fail = 1;
		return;
	}

	if (find_should_print(e, path, &st, depth)) {
		find_print_path_or_die(argv0, path);
	}

	// Stop recursion if maxdepth reached.
	if (e->has_maxdepth && depth >= e->maxdepth) {
		return;
	}

	sb_u32 t = st.st_mode & SB_S_IFMT;
	if (t != SB_S_IFDIR) return;

	// Do not recurse into symlinked dirs (O_NOFOLLOW).
	sb_i64 dfd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY | SB_O_NOFOLLOW, 0);
	if (dfd < 0) {
		// If it's a symlink, just don't recurse.
		if ((sb_u64)(-dfd) == (sb_u64)SB_ELOOP) return;
		find_print_err(argv0, path, dfd);
		*any_fail = 1;
		return;
	}

	sb_u8 buf[32768];
	for (;;) {
		sb_i64 nread = sb_sys_getdents64((sb_i32)dfd, buf, (sb_u32)sizeof(buf));
		if (nread < 0) {
			(void)sb_sys_close((sb_i32)dfd);
			find_print_err(argv0, "getdents64", nread);
			*any_fail = 1;
			return;
		}
		if (nread == 0) break;

		sb_u32 bpos = 0;
		while (bpos < (sb_u32)nread) {
			struct sb_linux_dirent64 *d = (struct sb_linux_dirent64 *)(buf + bpos);
			const char *name = d->d_name;
			if (!sb_is_dot_or_dotdot(name)) {
				char child[4096];
				find_join_path_or_die(argv0, path, name, child);
				find_walk(argv0, e, child, depth + 1, any_fail);
			}
			bpos += d->d_reclen;
		}
	}

	(void)sb_sys_close((sb_i32)dfd);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "find";

	struct find_expr expr = {0};
	int i = 1;

	// Collect paths: any leading args not starting with '-' and not equal to '--'.
	int path_start = i;
	int n_paths = 0;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] == '-') break;
		n_paths++;
	}

	// Parse expression tokens.
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) continue;
		if (sb_streq(a, "-print")) {
			continue;
		}
		if (sb_streq(a, "-name")) {
			i++;
			if (i >= argc || !argv[i]) sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print]");
			expr.name_pat = argv[i];
			continue;
		}
		if (sb_streq(a, "-type")) {
			i++;
			if (i >= argc || !argv[i]) sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print]");
			const char *t = argv[i];
			if (!t[0] || t[1]) sb_die_usage(argv0, "find: -type expects one of f,d,l");
			expr.has_type = 1;
			if (t[0] == 'f') expr.type_mode = SB_S_IFREG;
			else if (t[0] == 'd') expr.type_mode = SB_S_IFDIR;
			else if (t[0] == 'l') expr.type_mode = SB_S_IFLNK;
			else sb_die_usage(argv0, "find: -type expects one of f,d,l");
			continue;
		}
		if (sb_streq(a, "-mindepth")) {
			i++;
			if (i >= argc || !argv[i]) sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print]");
			sb_i32 v = 0;
			if (find_parse_i32(argv[i], &v) != 0 || v < 0) sb_die_usage(argv0, "find: -mindepth expects non-negative integer");
			expr.has_mindepth = 1;
			expr.mindepth = v;
			continue;
		}
		if (sb_streq(a, "-maxdepth")) {
			i++;
			if (i >= argc || !argv[i]) sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print]");
			sb_i32 v = 0;
			if (find_parse_i32(argv[i], &v) != 0 || v < 0) sb_die_usage(argv0, "find: -maxdepth expects non-negative integer");
			expr.has_maxdepth = 1;
			expr.maxdepth = v;
			continue;
		}
		// Unknown token.
		sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print]");
	}

	int any_fail = 0;
	if (n_paths == 0) {
		find_walk(argv0, &expr, ".", 0, &any_fail);
		return any_fail ? 1 : 0;
	}

	for (int pi = 0; pi < n_paths; pi++) {
		const char *p = argv[path_start + pi];
		if (!p) continue;
		find_walk(argv0, &expr, p, 0, &any_fail);
	}

	return any_fail ? 1 : 0;
}
