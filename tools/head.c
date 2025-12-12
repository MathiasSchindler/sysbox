#include "../src/sb.h"

static sb_u64 head_parse_n_or_die(const char *argv0, const char *s) {
	sb_u64 n = 0;
	if (sb_parse_u64_dec(s, &n) != 0) {
		sb_die_usage(argv0, "head [-n N] [-c N] [FILE...]");
	}
	return n;
}

static sb_u64 head_parse_c_or_die(const char *argv0, const char *s) {
	return head_parse_n_or_die(argv0, s);
}

static int head_fd(const char *argv0, sb_i32 fd, sb_u64 nlines) {
	sb_u8 buf[32768];
	sb_u64 lines = 0;
	for (;;) {
		if (lines >= nlines) {
			return 0;
		}
		sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) {
			return 0;
		}

		sb_i64 cut = r;
		for (sb_i64 i = 0; i < r; i++) {
			if (buf[i] == (sb_u8)'\n') {
				lines++;
				if (lines >= nlines) {
					cut = i + 1;
					break;
				}
			}
		}

		sb_i64 w = sb_write_all(1, buf, (sb_usize)cut);
		if (w < 0) {
			sb_die_errno(argv0, "write", w);
		}
	}
}

static int head_fd_bytes(const char *argv0, sb_i32 fd, sb_u64 nbytes) {
	sb_u8 buf[32768];
	sb_u64 remaining = nbytes;
	for (;;) {
		if (remaining == 0) {
			return 0;
		}
		sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) {
			return 0;
		}

		sb_usize cut = (sb_usize)r;
		if ((sb_u64)r > remaining) {
			cut = (sb_usize)remaining;
		}

		sb_i64 w = sb_write_all(1, buf, cut);
		if (w < 0) {
			sb_die_errno(argv0, "write", w);
		}
		remaining -= (sb_u64)cut;
	}
}

static int head_path(const char *argv0, const char *path, int bytes_mode, sb_u64 n) {
	if (sb_streq(path, "-")) {
		if (bytes_mode) return head_fd_bytes(argv0, 0, n);
		return head_fd(argv0, 0, n);
	}
	
	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) {
		sb_die_errno(argv0, path, fd);
	}
	if (bytes_mode) (void)head_fd_bytes(argv0, (sb_i32)fd, n);
	else (void)head_fd(argv0, (sb_i32)fd, n);
	(void)sb_sys_close((sb_i32)fd);
	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "head";

	int bytes_mode = 0;
	sb_u64 n = 10;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) {
			break;
		}
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] != '-' || sb_streq(a, "-")) {
			break;
		}
		if (sb_streq(a, "-n")) {
			if (i + 1 >= argc) {
				sb_die_usage(argv0, "head [-n N] [-c N] [FILE...]");
			}
			bytes_mode = 0;
			n = head_parse_n_or_die(argv0, argv[i + 1]);
			i++;
			continue;
		}
		if (sb_streq(a, "-c")) {
			if (i + 1 >= argc) {
				sb_die_usage(argv0, "head [-n N] [-c N] [FILE...]");
			}
			bytes_mode = 1;
			n = head_parse_c_or_die(argv0, argv[i + 1]);
			i++;
			continue;
		}
		// Allow -nN as a small convenience.
		if (a[1] == 'n' && a[2] != 0) {
			bytes_mode = 0;
			n = head_parse_n_or_die(argv0, a + 2);
			continue;
		}
		// Allow -cN as a small convenience.
		if (a[1] == 'c' && a[2] != 0) {
			bytes_mode = 1;
			n = head_parse_c_or_die(argv0, a + 2);
			continue;
		}
		sb_die_usage(argv0, "head [-n N] [-c N] [FILE...]");
	}

	if (i >= argc) {
		if (bytes_mode) return head_fd_bytes(argv0, 0, n);
		return head_fd(argv0, 0, n);
	}

	for (; i < argc; i++) {
		const char *path = argv[i] ? argv[i] : "";
		(void)head_path(argv0, path, bytes_mode, n);
	}
	return 0;
}
