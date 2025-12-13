#include "../src/sb.h"

struct diff_reader {
	sb_i32 fd;
	sb_u32 pos;
	sb_u32 len;
	int eof;
	sb_u8 buf[4096];
};

static int diff_is_stdin(const char *p) {
	return p && sb_streq(p, "-");
}

static sb_i32 diff_open_ro(const char *argv0, const char *path) {
	if (diff_is_stdin(path)) {
		return 0;
	}
	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) {
		sb_die_errno(argv0, path, fd);
	}
	return (sb_i32)fd;
}

static void diff_close_if_needed(sb_i32 fd) {
	if (fd != 0) {
		(void)sb_sys_close(fd);
	}
}

static int diff_reader_fill(const char *argv0, struct diff_reader *r) {
	if (r->eof) {
		return 0;
	}
	sb_i64 n = sb_sys_read(r->fd, r->buf, sizeof(r->buf));
	if (n < 0) {
		sb_die_errno(argv0, "read", n);
	}
	if (n == 0) {
		r->eof = 1;
		r->pos = 0;
		r->len = 0;
		return 0;
	}
	r->pos = 0;
	r->len = (sb_u32)n;
	return 1;
}

static int diff_reader_getc(const char *argv0, struct diff_reader *r, sb_u8 *out, int *out_eof) {
	if (r->pos >= r->len) {
		if (!diff_reader_fill(argv0, r)) {
			*out_eof = 1;
			return 0;
		}
	}
	*out = r->buf[r->pos++];
	*out_eof = 0;
	return 1;
}

static int diff_read_line(const char *argv0, struct diff_reader *r, sb_u8 *dst, sb_usize cap, sb_usize *out_len, int *out_eof) {
	if (!dst || cap == 0 || !out_len || !out_eof) {
		return -1;
	}
	*out_len = 0;
	*out_eof = 0;

	for (;;) {
		sb_u8 c = 0;
		int eof = 0;
		if (!diff_reader_getc(argv0, r, &c, &eof)) {
			*out_eof = 1;
			return 0;
		}
		if (*out_len + 1 > cap) {
			// Line too long.
			return -1;
		}
		dst[*out_len] = c;
		(*out_len)++;
		if (c == '\n') {
			return 0;
		}
	}
}

static void diff_write_line_prefixed(const char *argv0, const char *prefix, const sb_u8 *line, sb_usize len) {
	sb_i64 w = sb_write_all(1, prefix, sb_strlen(prefix));
	if (w < 0) sb_die_errno(argv0, "write", w);
	w = sb_write_all(1, line, len);
	if (w < 0) sb_die_errno(argv0, "write", w);
	if (len == 0 || line[len - 1] != '\n') {
		w = sb_write_all(1, "\n", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "diff";

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
		sb_die_usage(argv0, "diff [--] FILE1 FILE2");
	}
	if (argc - i != 2) {
		sb_die_usage(argv0, "diff [--] FILE1 FILE2");
	}

	const char *p1 = argv[i];
	const char *p2 = argv[i + 1];
	if (!p1) p1 = "";
	if (!p2) p2 = "";

	sb_i32 fd1 = diff_open_ro(argv0, p1);
	sb_i32 fd2 = diff_open_ro(argv0, p2);

	struct diff_reader r1 = {.fd = fd1, .pos = 0, .len = 0, .eof = 0};
	struct diff_reader r2 = {.fd = fd2, .pos = 0, .len = 0, .eof = 0};

	sb_u8 l1[4096];
	sb_u8 l2[4096];
	sb_u64 line_no = 1;

	for (;;) {
		sb_usize n1 = 0;
		sb_usize n2 = 0;
		int eof1 = 0;
		int eof2 = 0;

		if (diff_read_line(argv0, &r1, l1, sizeof(l1), &n1, &eof1) != 0) {
			diff_close_if_needed(fd1);
			diff_close_if_needed(fd2);
			sb_die_errno(argv0, "line too long", (sb_i64)-SB_EINVAL);
		}
		if (diff_read_line(argv0, &r2, l2, sizeof(l2), &n2, &eof2) != 0) {
			diff_close_if_needed(fd1);
			diff_close_if_needed(fd2);
			sb_die_errno(argv0, "line too long", (sb_i64)-SB_EINVAL);
		}

		if (eof1 && eof2) {
			break;
		}

		int same = 0;
		if (eof1 == eof2 && n1 == n2) {
			same = 1;
			for (sb_usize k = 0; k < n1; k++) {
				if (l1[k] != l2[k]) {
					same = 0;
					break;
				}
			}
		}

		if (!same) {
			(void)sb_write_str(1, "diff: line ");
			(void)sb_write_u64_dec(1, line_no);
			(void)sb_write_str(1, "\n");
			if (!eof1) diff_write_line_prefixed(argv0, "< ", l1, n1);
			else diff_write_line_prefixed(argv0, "< ", (const sb_u8 *)"", 0);
			if (!eof2) diff_write_line_prefixed(argv0, "> ", l2, n2);
			else diff_write_line_prefixed(argv0, "> ", (const sb_u8 *)"", 0);

			diff_close_if_needed(fd1);
			diff_close_if_needed(fd2);
			return 1;
		}

		line_no++;
	}

	diff_close_if_needed(fd1);
	diff_close_if_needed(fd2);
	return 0;
}
