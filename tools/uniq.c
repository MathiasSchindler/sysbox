#include "../src/sb.h"

#define UNIQ_READ_BUF_SIZE 32768u
#define UNIQ_LINE_MAX 65536u

struct uniq_reader {
	sb_i32 fd;
	sb_u8 buf[UNIQ_READ_BUF_SIZE];
	sb_u32 pos;
	sb_u32 len;
	int eof;
};

static SB_NORETURN void uniq_die_msg(const char *argv0, const char *msg) {
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, msg);
	(void)sb_write_str(2, "\n");
	sb_exit(1);
}

static int uniq_fill(struct uniq_reader *r, const char *argv0) {
	if (r->eof) {
		return 0;
	}
	sb_i64 n = sb_sys_read(r->fd, r->buf, (sb_usize)sizeof(r->buf));
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

// Reads one line (without the trailing '\n') into out[].
// Returns 1 if a line was read, 0 on EOF with no data.
static int uniq_read_line(struct uniq_reader *r, const char *argv0, sb_u8 *out, sb_u32 *out_len) {
	sb_u32 n = 0;
	int have_any = 0;

	for (;;) {
		if (r->pos >= r->len) {
			if (!uniq_fill(r, argv0)) {
				break;
			}
		}

		while (r->pos < r->len) {
			sb_u8 c = r->buf[r->pos++];
			have_any = 1;
			if (c == (sb_u8)'\n') {
				*out_len = n;
				return 1;
			}
			if (n >= UNIQ_LINE_MAX) {
				uniq_die_msg(argv0, "line too long");
			}
			out[n++] = c;
		}
	}

	if (!have_any) {
		return 0;
	}
	*out_len = n;
	return 1;
}

static int uniq_lines_equal(const sb_u8 *a, sb_u32 alen, const sb_u8 *b, sb_u32 blen) {
	if (alen != blen) {
		return 0;
	}
	for (sb_u32 i = 0; i < alen; i++) {
		if (a[i] != b[i]) {
			return 0;
		}
	}
	return 1;
}

static void uniq_write_group(const char *argv0, const sb_u8 *line, sb_u32 len, sb_u64 count, int opt_c, int opt_d, int opt_u) {
	int print_it = 1;
	if (opt_d) {
		print_it = (count > 1);
	}
	if (opt_u) {
		print_it = (count == 1);
	}
	if (!print_it) {
		return;
	}

	if (opt_c) {
		sb_i64 w = sb_write_u64_dec(1, count);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, " ", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
	}

	sb_i64 w = sb_write_all(1, line, (sb_usize)len);
	if (w < 0) sb_die_errno(argv0, "write", w);
	w = sb_write_all(1, "\n", 1);
	if (w < 0) sb_die_errno(argv0, "write", w);
}

static void uniq_fd(const char *argv0, sb_i32 fd, int opt_c, int opt_d, int opt_u, sb_u8 *prev, sb_u32 *prev_len, sb_u64 *group_count, int *have_prev) {
	struct uniq_reader r;
	r.fd = fd;
	r.pos = 0;
	r.len = 0;
	r.eof = 0;

	sb_u8 cur[UNIQ_LINE_MAX];
	sb_u32 cur_len = 0;

	for (;;) {
		int ok = uniq_read_line(&r, argv0, cur, &cur_len);
		if (!ok) {
			break;
		}

		if (!*have_prev) {
			for (sb_u32 i = 0; i < cur_len; i++) prev[i] = cur[i];
			*prev_len = cur_len;
			*group_count = 1;
			*have_prev = 1;
			continue;
		}

		if (uniq_lines_equal(prev, *prev_len, cur, cur_len)) {
			(*group_count)++;
			continue;
		}

		uniq_write_group(argv0, prev, *prev_len, *group_count, opt_c, opt_d, opt_u);

		for (sb_u32 i = 0; i < cur_len; i++) prev[i] = cur[i];
		*prev_len = cur_len;
		*group_count = 1;
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "uniq";

	int opt_c = 0;
	int opt_d = 0;
	int opt_u = 0;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] != '-' || sb_streq(a, "-")) {
			break;
		}
		if (sb_streq(a, "-c")) {
			opt_c = 1;
			continue;
		}
		if (sb_streq(a, "-d")) {
			opt_d = 1;
			continue;
		}
		if (sb_streq(a, "-u")) {
			opt_u = 1;
			continue;
		}
		sb_die_usage(argv0, "uniq [-c] [-d|-u] [FILE...]");
	}

	if (opt_d && opt_u) {
		sb_die_usage(argv0, "uniq [-c] [-d|-u] [FILE...]");
	}

	// Keep group state across multiple FILE operands to behave like a concatenated stream.
	static sb_u8 prev[UNIQ_LINE_MAX];
	sb_u32 prev_len = 0;
	sb_u64 group_count = 0;
	int have_prev = 0;

	if (i >= argc) {
		uniq_fd(argv0, 0, opt_c, opt_d, opt_u, prev, &prev_len, &group_count, &have_prev);
	} else {
		for (; i < argc; i++) {
			const char *path = argv[i];
			if (!path) break;
			if (sb_streq(path, "-")) {
				uniq_fd(argv0, 0, opt_c, opt_d, opt_u, prev, &prev_len, &group_count, &have_prev);
				continue;
			}
			sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
			if (fd < 0) {
				sb_die_errno(argv0, path, fd);
			}
			uniq_fd(argv0, (sb_i32)fd, opt_c, opt_d, opt_u, prev, &prev_len, &group_count, &have_prev);
			(void)sb_sys_close((sb_i32)fd);
		}
	}

	if (have_prev) {
		uniq_write_group(argv0, prev, prev_len, group_count, opt_c, opt_d, opt_u);
	}
	return 0;
}
