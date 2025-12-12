#include "../src/sb.h"

struct sb_linux_dirent64 {
	sb_u64 d_ino;
	sb_i64 d_off;
	sb_u16 d_reclen;
	sb_u8 d_type;
	char d_name[];
} __attribute__((packed));

static void rm_print_err(const char *argv0, const char *path, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, path);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static int rm_skip_dot(const char *name) {
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

static int rm_unlinkat_maybe(sb_i32 dirfd, const char *name, sb_i32 flags, int force, sb_i64 *out_err) {
	sb_i64 r = sb_sys_unlinkat(dirfd, name, flags);
	if (r >= 0) {
		return 0;
	}
	if (force && (sb_u64)(-r) == (sb_u64)SB_ENOENT) {
		return 0;
	}
	*out_err = r;
	return -1;
}

static int rm_dirfd_contents(const char *argv0, sb_i32 dirfd, int force, int recursive);

static int rm_entry_dirfd(const char *argv0, sb_i32 parentfd, const char *name, int force, int recursive) {
	// First try as a non-directory entry (regular file, symlink, etc.).
	sb_i64 err = 0;
	if (rm_unlinkat_maybe(parentfd, name, 0, force, &err) == 0) {
		return 0;
	}

	if (!recursive) {
		rm_print_err(argv0, name, err);
		return -1;
	}

	// If it might be a directory, open it with O_NOFOLLOW to avoid recursing into symlinks.
	sb_i64 cfd = sb_sys_openat(parentfd, name, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY | SB_O_NOFOLLOW, 0);
	if (cfd < 0) {
		if ((sb_u64)(-cfd) == (sb_u64)SB_ELOOP) {
			// It's a symlink; remove the link itself.
			sb_i64 err2 = 0;
			if (rm_unlinkat_maybe(parentfd, name, 0, force, &err2) == 0) {
				return 0;
			}
			rm_print_err(argv0, name, err2);
			return -1;
		}
		// Not a directory (or can't open it). Report the original unlink error.
		rm_print_err(argv0, name, err);
		return -1;
	}

	int any_fail = 0;
	if (rm_dirfd_contents(argv0, (sb_i32)cfd, force, recursive) != 0) {
		any_fail = 1;
	}
	(void)sb_sys_close((sb_i32)cfd);

	// Remove the now-empty directory entry.
	sb_i64 err3 = 0;
	if (rm_unlinkat_maybe(parentfd, name, SB_AT_REMOVEDIR, force, &err3) != 0) {
		rm_print_err(argv0, name, err3);
		any_fail = 1;
	}
	return any_fail ? -1 : 0;
}

static int rm_dirfd_contents(const char *argv0, sb_i32 dirfd, int force, int recursive) {
	sb_u8 buf[32768];
	int any_fail = 0;

	for (;;) {
		sb_i64 nread = sb_sys_getdents64(dirfd, buf, (sb_u32)sizeof(buf));
		if (nread < 0) {
			sb_die_errno(argv0, "getdents64", nread);
		}
		if (nread == 0) {
			break;
		}

		sb_u32 bpos = 0;
		while (bpos < (sb_u32)nread) {
			struct sb_linux_dirent64 *d = (struct sb_linux_dirent64 *)(buf + bpos);
			const char *name = d->d_name;
			if (!rm_skip_dot(name)) {
				if (rm_entry_dirfd(argv0, dirfd, name, force, recursive) != 0) {
					any_fail = 1;
				}
			}
			bpos += d->d_reclen;
		}
	}

	return any_fail ? -1 : 0;
}

static int rm_path(const char *argv0, const char *path, int force, int recursive) {
	// Fast path: try unlink first.
	sb_i64 err = 0;
	if (rm_unlinkat_maybe(SB_AT_FDCWD, path, 0, force, &err) == 0) {
		return 0;
	}

	if (!recursive) {
		rm_print_err(argv0, path, err);
		return -1;
	}

	// If it might be a directory, open it with O_NOFOLLOW to avoid following symlinks.
	sb_i64 dfd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY | SB_O_NOFOLLOW, 0);
	if (dfd < 0) {
		if ((sb_u64)(-dfd) == (sb_u64)SB_ELOOP) {
			// It's a symlink; remove the link itself.
			sb_i64 err2 = 0;
			if (rm_unlinkat_maybe(SB_AT_FDCWD, path, 0, force, &err2) == 0) {
				return 0;
			}
			rm_print_err(argv0, path, err2);
			return -1;
		}
		// Not a directory (or can't open it). Report the original unlink error.
		rm_print_err(argv0, path, err);
		return -1;
	}

	int any_fail = 0;
	if (rm_dirfd_contents(argv0, (sb_i32)dfd, force, recursive) != 0) {
		any_fail = 1;
	}
	(void)sb_sys_close((sb_i32)dfd);

	// Remove the now-empty directory.
	sb_i64 err3 = 0;
	if (rm_unlinkat_maybe(SB_AT_FDCWD, path, SB_AT_REMOVEDIR, force, &err3) != 0) {
		rm_print_err(argv0, path, err3);
		any_fail = 1;
	}
	return any_fail ? -1 : 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;

	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "rm";
	int force = 0;
	int recursive = 0;

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
		if (sb_streq(a, "-f")) {
			force = 1;
			continue;
		}
		if (sb_streq(a, "-r")) {
			recursive = 1;
			continue;
		}
		sb_die_usage(argv0, "rm [-f] [-r] [--] FILE...");
	}

	if (i >= argc) {
		sb_die_usage(argv0, "rm [-f] [-r] [--] FILE...");
	}

	int any_fail = 0;
	for (; i < argc; i++) {
		const char *path = argv[i] ? argv[i] : "";
		if (rm_path(argv0, path, force, recursive) != 0) {
			any_fail = 1;
		}
	}

	return any_fail ? 1 : 0;
}
