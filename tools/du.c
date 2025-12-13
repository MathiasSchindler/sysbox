#include "../src/sb.h"

static void du_print_err(const char *argv0, const char *path, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, path);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static void du_join_path_or_die(const char *argv0, const char *base, const char *name, char out[4096]) {
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

static int du_print_line(const char *argv0, sb_u64 bytes, const char *path) {
	if (sb_write_u64_dec(1, bytes) < 0) return -1;
	if (sb_write_all(1, "\t", 1) < 0) return -1;
	if (sb_write_str(1, path) < 0) return -1;
	if (sb_write_all(1, "\n", 1) < 0) return -1;
	(void)argv0;
	return 0;
}

static sb_u64 du_sum_path(const char *argv0, const char *path, int summary_only, int depth, int *io_fail) {
	if (depth > 64) {
		du_print_err(argv0, path, (sb_i64)-SB_ELOOP);
		*io_fail = 1;
		return 0;
	}

	// Check type without following symlinks.
	struct sb_stat st;
	sb_i64 sr = sb_sys_newfstatat(SB_AT_FDCWD, path, &st, SB_AT_SYMLINK_NOFOLLOW);
	if (sr < 0) {
		du_print_err(argv0, path, sr);
		*io_fail = 1;
		return 0;
	}

	sb_u32 t = st.st_mode & SB_S_IFMT;
	if (t != SB_S_IFDIR) {
		// Count non-directories by st_size (stable across filesystems).
		sb_u64 total = (sb_u64)st.st_size;
		if (du_print_line(argv0, total, path) != 0) {
			sb_die_errno(argv0, "write", -1);
		}
		return total;
	}

	// Directory: traverse children.
	sb_i64 dfd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY | SB_O_NOFOLLOW, 0);
	if (dfd < 0) {
		// If it's a symlink (ELOOP), count the symlink itself (already stat'ed).
		if ((sb_u64)(-dfd) == (sb_u64)SB_ELOOP) {
			sb_u64 total = (sb_u64)st.st_size;
			if (du_print_line(argv0, total, path) != 0) sb_die_errno(argv0, "write", -1);
			return total;
		}
		du_print_err(argv0, path, dfd);
		*io_fail = 1;
		return 0;
	}

	// Collect subdirectory names to recurse into.
	char subpool[32768];
	sb_u32 subpool_len = 0;
	sb_u32 sub_offs[256];
	sb_u32 sub_n = 0;

	sb_u64 total = 0;
	sb_u8 buf[32768];
	for (;;) {
		sb_i64 nread = sb_sys_getdents64((sb_i32)dfd, buf, (sb_u32)sizeof(buf));
		if (nread < 0) {
			(void)sb_sys_close((sb_i32)dfd);
			sb_die_errno(argv0, "getdents64", nread);
		}
		if (nread == 0) break;

		sb_u32 bpos = 0;
		while (bpos < (sb_u32)nread) {
			struct sb_linux_dirent64 *d = (struct sb_linux_dirent64 *)(buf + bpos);
			const char *name = d->d_name;
			if (!sb_is_dot_or_dotdot(name)) {
				struct sb_stat stc;
				sb_i64 fr = sb_sys_newfstatat((sb_i32)dfd, name, &stc, SB_AT_SYMLINK_NOFOLLOW);
				if (fr < 0) {
					du_print_err(argv0, name, fr);
					*io_fail = 1;
				} else {
					sb_u32 ct = stc.st_mode & SB_S_IFMT;
					if (ct == SB_S_IFDIR) {
						// Record for later recursion.
						sb_usize nlen = sb_strlen(name);
						if (sub_n < (sb_u32)(sizeof(sub_offs) / sizeof(sub_offs[0])) && subpool_len + (sb_u32)nlen + 1u <= (sb_u32)sizeof(subpool)) {
							sub_offs[sub_n] = subpool_len;
							for (sb_usize k = 0; k < nlen; k++) subpool[subpool_len + (sb_u32)k] = name[k];
							subpool[subpool_len + (sb_u32)nlen] = 0;
							subpool_len += (sb_u32)nlen + 1u;
							sub_n++;
						} else {
							// Too many subdirs; treat as error.
							du_print_err(argv0, name, (sb_i64)-SB_EINVAL);
							*io_fail = 1;
						}
					} else {
						// Non-directory: add its size.
						total += (sb_u64)stc.st_size;
					}
				}
			}
			bpos += d->d_reclen;
		}
	}

	(void)sb_sys_close((sb_i32)dfd);

	for (sb_u32 si = 0; si < sub_n; si++) {
		const char *subname = subpool + sub_offs[si];
		char child[4096];
		du_join_path_or_die(argv0, path, subname, child);
		total += du_sum_path(argv0, child, summary_only, depth + 1, io_fail);
	}

	if (!summary_only) {
		if (du_print_line(argv0, total, path) != 0) {
			sb_die_errno(argv0, "write", -1);
		}
	}

	return total;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "du";

	int summary_only = 0;
	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a || a[0] != '-' || sb_streq(a, "-")) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (sb_streq(a, "-s")) {
			summary_only = 1;
			continue;
		}
		sb_die_usage(argv0, "du [-s] [PATH...]");
	}

	int any_fail = 0;
	if (i >= argc) {
		int io_fail = 0;
		sb_u64 total = du_sum_path(argv0, ".", summary_only, 0, &io_fail);
		if (summary_only) {
			if (du_print_line(argv0, total, ".") != 0) sb_die_errno(argv0, "write", -1);
		}
		return io_fail ? 1 : 0;
	}

	for (; i < argc; i++) {
		const char *path = argv[i] ? argv[i] : "";
		int io_fail = 0;
		sb_u64 total = du_sum_path(argv0, path, summary_only, 0, &io_fail);
		if (summary_only) {
			if (du_print_line(argv0, total, path) != 0) sb_die_errno(argv0, "write", -1);
		}
		if (io_fail) any_fail = 1;
	}

	return any_fail ? 1 : 0;
}
