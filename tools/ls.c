#include "../src/sb.h"

struct sb_linux_dirent64 {
	sb_u64 d_ino;
	sb_i64 d_off;
	sb_u16 d_reclen;
	sb_u8 d_type;
	char d_name[];
} __attribute__((packed));

static int ls_is_dot_or_dotdot(const char *name) {
	if (!name) return 0;
	if (name[0] != '.') return 0;
	if (name[1] == '\0') return 1;
	if (name[1] == '.' && name[2] == '\0') return 1;
	return 0;
}

static void ls_join_path_or_die(const char *argv0, const char *base, const char *name, char out[4096]) {
	// Refuse overly long paths to keep things deterministic.
	sb_usize blen = sb_strlen(base);
	sb_usize nlen = sb_strlen(name);
	int need_slash = 1;
	if (blen == 0 || (blen == 1 && base[0] == '.')) {
		// Use just the name.
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

static void ls_write_mode(const char *argv0, sb_u32 mode) {
	char p[10];
	p[0] = (mode & 0400u) ? 'r' : '-';
	p[1] = (mode & 0200u) ? 'w' : '-';
	p[2] = (mode & 0100u) ? 'x' : '-';
	p[3] = (mode & 0040u) ? 'r' : '-';
	p[4] = (mode & 0020u) ? 'w' : '-';
	p[5] = (mode & 0010u) ? 'x' : '-';
	p[6] = (mode & 0004u) ? 'r' : '-';
	p[7] = (mode & 0002u) ? 'w' : '-';
	p[8] = (mode & 0001u) ? 'x' : '-';
	p[9] = 0;

	char type = '?';
	sb_u32 t = mode & SB_S_IFMT;
	if (t == SB_S_IFDIR) type = 'd';
	else if (t == SB_S_IFREG) type = '-';
	else if (t == SB_S_IFLNK) type = 'l';

	if (sb_write_all(1, &type, 1) < 0 || sb_write_all(1, p, 9) < 0) {
		sb_die_errno(argv0, "write", -1);
	}
}

static void ls_write_size(const char *argv0, sb_u64 size, int human) {
	if (!human) {
		if (sb_write_u64_dec(1, size) < 0) sb_die_errno(argv0, "write", -1);
		return;
	}

	char suf = 0;
	while (size >= 1024u) {
		if (!suf) suf = 'K';
		else if (suf == 'K') suf = 'M';
		else if (suf == 'M') suf = 'G';
		else if (suf == 'G') suf = 'T';
		else break;
		size /= 1024u;
	}
	if (sb_write_u64_dec(1, size) < 0) sb_die_errno(argv0, "write", -1);
	if (suf) {
		if (sb_write_all(1, &suf, 1) < 0) sb_die_errno(argv0, "write", -1);
	}
}

static void ls_print_long_at(const char *argv0, sb_i32 dirfd, const char *name, int human) {
	struct sb_stat st;
	sb_i64 r = sb_sys_newfstatat(dirfd, name, &st, SB_AT_SYMLINK_NOFOLLOW);
	if (r < 0) {
		sb_die_errno(argv0, name, r);
	}

	ls_write_mode(argv0, st.st_mode);
	if (sb_write_all(1, " ", 1) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_u64_dec(1, st.st_nlink) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_all(1, " ", 1) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_u64_dec(1, st.st_uid) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_all(1, " ", 1) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_u64_dec(1, st.st_gid) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_all(1, " ", 1) < 0) sb_die_errno(argv0, "write", -1);
	ls_write_size(argv0, (sb_u64)st.st_size, human);
	if (sb_write_all(1, " ", 1) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_u64_dec(1, st.st_mtime) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_all(1, " ", 1) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_all(1, name, sb_strlen(name)) < 0 || sb_write_all(1, "\n", 1) < 0) {
		sb_die_errno(argv0, "write", -1);
	}
}

static void ls_dir(const char *argv0, const char *path, int show_all, int long_mode, int human, int recursive, int is_first) {
	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY, 0);
	if (fd < 0) {
		// If the operand is a file, GNU ls prints it. Minimal behavior: print operand.
		// openat(O_DIRECTORY) returns -ENOTDIR in that case.
		if ((sb_u64)(-fd) == (sb_u64)SB_ENOTDIR) {
			if (long_mode) {
				ls_print_long_at(argv0, SB_AT_FDCWD, path, human);
				return;
			}
			if (sb_write_str(1, path) < 0 || sb_write_all(1, "\n", 1) < 0) sb_die_errno(argv0, "write", -1);
			return;
		}
		sb_die_errno(argv0, path, fd);
	}

	if (recursive) {
		if (!is_first) {
			if (sb_write_all(1, "\n", 1) < 0) sb_die_errno(argv0, "write", -1);
		}
		if (sb_write_str(1, path) < 0 || sb_write_all(1, ":\n", 2) < 0) sb_die_errno(argv0, "write", -1);
	}

	// Collect subdirectories for -R without allocating.
	char subpool[32768];
	sb_u32 subpool_len = 0;
	sb_u32 sub_offs[256];
	sb_u32 sub_n = 0;

	sb_u8 buf[32768];
	for (;;) {
		sb_i64 nread = sb_sys_getdents64((sb_i32)fd, buf, (sb_u32)sizeof(buf));
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
			int visible = show_all || (name[0] != '.');
			if (visible) {
				if (long_mode) {
					ls_print_long_at(argv0, (sb_i32)fd, name, human);
				} else {
					if (sb_write_all(1, name, sb_strlen(name)) < 0 || sb_write_all(1, "\n", 1) < 0) {
						sb_die_errno(argv0, "write", -1);
					}
				}
			}

			if (recursive && visible && !ls_is_dot_or_dotdot(name)) {
				struct sb_stat st;
				sb_i64 sr = sb_sys_newfstatat((sb_i32)fd, name, &st, SB_AT_SYMLINK_NOFOLLOW);
				if (sr >= 0 && ((st.st_mode & SB_S_IFMT) == SB_S_IFDIR)) {
					// Copy name into subpool for later recursion.
					sb_usize nlen = sb_strlen(name);
					if (sub_n < (sb_u32)(sizeof(sub_offs) / sizeof(sub_offs[0])) && subpool_len + (sb_u32)nlen + 1u <= (sb_u32)sizeof(subpool)) {
						sub_offs[sub_n] = subpool_len;
						for (sb_usize k = 0; k < nlen; k++) {
							subpool[subpool_len + (sb_u32)k] = name[k];
						}
						subpool[subpool_len + (sb_u32)nlen] = 0;
						subpool_len += (sb_u32)nlen + 1u;
						sub_n++;
					}
				}
			}
			bpos += d->d_reclen;
		}
	}

	(void)sb_sys_close((sb_i32)fd);

	if (recursive) {
		for (sb_u32 si = 0; si < sub_n; si++) {
			const char *subname = subpool + sub_offs[si];
			char child[4096];
			ls_join_path_or_die(argv0, path, subname, child);
			ls_dir(argv0, child, show_all, long_mode, human, recursive, 0);
		}
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;

	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "ls";
	int show_all = 0;
	int long_mode = 0;
	int human = 0;
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
		if (a[1] && a[1] != '-' && a[2]) {
			// Combined short options: -alh
			for (sb_u32 j = 1; a[j]; j++) {
				if (a[j] == 'a') show_all = 1;
				else if (a[j] == 'l') long_mode = 1;
				else if (a[j] == 'h') human = 1;
				else if (a[j] == 'R') recursive = 1;
				else sb_die_usage(argv0, "ls [-a] [-l] [-h] [-R] [DIR]");
			}
			continue;
		}
		if (sb_streq(a, "-a")) {
			show_all = 1;
			continue;
		}
		if (sb_streq(a, "-l")) {
			long_mode = 1;
			continue;
		}
		if (sb_streq(a, "-h")) {
			human = 1;
			continue;
		}
		if (sb_streq(a, "-R")) {
			recursive = 1;
			continue;
		}
		sb_die_usage(argv0, "ls [-a] [-l] [-h] [-R] [DIR]");
	}

	int remaining = argc - i;
	if (remaining == 0) {
		ls_dir(argv0, ".", show_all, long_mode, human, recursive, 1);
		return 0;
	}
	if (remaining == 1) {
		ls_dir(argv0, argv[i], show_all, long_mode, human, recursive, 1);
		return 0;
	}

	sb_die_usage(argv0, "ls [-a] [-l] [-h] [-R] [DIR]");
}
