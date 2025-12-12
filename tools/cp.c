#include "../src/sb.h"

struct sb_linux_dirent64 {
	sb_u64 d_ino;
	sb_i64 d_off;
	sb_u16 d_reclen;
	sb_u8 d_type;
	char d_name[];
} __attribute__((packed));

static int cp_skip_dot(const char *name) {
	if (!name || name[0] != '.') {
		return 0;
	}
	if (name[1] == 0) {
		return 1;
	}
	if (name[1] == '.' && name[2] == 0) {
		return 1;
	}
	return 0;
}

static void cp_stream(const char *argv0, sb_i32 src_fd, sb_i32 dst_fd) {
	sb_u8 buf[32768];
	for (;;) {
		sb_i64 r = sb_sys_read(src_fd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) {
			break;
		}
		sb_i64 w = sb_write_all(dst_fd, buf, (sb_usize)r);
		if (w < 0) {
			sb_die_errno(argv0, "write", w);
		}
	}
}

static void cp_preserve_at(const char *argv0, sb_i32 dirfd, const char *name, const struct sb_stat *st) {
	// Mode
	sb_u32 mode = (sb_u32)(st->st_mode & 07777u);
	sb_i64 r = sb_sys_fchmodat(dirfd, name, mode, 0);
	if (r < 0) {
		sb_die_errno(argv0, "chmod", r);
	}

	// Owner/group (may fail for non-root; ignore EPERM).
	r = sb_sys_fchownat(dirfd, name, st->st_uid, st->st_gid, 0);
	if (r < 0 && (sb_u64)(-r) != (sb_u64)SB_EPERM) {
		sb_die_errno(argv0, "chown", r);
	}

	// Times (best-effort; ignore EPERM).
	struct sb_timespec ts[2];
	ts[0].tv_sec = (sb_i64)st->st_atime;
	ts[0].tv_nsec = (sb_i64)st->st_atime_nsec;
	ts[1].tv_sec = (sb_i64)st->st_mtime;
	ts[1].tv_nsec = (sb_i64)st->st_mtime_nsec;
	r = sb_sys_utimensat(dirfd, name, ts, 0);
	if (r < 0 && (sb_u64)(-r) != (sb_u64)SB_EPERM) {
		sb_die_errno(argv0, "utimensat", r);
	}
}

static void cp_copy_file_at(const char *argv0, sb_i32 src_dirfd, const char *src_name, sb_i32 dst_dirfd, const char *dst_name, int preserve) {
	sb_i64 src_fd = sb_sys_openat(src_dirfd, src_name, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (src_fd < 0) {
		sb_die_errno(argv0, src_name, src_fd);
	}

	struct sb_stat st;
	sb_i64 sr = sb_sys_fstat((sb_i32)src_fd, &st);
	if (sr < 0) {
		(void)sb_sys_close((sb_i32)src_fd);
		sb_die_errno(argv0, "fstat", sr);
	}
	const sb_u32 type = st.st_mode & SB_S_IFMT;
	if (type != SB_S_IFREG) {
		(void)sb_sys_close((sb_i32)src_fd);
		sb_die_errno(argv0, src_name, (type == SB_S_IFDIR) ? (sb_i64)-SB_EISDIR : (sb_i64)-SB_EINVAL);
	}

	sb_i64 dst_fd = sb_sys_openat(dst_dirfd, dst_name, SB_O_WRONLY | SB_O_CREAT | SB_O_TRUNC | SB_O_CLOEXEC, 0666);
	if (dst_fd < 0) {
		(void)sb_sys_close((sb_i32)src_fd);
		sb_die_errno(argv0, dst_name, dst_fd);
	}

	cp_stream(argv0, (sb_i32)src_fd, (sb_i32)dst_fd);
	(void)sb_sys_close((sb_i32)dst_fd);
	(void)sb_sys_close((sb_i32)src_fd);

	if (preserve) {
		cp_preserve_at(argv0, dst_dirfd, dst_name, &st);
	}
}

static void cp_mkdirat_if_needed(const char *argv0, sb_i32 dirfd, const char *name) {
	sb_i64 r = sb_sys_mkdirat(dirfd, name, 0777);
	if (r >= 0) {
		return;
	}
	if ((sb_u64)(-r) == (sb_u64)SB_EEXIST) {
		// Confirm it's a directory.
		sb_i64 fd = sb_sys_openat(dirfd, name, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY, 0);
		if (fd < 0) {
			sb_die_errno(argv0, name, fd);
		}
		(void)sb_sys_close((sb_i32)fd);
		return;
	}
	sb_die_errno(argv0, name, r);
}

static void cp_dir_contents(const char *argv0, sb_i32 src_dirfd, sb_i32 dst_dirfd, int preserve);

static void cp_copy_entry(const char *argv0, sb_i32 src_dirfd, const char *name, sb_i32 dst_dirfd, int preserve) {
	if (cp_skip_dot(name)) {
		return;
	}

	// Try to treat it as a directory without following symlinks.
	sb_i64 dfd = sb_sys_openat(src_dirfd, name, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY | SB_O_NOFOLLOW, 0);
	if (dfd >= 0) {
		struct sb_stat st_dir;
		sb_i64 sr = sb_sys_fstat((sb_i32)dfd, &st_dir);
		if (sr < 0) {
			(void)sb_sys_close((sb_i32)dfd);
			sb_die_errno(argv0, "fstat", sr);
		}

		cp_mkdirat_if_needed(argv0, dst_dirfd, name);
		sb_i64 out_dfd = sb_sys_openat(dst_dirfd, name, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY, 0);
		if (out_dfd < 0) {
			(void)sb_sys_close((sb_i32)dfd);
			sb_die_errno(argv0, name, out_dfd);
		}

		cp_dir_contents(argv0, (sb_i32)dfd, (sb_i32)out_dfd, preserve);
		(void)sb_sys_close((sb_i32)out_dfd);
		(void)sb_sys_close((sb_i32)dfd);

		if (preserve) {
			cp_preserve_at(argv0, dst_dirfd, name, &st_dir);
		}
		return;
	}

	// If it's a symlink, decide based on the target type.
	if ((sb_u64)(-dfd) == (sb_u64)SB_ELOOP) {
		sb_i64 sfd = sb_sys_openat(src_dirfd, name, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (sfd < 0) {
			sb_die_errno(argv0, name, sfd);
		}
		struct sb_stat st;
		sb_i64 sr = sb_sys_fstat((sb_i32)sfd, &st);
		(void)sb_sys_close((sb_i32)sfd);
		if (sr < 0) {
			sb_die_errno(argv0, "fstat", sr);
		}
		if ((st.st_mode & SB_S_IFMT) == SB_S_IFDIR) {
			// Avoid recursing into symlinked directories (loop risk).
			sb_die_errno(argv0, name, (sb_i64)-SB_ELOOP);
		}
		// Treat as copying the target file.
		cp_copy_file_at(argv0, src_dirfd, name, dst_dirfd, name, preserve);
		return;
	}

	// Not a directory: copy as a regular file.
	cp_copy_file_at(argv0, src_dirfd, name, dst_dirfd, name, preserve);
}

static void cp_dir_contents(const char *argv0, sb_i32 src_dirfd, sb_i32 dst_dirfd, int preserve) {
	sb_u8 buf[32768];
	for (;;) {
		sb_i64 nread = sb_sys_getdents64(src_dirfd, buf, (sb_u32)sizeof(buf));
		if (nread < 0) {
			sb_die_errno(argv0, "getdents64", nread);
		}
		if (nread == 0) {
			break;
		}

		sb_u32 bpos = 0;
		while (bpos < (sb_u32)nread) {
			struct sb_linux_dirent64 *d = (struct sb_linux_dirent64 *)(buf + bpos);
			cp_copy_entry(argv0, src_dirfd, d->d_name, dst_dirfd, preserve);
			bpos += d->d_reclen;
		}
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;

	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "cp";
	int recursive = 0;
	int preserve = 0;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a || a[0] != '-' || sb_streq(a, "-")) {
			break;
		}
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (sb_streq(a, "-r")) {
			recursive = 1;
			continue;
		}
		if (sb_streq(a, "-p")) {
			preserve = 1;
			continue;
		}
		sb_die_usage(argv0, "cp [-r] [-p] [--] SRC DST");
	}

	if (argc - i != 2) {
		sb_die_usage(argv0, "cp [-r] [-p] [--] SRC DST");
	}

	const char *src = argv[i] ? argv[i] : "";
	const char *dst = argv[i + 1] ? argv[i + 1] : "";

	// If not recursive, keep minimal behavior: regular files only.
	if (!recursive) {
		cp_copy_file_at(argv0, SB_AT_FDCWD, src, SB_AT_FDCWD, dst, preserve);
		return 0;
	}

	// Recursive: if SRC is a directory, copy its contents into DST.
	sb_i64 src_dfd = sb_sys_openat(SB_AT_FDCWD, src, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY | SB_O_NOFOLLOW, 0);
	if (src_dfd < 0) {
		// Not a directory; treat as a file copy.
		cp_copy_file_at(argv0, SB_AT_FDCWD, src, SB_AT_FDCWD, dst, preserve);
		return 0;
	}

	struct sb_stat st_dir;
	sb_i64 sr = sb_sys_fstat((sb_i32)src_dfd, &st_dir);
	if (sr < 0) {
		(void)sb_sys_close((sb_i32)src_dfd);
		sb_die_errno(argv0, "fstat", sr);
	}

	// Create destination directory (or ensure it exists as a directory).
	cp_mkdirat_if_needed(argv0, SB_AT_FDCWD, dst);
	sb_i64 dst_dfd = sb_sys_openat(SB_AT_FDCWD, dst, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY, 0);
	if (dst_dfd < 0) {
		(void)sb_sys_close((sb_i32)src_dfd);
		sb_die_errno(argv0, dst, dst_dfd);
	}

	cp_dir_contents(argv0, (sb_i32)src_dfd, (sb_i32)dst_dfd, preserve);
	(void)sb_sys_close((sb_i32)dst_dfd);
	(void)sb_sys_close((sb_i32)src_dfd);

	if (preserve) {
		cp_preserve_at(argv0, SB_AT_FDCWD, dst, &st_dir);
	}
	return 0;
}
