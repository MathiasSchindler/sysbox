#include "../src/sb.h"

#define COL_MAX_COLS 4096

static int is_printable_or_tab(sb_u8 c) {
	if (c == (sb_u8)'\t') return 1;
	return (c >= 0x20 && c <= 0x7e);
}

static void col_flush_line(sb_u8 *line, sb_u32 len) {
	for (sb_u32 i = 0; i < len; i++) {
		sb_u8 c = line[i];
		if (c == 0) c = (sb_u8)' ';
		(void)sb_write_all(1, &c, 1);
	}
	(void)sb_write_all(1, "\n", 1);
}

static void col_fd(const char *argv0, sb_i32 fd) {
	(void)argv0;
	sb_u8 in[4096];
	sb_u8 line[COL_MAX_COLS];
	sb_u32 cursor = 0;
	sb_u32 linelen = 0;

	// initialize line buffer to zeros; output replaces 0 with spaces.
	for (sb_u32 i = 0; i < COL_MAX_COLS; i++) line[i] = 0;

	while (1) {
		sb_i64 n = sb_sys_read(fd, in, sizeof(in));
		if (n < 0) sb_die_errno(argv0, "read", n);
		if (n == 0) break;
		for (sb_i64 i = 0; i < n; i++) {
			sb_u8 c = in[i];
			if (c == (sb_u8)'\n') {
				col_flush_line(line, linelen);
				cursor = 0;
				linelen = 0;
				for (sb_u32 k = 0; k < COL_MAX_COLS; k++) line[k] = 0;
				continue;
			}
			if (c == (sb_u8)'\f') {
				// treat form feed as newline
				col_flush_line(line, linelen);
				cursor = 0;
				linelen = 0;
				for (sb_u32 k = 0; k < COL_MAX_COLS; k++) line[k] = 0;
				continue;
			}
			if (c == (sb_u8)'\r') {
				cursor = 0;
				continue;
			}
			if (c == (sb_u8)'\b') {
				if (cursor > 0) cursor--;
				continue;
			}
			if (!is_printable_or_tab(c)) {
				continue;
			}

			if (cursor >= COL_MAX_COLS) {
				sb_die_errno(argv0, "line too wide", (sb_i64)-SB_EINVAL);
			}
			line[cursor] = c;
			cursor++;
			if (cursor > linelen) linelen = cursor;
		}
	}

	// flush last line (if any content)
	if (linelen) {
		col_flush_line(line, linelen);
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "col";

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] == '-') sb_die_usage(argv0, "col [FILE...]");
		break;
	}

	if (i >= argc) {
		col_fd(argv0, 0);
		return 0;
	}

	for (; i < argc; i++) {
		const char *path = argv[i];
		if (!path) continue;
		sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (fd < 0) sb_die_errno(argv0, path, fd);
		col_fd(argv0, (sb_i32)fd);
		(void)sb_sys_close((sb_i32)fd);
	}
	return 0;
}
