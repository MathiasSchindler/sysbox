#include "../src/sb.h"

static void mv_unlink_dest_if_exists(const char *argv0, const char *dst) {
	sb_i64 r = sb_sys_unlinkat(SB_AT_FDCWD, dst, 0);
	if (r < 0 && (sb_u64)(-r) != (sb_u64)SB_ENOENT) {
		// If DST is a directory, unlinkat returns -EISDIR, matching rename's failure.
		sb_die_errno(argv0, "unlink dst", r);
	}
}

static void mv_copy_regular_file(const char *argv0, const char *src, const char *dst, sb_u32 mode0777) {
	sb_i64 sfd = sb_sys_openat(SB_AT_FDCWD, src, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (sfd < 0) sb_die_errno(argv0, src, sfd);

	sb_i64 dfd = sb_sys_openat(SB_AT_FDCWD, dst, SB_O_WRONLY | SB_O_CREAT | SB_O_TRUNC | SB_O_CLOEXEC, mode0777);
	if (dfd < 0) {
		(void)sb_sys_close((sb_i32)sfd);
		sb_die_errno(argv0, dst, dfd);
	}

	sb_u8 buf[32768];
	for (;;) {
		sb_i64 r = sb_sys_read((sb_i32)sfd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			(void)sb_sys_close((sb_i32)dfd);
			(void)sb_sys_close((sb_i32)sfd);
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) break;
		sb_i64 w = sb_write_all((sb_i32)dfd, buf, (sb_usize)r);
		if (w < 0) {
			(void)sb_sys_close((sb_i32)dfd);
			(void)sb_sys_close((sb_i32)sfd);
			sb_die_errno(argv0, "write", w);
		}
	}

	(void)sb_sys_close((sb_i32)dfd);
	(void)sb_sys_close((sb_i32)sfd);
}

static void mv_copy_regular_file_at(const char *argv0, sb_i32 src_dirfd, const char *src_name, sb_i32 dst_dirfd, const char *dst_name, sb_u32 mode0777) {
	sb_i64 sfd = sb_sys_openat(src_dirfd, src_name, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (sfd < 0) sb_die_errno(argv0, "open src", sfd);

	sb_i64 dfd = sb_sys_openat(dst_dirfd, dst_name, SB_O_WRONLY | SB_O_CREAT | SB_O_TRUNC | SB_O_CLOEXEC, mode0777);
	if (dfd < 0) {
		(void)sb_sys_close((sb_i32)sfd);
		sb_die_errno(argv0, "open dst", dfd);
	}

	sb_u8 buf[32768];
	for (;;) {
		sb_i64 r = sb_sys_read((sb_i32)sfd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			(void)sb_sys_close((sb_i32)dfd);
			(void)sb_sys_close((sb_i32)sfd);
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) break;
		sb_i64 w = sb_write_all((sb_i32)dfd, buf, (sb_usize)r);
		if (w < 0) {
			(void)sb_sys_close((sb_i32)dfd);
			(void)sb_sys_close((sb_i32)sfd);
			sb_die_errno(argv0, "write", w);
		}
	}

	(void)sb_sys_close((sb_i32)dfd);
	(void)sb_sys_close((sb_i32)sfd);
}

static void mv_copy_symlink(const char *argv0, const char *src, const char *dst) {
	// Replace destination if it exists (rename semantics).
	mv_unlink_dest_if_exists(argv0, dst);

	char target[4096];
	sb_i64 n = sb_sys_readlinkat(SB_AT_FDCWD, src, target, sizeof(target) - 1);
	if (n < 0) sb_die_errno(argv0, "readlink", n);
	target[(sb_usize)n] = 0;

	sb_i64 r = sb_sys_symlinkat(target, SB_AT_FDCWD, dst);
	if (r < 0) sb_die_errno(argv0, "symlink", r);
}

static void mv_copy_symlink_at(const char *argv0, sb_i32 src_dirfd, const char *src_name, sb_i32 dst_dirfd, const char *dst_name) {
	// Replace destination if it exists (rename semantics).
	sb_i64 r = sb_sys_unlinkat(dst_dirfd, dst_name, 0);
	if (r < 0 && (sb_u64)(-r) != (sb_u64)SB_ENOENT) {
		sb_die_errno(argv0, "unlink dst", r);
	}

	char target[4096];
	sb_i64 n = sb_sys_readlinkat(src_dirfd, src_name, target, sizeof(target) - 1);
	if (n < 0) sb_die_errno(argv0, "readlink", n);
	target[(sb_usize)n] = 0;

	r = sb_sys_symlinkat(target, dst_dirfd, dst_name);
	if (r < 0) sb_die_errno(argv0, "symlink", r);
}

static void mv_copy_unlink_at_depth(const char *argv0, sb_i32 src_dirfd, const char *src_name, sb_i32 dst_dirfd, const char *dst_name, int depth);

static void mv_copy_dir_at_depth(const char *argv0, sb_i32 src_parentfd, const char *src_name, sb_i32 dst_parentfd, const char *dst_name, sb_u32 mode0777, int depth) {
	if (depth > 64) {
		sb_die_usage(argv0, "mv SRC DST");
	}

	// Create destination directory (must not exist).
	sb_i64 mr = sb_sys_mkdirat(dst_parentfd, dst_name, mode0777);
	if (mr < 0) {
		sb_die_errno(argv0, "mkdir dst", mr);
	}

	sb_i64 sfd = sb_sys_openat(src_parentfd, src_name, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY, 0);
	if (sfd < 0) sb_die_errno(argv0, "open src dir", sfd);

	sb_i64 dfd = sb_sys_openat(dst_parentfd, dst_name, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY, 0);
	if (dfd < 0) {
		(void)sb_sys_close((sb_i32)sfd);
		sb_die_errno(argv0, "open dst dir", dfd);
	}

	sb_u8 buf[32768];
	for (;;) {
		sb_i64 nread = sb_sys_getdents64((sb_i32)sfd, buf, (sb_u32)sizeof(buf));
		if (nread < 0) {
			(void)sb_sys_close((sb_i32)dfd);
			(void)sb_sys_close((sb_i32)sfd);
			sb_die_errno(argv0, "getdents64", nread);
		}
		if (nread == 0) break;
		sb_u32 bpos = 0;
		while (bpos < (sb_u32)nread) {
			struct sb_linux_dirent64 *d = (struct sb_linux_dirent64 *)(buf + bpos);
			const char *name = d->d_name;
			if (!sb_is_dot_or_dotdot(name)) {
				mv_copy_unlink_at_depth(argv0, (sb_i32)sfd, name, (sb_i32)dfd, name, depth + 1);
			}
			bpos += d->d_reclen;
		}
	}

	(void)sb_sys_close((sb_i32)dfd);
	(void)sb_sys_close((sb_i32)sfd);

	// Remove now-empty source directory.
	sb_i64 ur = sb_sys_unlinkat(src_parentfd, src_name, SB_AT_REMOVEDIR);
	if (ur < 0) sb_die_errno(argv0, "rmdir src", ur);
}

static void mv_copy_unlink_at_depth(const char *argv0, sb_i32 src_dirfd, const char *src_name, sb_i32 dst_dirfd, const char *dst_name, int depth) {
	struct sb_stat st;
	sb_i64 r = sb_sys_newfstatat(src_dirfd, src_name, &st, SB_AT_SYMLINK_NOFOLLOW);
	if (r < 0) sb_die_errno(argv0, "stat src", r);

	sb_u32 t = st.st_mode & SB_S_IFMT;
	if (t == SB_S_IFREG) {
		mv_copy_regular_file_at(argv0, src_dirfd, src_name, dst_dirfd, dst_name, (sb_u32)(st.st_mode & 0777u));
		r = sb_sys_unlinkat(src_dirfd, src_name, 0);
		if (r < 0) sb_die_errno(argv0, "unlink src", r);
		return;
	}
	if (t == SB_S_IFLNK) {
		mv_copy_symlink_at(argv0, src_dirfd, src_name, dst_dirfd, dst_name);
		r = sb_sys_unlinkat(src_dirfd, src_name, 0);
		if (r < 0) sb_die_errno(argv0, "unlink src", r);
		return;
	}
	if (t == SB_S_IFDIR) {
		mv_copy_dir_at_depth(argv0, src_dirfd, src_name, dst_dirfd, dst_name, (sb_u32)(st.st_mode & 0777u), depth);
		return;
	}

	sb_die_usage(argv0, "mv SRC DST");
}

static void mv_copy_unlink(const char *argv0, const char *src, const char *dst) {
	struct sb_stat st;
	sb_i64 r = sb_sys_newfstatat(SB_AT_FDCWD, src, &st, SB_AT_SYMLINK_NOFOLLOW);
	if (r < 0) sb_die_errno(argv0, src, r);

	sb_u32 t = st.st_mode & SB_S_IFMT;
	if (t == SB_S_IFREG) {
		mv_copy_regular_file(argv0, src, dst, (sb_u32)(st.st_mode & 0777u));
	} else if (t == SB_S_IFLNK) {
		mv_copy_symlink(argv0, src, dst);
	} else if (t == SB_S_IFDIR) {
		// Minimal semantics for cross-FS directory moves:
		// - only supports DST not existing
		struct sb_stat dstst;
		sb_i64 dr = sb_sys_newfstatat(SB_AT_FDCWD, dst, &dstst, SB_AT_SYMLINK_NOFOLLOW);
		if (dr >= 0) {
			sb_die_errno(argv0, "dst exists", (sb_i64)-SB_EEXIST);
		}
		if ((sb_u64)(-dr) != (sb_u64)SB_ENOENT) {
			sb_die_errno(argv0, "stat dst", dr);
		}
		mv_copy_dir_at_depth(argv0, SB_AT_FDCWD, src, SB_AT_FDCWD, dst, (sb_u32)(st.st_mode & 0777u), 0);
		return;
	} else {
		// Keep minimal: no cross-FS directory moves or special files.
		sb_die_usage(argv0, "mv SRC DST");
	}

	r = sb_sys_unlinkat(SB_AT_FDCWD, src, 0);
	if (r < 0) sb_die_errno(argv0, "unlink src", r);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;

	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "mv";

	int i = 1;
	if (i < argc && argv[i] && sb_streq(argv[i], "--")) {
		i++;
	}

	if (argc - i != 2) {
		sb_die_usage(argv0, "mv SRC DST");
	}

	const char *src = argv[i] ? argv[i] : "";
	const char *dst = argv[i + 1] ? argv[i + 1] : "";

	sb_i64 r = sb_sys_renameat(SB_AT_FDCWD, src, SB_AT_FDCWD, dst);
	if (r < 0) {
		if ((sb_u64)(-r) == (sb_u64)SB_EXDEV) {
			mv_copy_unlink(argv0, src, dst);
			return 0;
		}
		sb_die_errno(argv0, "rename", r);
	}

	return 0;
}
