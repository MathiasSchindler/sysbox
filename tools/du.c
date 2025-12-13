#include "../src/sb.h"

static int du_print_line(const char *argv0, sb_u64 bytes, const char *path) {
	if (sb_write_u64_dec(1, bytes) < 0) return -1;
	if (sb_write_all(1, "\t", 1) < 0) return -1;
	if (sb_write_str(1, path) < 0) return -1;
	if (sb_write_all(1, "\n", 1) < 0) return -1;
	(void)argv0;
	return 0;
}

struct du_dir_iter_ctx {
	const char *argv0;
	sb_i32 dfd;
	char *subpool;
	sb_u32 *subpool_len;
	sb_u32 *sub_offs;
	sb_u32 *sub_n;
	sb_u32 sub_offs_cap;
	sb_u32 subpool_cap;
	sb_u64 *total;
	int *io_fail;
};

static int du_dir_iter_cb(void *ctxp, const char *name, sb_u8 d_type) {
	(void)d_type;
	struct du_dir_iter_ctx *ctx = (struct du_dir_iter_ctx *)ctxp;

	struct sb_stat stc;
	sb_i64 fr = sb_sys_newfstatat(ctx->dfd, name, &stc, SB_AT_SYMLINK_NOFOLLOW);
	if (fr < 0) {
		sb_print_errno(ctx->argv0, name, fr);
		*ctx->io_fail = 1;
		return 0;
	}

	sb_u32 ct = stc.st_mode & SB_S_IFMT;
	if (ct == SB_S_IFDIR) {
		// Record for later recursion.
		sb_usize nlen = sb_strlen(name);
		if (*ctx->sub_n < ctx->sub_offs_cap && *ctx->subpool_len + (sb_u32)nlen + 1u <= ctx->subpool_cap) {
			ctx->sub_offs[*ctx->sub_n] = *ctx->subpool_len;
			for (sb_usize k = 0; k < nlen; k++) ctx->subpool[*ctx->subpool_len + (sb_u32)k] = name[k];
			ctx->subpool[*ctx->subpool_len + (sb_u32)nlen] = 0;
			*ctx->subpool_len += (sb_u32)nlen + 1u;
			(*ctx->sub_n)++;
		} else {
			// Too many subdirs; treat as error.
			sb_print_errno(ctx->argv0, name, (sb_i64)-SB_EINVAL);
			*ctx->io_fail = 1;
		}
		return 0;
	}

	// Non-directory: add its size.
	*ctx->total += (sb_u64)stc.st_size;
	return 0;
}

static sb_u64 du_sum_path(const char *argv0, const char *path, int summary_only, int depth, int *io_fail) {
	if (depth > 64) {
		sb_print_errno(argv0, path, (sb_i64)-SB_ELOOP);
		*io_fail = 1;
		return 0;
	}

	// Check type without following symlinks.
	struct sb_stat st;
	sb_i64 sr = sb_sys_newfstatat(SB_AT_FDCWD, path, &st, SB_AT_SYMLINK_NOFOLLOW);
	if (sr < 0) {
		sb_print_errno(argv0, path, sr);
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
		sb_print_errno(argv0, path, dfd);
		*io_fail = 1;
		return 0;
	}

	// Collect subdirectory names to recurse into.
	char subpool[32768];
	sb_u32 subpool_len = 0;
	sb_u32 sub_offs[256];
	sb_u32 sub_n = 0;

	sb_u64 total = 0;
	struct du_dir_iter_ctx ictx = {
		.argv0 = argv0,
		.dfd = (sb_i32)dfd,
		.subpool = subpool,
		.subpool_len = &subpool_len,
		.sub_offs = sub_offs,
		.sub_n = &sub_n,
		.sub_offs_cap = (sb_u32)(sizeof(sub_offs) / sizeof(sub_offs[0])),
		.subpool_cap = (sb_u32)sizeof(subpool),
		.total = &total,
		.io_fail = io_fail,
	};
	sb_i64 ir = sb_for_each_dirent((sb_i32)dfd, du_dir_iter_cb, &ictx);
	if (ir < 0) {
		(void)sb_sys_close((sb_i32)dfd);
		sb_die_errno(argv0, "getdents64", ir);
	}

	(void)sb_sys_close((sb_i32)dfd);

	for (sb_u32 si = 0; si < sub_n; si++) {
		const char *subname = subpool + sub_offs[si];
		char child[4096];
		sb_join_path_or_die(argv0, path, subname, child, (sb_usize)sizeof(child));
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
