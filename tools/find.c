#include "../src/sb.h"

// Minimal find subset (syscall-only):
// - Usage: find [PATH...] [EXPR]
// - PATH: defaults to '.' if none.
// - EXPR (AND-only; left-to-right):
//     -name PAT    (PAT supports '*' and '?' only; match against basename)
//     -type [f|d|l]
//     -mindepth N
//     -maxdepth N
//     -print       (default action if no action is specified)
//     -exec CMD... {} ... \;   (run once per match)
// - Traversal is pre-order; does not follow symlinks when recursing.


struct find_expr {
	const char *name_pat;
	int has_type;
	sb_u32 type_mode; // SB_S_IFREG/SB_S_IFDIR/SB_S_IFLNK
	int has_mindepth;
	sb_i32 mindepth;
	int has_maxdepth;
	sb_i32 maxdepth;

	int has_action;
	int do_print;

	int has_exec;
	int exec_argc;
	const char **exec_argv; // points into argv[]; length exec_argc
};

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

static int find_match(const struct find_expr *e, const char *path, const struct sb_stat *st, sb_i32 depth) {
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

static sb_i64 find_execvp(const char *file, char **argv, char **envp) {
	if (!file || !*file) {
		return (sb_i64)-SB_ENOENT;
	}
	if (sb_has_slash(file)) {
		return sb_sys_execve(file, argv, envp);
	}

	const char *path_env = sb_getenv_kv(envp, "PATH=");
	if (!path_env || !*path_env) {
		path_env = "/bin:/usr/bin";
	}

	char full[4096];
	sb_usize fn = sb_strlen(file);

	const char *p = path_env;
	for (;;) {
		const char *seg = p;
		while (*p && *p != ':') {
			p++;
		}
		sb_usize seglen = (sb_usize)(p - seg);

		if (seglen + 1 + fn + 1 <= sizeof(full)) {
			sb_usize k = 0;
			for (; k < seglen; k++) full[k] = seg[k];
			if (k == 0) {
				full[k++] = '.';
			}
			if (full[k - 1] != '/') {
				full[k++] = '/';
			}
			for (sb_usize j = 0; j < fn; j++) full[k + j] = file[j];
			k += fn;
			full[k] = 0;

			sb_i64 r = sb_sys_execve(full, argv, envp);
			if (r < 0) {
				sb_u64 e = (sb_u64)(-r);
				if (e != (sb_u64)SB_ENOENT && e != (sb_u64)SB_ENOTDIR) {
					return r;
				}
			}
		}

		if (*p == ':') {
			p++;
			continue;
		}
		break;
	}

	return (sb_i64)-SB_ENOENT;
}

static int find_exit_code_from_wait_status(sb_i32 status) {
	sb_u32 u = (sb_u32)status;
	sb_u32 sig = u & 0x7Fu;
	if (sig != 0) {
		return 128 + (int)sig;
	}
	return (int)((u >> 8) & 0xFFu);
}

static const char *find_exec_subst_braces(const char *arg, const char *path, char *storage, sb_usize storage_cap, sb_usize *io_used) {
	if (!arg) arg = "";
	if (!path) path = "";
	if (!storage || storage_cap == 0 || !io_used) return 0;

	// Fast path: no "{}".
	int needs = 0;
	for (const char *p = arg; *p; p++) {
		if (p[0] == '{' && p[1] == '}') {
			needs = 1;
			break;
		}
	}
	if (!needs) return arg;

	sb_usize path_len = sb_strlen(path);
	sb_usize out_len = 0;
	for (const char *p = arg; *p; ) {
		if (p[0] == '{' && p[1] == '}') {
			out_len += path_len;
			p += 2;
		} else {
			out_len += 1;
			p += 1;
		}
	}

	if (out_len + 1 > storage_cap - *io_used) return 0;
	char *dst = &storage[*io_used];
	*io_used += out_len + 1;

	sb_usize k = 0;
	for (const char *p = arg; *p; ) {
		if (p[0] == '{' && p[1] == '}') {
			for (sb_usize j = 0; j < path_len; j++) dst[k + j] = path[j];
			k += path_len;
			p += 2;
		} else {
			dst[k++] = *p++;
		}
	}
	dst[k] = 0;
	return dst;
}

static void find_run_exec(const char *argv0, const struct find_expr *e, const char *path, char **envp, int *any_fail) {
	if (!e->has_exec || e->exec_argc <= 0 || !e->exec_argv) {
		return;
	}

	char repl_storage[16384];
	sb_usize repl_used = 0;

	// Keep this intentionally small: fixed argv size, no heap.
	char *argv_exec[64];
	int outc = 0;
	for (int i = 0; i < e->exec_argc && outc + 1 < (int)(sizeof(argv_exec) / sizeof(argv_exec[0])); i++) {
		const char *a = e->exec_argv[i];
		const char *r = find_exec_subst_braces(a, path, repl_storage, (sb_usize)sizeof(repl_storage), &repl_used);
		if (!r) {
			(void)sb_write_str(2, argv0);
			(void)sb_write_str(2, ": -exec: substitution too large\n");
			*any_fail = 1;
			return;
		}
		argv_exec[outc++] = (char *)r;
	}
	argv_exec[outc] = 0;
	if (outc == 0) {
		*any_fail = 1;
		return;
	}

	sb_i64 pid = sb_sys_vfork();
	if (pid < 0) {
		sb_print_errno(argv0, "vfork", pid);
		*any_fail = 1;
		return;
	}
	if (pid == 0) {
		sb_i64 r = find_execvp(argv_exec[0], argv_exec, envp);
		(void)sb_write_str(2, argv0);
		(void)sb_write_str(2, ": -exec: ");
		(void)sb_write_str(2, argv_exec[0]);
		(void)sb_write_str(2, ": errno=");
		sb_write_hex_u64(2, (sb_u64)(-r));
		(void)sb_write_str(2, "\n");
		sb_exit(127);
	}

	sb_i32 status = 0;
	sb_i64 w = sb_sys_wait4((sb_i32)pid, &status, 0, 0);
	if (w < 0) {
		sb_print_errno(argv0, "wait4", w);
		*any_fail = 1;
		return;
	}
	int rc = find_exit_code_from_wait_status(status);
	if (rc != 0) {
		*any_fail = 1;
	}
}

static void find_walk(const char *argv0, const struct find_expr *e, const char *path, sb_i32 depth, char **envp, int *any_fail);

struct find_dir_iter_ctx {
	const char *argv0;
	const struct find_expr *expr;
	const char *path;
	sb_i32 depth;
	char **envp;
	int *any_fail;
};

static int find_dir_iter_cb(void *ctxp, const char *name, sb_u8 d_type) {
	(void)d_type;
	struct find_dir_iter_ctx *ctx = (struct find_dir_iter_ctx *)ctxp;
	char child[4096];
	sb_join_path_or_die(ctx->argv0, ctx->path, name, child, (sb_usize)sizeof(child));
	find_walk(ctx->argv0, ctx->expr, child, ctx->depth + 1, ctx->envp, ctx->any_fail);
	return 0;
}

static void find_walk(const char *argv0, const struct find_expr *e, const char *path, sb_i32 depth, char **envp, int *any_fail) {
	if (depth > 64) {
		sb_print_errno(argv0, path, (sb_i64)-SB_ELOOP);
		*any_fail = 1;
		return;
	}

	struct sb_stat st;
	sb_i64 sr = sb_sys_newfstatat(SB_AT_FDCWD, path, &st, SB_AT_SYMLINK_NOFOLLOW);
	if (sr < 0) {
		sb_print_errno(argv0, path, sr);
		*any_fail = 1;
		return;
	}

	if (find_match(e, path, &st, depth)) {
		if (e->do_print) {
			find_print_path_or_die(argv0, path);
		}
		if (e->has_exec) {
			find_run_exec(argv0, e, path, envp, any_fail);
		}
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
		sb_print_errno(argv0, path, dfd);
		*any_fail = 1;
		return;
	}

	struct find_dir_iter_ctx ictx = {
		.argv0 = argv0,
		.expr = e,
		.path = path,
		.depth = depth,
		.envp = envp,
		.any_fail = any_fail,
	};
	sb_i64 ir = sb_for_each_dirent((sb_i32)dfd, find_dir_iter_cb, &ictx);
	if (ir < 0) {
		(void)sb_sys_close((sb_i32)dfd);
		sb_print_errno(argv0, "getdents64", ir);
		*any_fail = 1;
		return;
	}

	(void)sb_sys_close((sb_i32)dfd);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
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
			expr.has_action = 1;
			expr.do_print = 1;
			continue;
		}
		if (sb_streq(a, "-name")) {
			i++;
			if (i >= argc || !argv[i]) sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print] [-exec CMD... {} ... \\;]");
			expr.name_pat = argv[i];
			continue;
		}
		if (sb_streq(a, "-type")) {
			i++;
			if (i >= argc || !argv[i]) sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print] [-exec CMD... {} ... \\;]");
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
			if (i >= argc || !argv[i]) sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print] [-exec CMD... {} ... \\;]");
			sb_i32 v = 0;
			if (sb_parse_i32_dec(argv[i], &v) != 0 || v < 0) sb_die_usage(argv0, "find: -mindepth expects non-negative integer");
			expr.has_mindepth = 1;
			expr.mindepth = v;
			continue;
		}
		if (sb_streq(a, "-maxdepth")) {
			i++;
			if (i >= argc || !argv[i]) sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print] [-exec CMD... {} ... \\;]");
			sb_i32 v = 0;
			if (sb_parse_i32_dec(argv[i], &v) != 0 || v < 0) sb_die_usage(argv0, "find: -maxdepth expects non-negative integer");
			expr.has_maxdepth = 1;
			expr.maxdepth = v;
			continue;
		}
		if (sb_streq(a, "-exec")) {
			int start = i + 1;
			int end = start;
			for (; end < argc; end++) {
				const char *t = argv[end];
				if (!t) continue;
				if (sb_streq(t, ";") || sb_streq(t, "\\;")) {
					break;
				}
			}
			if (start >= argc || end >= argc) {
				sb_die_usage(argv0, "find: -exec expects: -exec CMD... {} ... \\;");
			}
			if (end - start <= 0) {
				sb_die_usage(argv0, "find: -exec expects at least a command");
			}
			if (end - start >= 63) {
				sb_die_usage(argv0, "find: -exec supports up to 62 args");
			}
			expr.has_action = 1;
			expr.has_exec = 1;
			expr.exec_argv = (const char **)&argv[start];
			expr.exec_argc = end - start;
			i = end; // skip until terminator
			continue;
		}
		// Unknown token.
		sb_die_usage(argv0, "find [PATH...] [-name PAT] [-type f|d|l] [-mindepth N] [-maxdepth N] [-print] [-exec CMD... {} ... \\;]");
	}

	if (!expr.has_action) {
		expr.do_print = 1;
	}

	int any_fail = 0;
	if (n_paths == 0) {
		find_walk(argv0, &expr, ".", 0, envp, &any_fail);
		return any_fail ? 1 : 0;
	}

	for (int pi = 0; pi < n_paths; pi++) {
		const char *p = argv[path_start + pi];
		if (!p) continue;
		find_walk(argv0, &expr, p, 0, envp, &any_fail);
	}

	return any_fail ? 1 : 0;
}
