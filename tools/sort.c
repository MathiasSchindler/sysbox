#include "../src/sb.h"

#define SORT_BUF_SIZE (4u * 1024u * 1024u) // 4 MiB total for data + metadata

struct sb_sort_line {
	sb_u32 off; // offset in sort_buf
	sb_u32 len; // bytes, excluding '\n'
};

static SB_NORETURN void sort_die_msg(const char *argv0, const char *msg) {
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, msg);
	(void)sb_write_str(2, "\n");
	sb_exit(1);
}

static sb_i32 sort_cmp_bytes(const sb_u8 *a, sb_u32 alen, const sb_u8 *b, sb_u32 blen) {
	sb_u32 n = (alen < blen) ? alen : blen;
	for (sb_u32 i = 0; i < n; i++) {
		sb_u8 ac = a[i];
		sb_u8 bc = b[i];
		if (ac < bc) return -1;
		if (ac > bc) return 1;
	}
	if (alen < blen) return -1;
	if (alen > blen) return 1;
	return 0;
}

static int sort_is_space(sb_u8 c) {
	return (c == (sb_u8)' ' || c == (sb_u8)'\n' || c == (sb_u8)'\t' || c == (sb_u8)'\r' || c == (sb_u8)'\v' || c == (sb_u8)'\f');
}

static sb_i64 sort_parse_leading_i64(const sb_u8 *s, sb_u32 len) {
	// Like a minimal "sort -n": skip leading whitespace, parse optional sign + digits.
	sb_u32 i = 0;
	while (i < len && sort_is_space(s[i])) {
		i++;
	}
	int neg = 0;
	if (i < len && (s[i] == (sb_u8)'-' || s[i] == (sb_u8)'+')) {
		neg = (s[i] == (sb_u8)'-');
		i++;
	}

	sb_u64 v = 0;
	int any = 0;
	for (; i < len; i++) {
		sb_u8 c = s[i];
		if (c < (sb_u8)'0' || c > (sb_u8)'9') {
			break;
		}
		any = 1;
		sb_u64 d = (sb_u64)(c - (sb_u8)'0');
		// Saturating multiply/add into u64 (keeps code small and deterministic).
		if (v > (sb_u64)(~(sb_u64)0) / 10u) {
			v = (sb_u64)(~(sb_u64)0);
		} else {
			v *= 10u;
			if (v > (sb_u64)(~(sb_u64)0) - d) {
				v = (sb_u64)(~(sb_u64)0);
			} else {
				v += d;
			}
		}
	}

	if (!any) {
		return 0;
	}
	if (neg) {
		// Saturate to INT64_MIN on overflow.
		if (v > (sb_u64)0x8000000000000000ull) {
			return (sb_i64)0x8000000000000000ull;
		}
		return -(sb_i64)v;
	}
	// Saturate to INT64_MAX on overflow.
	if (v > (sb_u64)0x7fffffffffffffffull) {
		return (sb_i64)0x7fffffffffffffffull;
	}
	return (sb_i64)v;
}

static sb_i32 sort_cmp_numeric_line(const sb_u8 *a, sb_u32 alen, const sb_u8 *b, sb_u32 blen) {
	sb_i64 av = sort_parse_leading_i64(a, alen);
	sb_i64 bv = sort_parse_leading_i64(b, blen);
	if (av < bv) return -1;
	if (av > bv) return 1;
	return sort_cmp_bytes(a, alen, b, blen);
}

static sb_i32 sort_line_cmp(const sb_u8 *base, const struct sb_sort_line *a, const struct sb_sort_line *b, int numeric) {
	if (numeric) {
		return sort_cmp_numeric_line(base + a->off, a->len, base + b->off, b->len);
	}
	return sort_cmp_bytes(base + a->off, a->len, base + b->off, b->len);
}

static void sort_swap(struct sb_sort_line *a, struct sb_sort_line *b) {
	struct sb_sort_line t = *a;
	*a = *b;
	*b = t;
}

static void sort_sift_down(const sb_u8 *base, struct sb_sort_line *lines, sb_u32 start, sb_u32 end, int reverse, int numeric) {
	sb_u32 root = start;
	for (;;) {
		sb_u32 child = root * 2u + 1u;
		if (child >= end) {
			return;
		}

		sb_u32 swap_i = root;
		sb_i32 c = sort_line_cmp(base, &lines[swap_i], &lines[child], numeric);
		if (reverse) c = -c;
		if (c < 0) {
			swap_i = child;
		}
		if (child + 1u < end) {
			sb_i32 c2 = sort_line_cmp(base, &lines[swap_i], &lines[child + 1u], numeric);
			if (reverse) c2 = -c2;
			if (c2 < 0) {
				swap_i = child + 1u;
			}
		}

		if (swap_i == root) {
			return;
		}
		sort_swap(&lines[root], &lines[swap_i]);
		root = swap_i;
	}
}

static void sort_heapsort(const sb_u8 *base, struct sb_sort_line *lines, sb_u32 n, int reverse, int numeric) {
	if (n < 2) {
		return;
	}

	// heapify
	for (sb_u32 i = (n / 2u); i > 0; i--) {
		sort_sift_down(base, lines, i - 1u, n, reverse, numeric);
	}

	// sort
	for (sb_u32 end = n; end > 1; end--) {
		sort_swap(&lines[0], &lines[end - 1u]);
		sort_sift_down(base, lines, 0, end - 1u, reverse, numeric);
	}
}

static void sort_add_line(const char *argv0, sb_u32 line_off, sb_u32 line_len, sb_u8 **meta_ptr, sb_u8 *data_end) {
	// Metadata grows down from the end of the buffer; data grows up.
	// Refuse to overlap.
	sb_u8 *next = *meta_ptr - (sb_usize)sizeof(struct sb_sort_line);
	if (next < data_end) {
		sort_die_msg(argv0, "input too large (buffer limit)");
	}
	*meta_ptr = next;
	struct sb_sort_line *ent = (struct sb_sort_line *)next;
	ent->off = line_off;
	ent->len = line_len;
}

static void sort_read_fd_into_buf(const char *argv0, sb_i32 fd, sb_u8 *buf, sb_u32 *io_data_len, sb_u8 **io_meta_ptr, sb_u32 *io_line_start) {
	sb_u32 data_len = *io_data_len;
	sb_u8 *meta_ptr = *io_meta_ptr;
	sb_u32 line_start = *io_line_start;

	for (;;) {
		// Keep at least one metadata entry worth of space so we can always record the last line.
		sb_u32 max_write;
		if (meta_ptr <= buf + data_len) {
			sort_die_msg(argv0, "input too large (buffer limit)");
		}
		max_write = (sb_u32)(meta_ptr - (buf + data_len));
		if (max_write <= (sb_u32)sizeof(struct sb_sort_line)) {
			sort_die_msg(argv0, "input too large (buffer limit)");
		}
		max_write -= (sb_u32)sizeof(struct sb_sort_line);
		if (max_write > 32768u) {
			max_write = 32768u;
		}

		sb_i64 r = sb_sys_read(fd, buf + data_len, (sb_usize)max_write);
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) {
			break;
		}

		sb_u32 old_len = data_len;
		data_len += (sb_u32)r;

		// Scan only the new bytes for newlines.
		for (sb_u32 i = old_len; i < data_len; i++) {
			if (buf[i] == (sb_u8)'\n') {
				// Record line [line_start, i)
				sb_u32 len = i - line_start;
				sort_add_line(argv0, line_start, len, &meta_ptr, buf + data_len);
				line_start = i + 1u;
			}
		}
	}

	*io_data_len = data_len;
	*io_meta_ptr = meta_ptr;
	*io_line_start = line_start;
}

static void sort_write_lines(const char *argv0, const sb_u8 *buf, const struct sb_sort_line *lines, sb_u32 n, int unique) {
	struct sb_sort_line prev;
	int have_prev = 0;

	for (sb_u32 i = 0; i < n; i++) {
		const struct sb_sort_line *ln = &lines[i];
		if (unique && have_prev) {
			if (sort_cmp_bytes(buf + prev.off, prev.len, buf + ln->off, ln->len) == 0) {
				continue;
			}
		}

		sb_i64 w = sb_write_all(1, buf + ln->off, (sb_usize)ln->len);
		if (w < 0) {
			sb_die_errno(argv0, "write", w);
		}
		w = sb_write_all(1, "\n", 1);
		if (w < 0) {
			sb_die_errno(argv0, "write", w);
		}

		prev = *ln;
		have_prev = 1;
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "sort";

	int reverse = 0;
	int unique = 0;
	int numeric = 0;

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
		if (sb_streq(a, "-r")) {
			reverse = 1;
			continue;
		}
		if (sb_streq(a, "-u")) {
			unique = 1;
			continue;
		}
		if (sb_streq(a, "-n")) {
			numeric = 1;
			continue;
		}
		sb_die_usage(argv0, "sort [-r] [-u] [-n] [FILE...]");
	}

	static sb_u8 sort_buf[SORT_BUF_SIZE];
	const sb_u32 cap = (sb_u32)SORT_BUF_SIZE;

	sb_u32 data_len = 0;
	sb_u8 *meta_ptr = sort_buf + cap;
	sb_u32 line_start = 0;

	int have_any_input = 0;

	if (i >= argc) {
		have_any_input = 1;
		sort_read_fd_into_buf(argv0, 0, sort_buf, &data_len, &meta_ptr, &line_start);
	} else {
		for (; i < argc; i++) {
			const char *path = argv[i];
			if (!path) {
				break;
			}
			have_any_input = 1;
			if (sb_streq(path, "-")) {
				sort_read_fd_into_buf(argv0, 0, sort_buf, &data_len, &meta_ptr, &line_start);
				continue;
			}
			sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
			if (fd < 0) {
				sb_die_errno(argv0, path, fd);
			}
			sort_read_fd_into_buf(argv0, (sb_i32)fd, sort_buf, &data_len, &meta_ptr, &line_start);
			(void)sb_sys_close((sb_i32)fd);
		}
	}

	if (!have_any_input) {
		return 0;
	}

	// Final line (no trailing newline)
	if (line_start < data_len) {
		sort_add_line(argv0, line_start, data_len - line_start, &meta_ptr, sort_buf + data_len);
	}

	sb_u32 nlines = (sb_u32)((sort_buf + cap - meta_ptr) / (sb_u32)sizeof(struct sb_sort_line));
	if (nlines == 0) {
		return 0;
	}

	struct sb_sort_line *lines = (struct sb_sort_line *)meta_ptr;
	sort_heapsort(sort_buf, lines, nlines, reverse, numeric);
	sort_write_lines(argv0, sort_buf, lines, nlines, unique);
	return 0;
}
