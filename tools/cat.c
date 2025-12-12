#include "../src/sb.h"

static void cat_fd(const char *argv0, sb_i32 fd) {
	sb_u8 buf[32768];
	for (;;) {
		sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) {
			break;
		}
		sb_i64 w = sb_write_all(1, buf, (sb_usize)r);
		if (w < 0) {
			sb_die_errno(argv0, "write", w);
		}
	}
}

static void cat_write_line_prefix(const char *argv0, sb_u64 line_no) {
	if (sb_write_u64_dec(1, line_no) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_all(1, "\t", 1) < 0) sb_die_errno(argv0, "write", -1);
}

static void cat_fd_numbered(const char *argv0, sb_i32 fd, sb_u64 *line_no, int *at_line_start) {
	sb_u8 buf[32768];
	for (;;) {
		sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) {
			break;
		}

		sb_u32 n = (sb_u32)r;
		sb_u32 i = 0;
		while (i < n) {
			if (*at_line_start) {
				cat_write_line_prefix(argv0, *line_no);
				*at_line_start = 0;
			}

			sb_u32 j = i;
			while (j < n && buf[j] != '\n') j++;
			if (j < n) {
				// include newline
				j++;
			}

			if (sb_write_all(1, &buf[i], (sb_usize)(j - i)) < 0) {
				sb_die_errno(argv0, "write", -1);
			}

			if (j > i && buf[j - 1] == '\n') {
				(*line_no)++;
				*at_line_start = 1;
			}
			i = j;
		}
	}
}

static void cat_path(const char *argv0, const char *path, int number, sb_u64 *line_no, int *at_line_start) {
	if (sb_streq(path, "-")) {
		if (number) cat_fd_numbered(argv0, 0, line_no, at_line_start);
		else cat_fd(argv0, 0);
		return;
	}

	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) {
		sb_die_errno(argv0, path, fd);
	}

	if (number) cat_fd_numbered(argv0, (sb_i32)fd, line_no, at_line_start);
	else cat_fd(argv0, (sb_i32)fd);
	(void)sb_sys_close((sb_i32)fd);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;

	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "cat";
	int number = 0;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a || a[0] != '-' || sb_streq(a, "-")) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (sb_streq(a, "-n")) {
			number = 1;
			continue;
		}
		sb_die_usage(argv0, "cat [-n] [--] [FILE...]");
	}

	sb_u64 line_no = 1;
	int at_line_start = 1;

	if (i >= argc) {
		if (number) cat_fd_numbered(argv0, 0, &line_no, &at_line_start);
		else cat_fd(argv0, 0);
		return 0;
	}

	for (; i < argc; i++) {
		const char *path = argv[i] ? argv[i] : "";
		cat_path(argv0, path, number, &line_no, &at_line_start);
	}

	return 0;
}
