#include "../src/sb.h"

// Tail (streaming): keep last N lines using a fixed-size ring buffer.
// Limitations: if total buffered bytes exceed capacity, the oldest data is dropped.

#define TAIL_BUF_SIZE (1024u * 1024u) // 1 MiB
#define TAIL_MAX_LINES 8192u

struct tail_state {
	sb_u8 buf[TAIL_BUF_SIZE];
	sb_u32 head; // next write position
	sb_u32 len;  // valid bytes in buffer (<= TAIL_BUF_SIZE)

	sb_u32 starts[TAIL_MAX_LINES]; // indices in buf[] of line starts
	sb_u32 start_count;
};

static sb_u64 tail_parse_n_or_die(const char *argv0, const char *s) {
	sb_u64 n = 0;
	if (sb_parse_u64_dec(s, &n) != 0) {
		sb_die_usage(argv0, "tail [-n N] [-c N] [FILE...]");
	}
	return n;
}

static sb_u64 tail_parse_c_or_die(const char *argv0, const char *s) {
	return tail_parse_n_or_die(argv0, s);
}

struct tail_bytes_state {
	sb_u8 buf[TAIL_BUF_SIZE];
	sb_u32 cap;
	sb_u32 head;
	sb_u32 len;
};

static void tail_bytes_feed(struct tail_bytes_state *st, const sb_u8 *p, sb_usize n) {
	if (st->cap == 0) return;
	for (sb_usize i = 0; i < n; i++) {
		st->buf[st->head] = p[i];
		st->head = (st->head + 1) % st->cap;
		if (st->len < st->cap) st->len++;
	}
}

static void tail_bytes_write(const char *argv0, const struct tail_bytes_state *st) {
	if (st->len == 0 || st->cap == 0) return;
	sb_u32 oldest = (st->head + st->cap - st->len) % st->cap;
	if (oldest < st->head) {
		sb_i64 w = sb_write_all(1, st->buf + oldest, (sb_usize)(st->head - oldest));
		if (w < 0) sb_die_errno(argv0, "write", w);
		return;
	}
	// Wrapped.
	sb_i64 w1 = sb_write_all(1, st->buf + oldest, (sb_usize)(st->cap - oldest));
	if (w1 < 0) sb_die_errno(argv0, "write", w1);
	sb_i64 w2 = sb_write_all(1, st->buf, (sb_usize)st->head);
	if (w2 < 0) sb_die_errno(argv0, "write", w2);
}

static void tail_drop_oldest_bytes(struct tail_state *st, sb_u32 drop) {
	if (drop >= st->len) {
		st->len = 0;
		st->start_count = 0;
		return;
	}

	// When dropping bytes, any recorded line starts that fall into the dropped region must be removed.
	sb_u32 old_len = st->len;
	st->len = old_len - drop;

	// Compute new oldest byte index.
	sb_u32 new_oldest = (st->head + (sb_u32)TAIL_BUF_SIZE - st->len) % (sb_u32)TAIL_BUF_SIZE;

	// Filter starts[] in-place to only keep those within the new buffer window.
	sb_u32 out = 0;
	for (sb_u32 i = 0; i < st->start_count; i++) {
		sb_u32 idx = st->starts[i];
		// Determine if idx is within [new_oldest, head) circular range of length st->len.
		// Convert idx to offset from new_oldest.
		sb_u32 off = (idx + (sb_u32)TAIL_BUF_SIZE - new_oldest) % (sb_u32)TAIL_BUF_SIZE;
		if (off < st->len) {
			st->starts[out++] = idx;
		}
	}
	st->start_count = out;

	// Ensure we have at least one start recorded when buffer is non-empty.
	if (st->len != 0 && st->start_count == 0) {
		st->starts[0] = new_oldest;
		st->start_count = 1;
	}
}

static void tail_record_line_start(struct tail_state *st, sb_u32 idx) {
	if (st->start_count < (sb_u32)TAIL_MAX_LINES) {
		st->starts[st->start_count++] = idx;
		return;
	}
	// If the starts ring is full, drop the oldest start.
	for (sb_u32 i = 1; i < st->start_count; i++) {
		st->starts[i - 1] = st->starts[i];
	}
	st->starts[st->start_count - 1] = idx;
}

static void tail_feed_byte(struct tail_state *st, sb_u8 b) {
	// If buffer is full, drop one byte (and any starts it invalidates).
	if (st->len == (sb_u32)TAIL_BUF_SIZE) {
		tail_drop_oldest_bytes(st, 1);
	}

	st->buf[st->head] = b;
	st->head = (st->head + 1) % (sb_u32)TAIL_BUF_SIZE;
	st->len++;

	if (b == (sb_u8)'\n') {
		// Start of next line is current head.
		tail_record_line_start(st, st->head);
	}
}

static void tail_feed(struct tail_state *st, const sb_u8 *p, sb_usize n) {
	for (sb_usize i = 0; i < n; i++) {
		tail_feed_byte(st, p[i]);
	}
}

static void tail_write_range(const char *argv0, const struct tail_state *st, sb_u32 from, sb_u32 to_exclusive) {
	if (from == to_exclusive) {
		return;
	}
	if (from < to_exclusive) {
		sb_i64 w = sb_write_all(1, st->buf + from, (sb_usize)(to_exclusive - from));
		if (w < 0) sb_die_errno(argv0, "write", w);
		return;
	}
	// Wrapped.
	sb_i64 w1 = sb_write_all(1, st->buf + from, (sb_usize)((sb_u32)TAIL_BUF_SIZE - from));
	if (w1 < 0) sb_die_errno(argv0, "write", w1);
	sb_i64 w2 = sb_write_all(1, st->buf, (sb_usize)to_exclusive);
	if (w2 < 0) sb_die_errno(argv0, "write", w2);
}

static int tail_fd(const char *argv0, sb_i32 fd, sb_u64 nlines) {
	struct tail_state st;
	st.head = 0;
	st.len = 0;
	st.start_count = 0;

	// Record the start of the first line.
	tail_record_line_start(&st, 0);

	sb_u8 buf[32768];
	for (;;) {
		sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) {
			break;
		}
		tail_feed(&st, buf, (sb_usize)r);
	}

	if (nlines == 0) {
		return 0;
	}

	// Determine which recorded start corresponds to the last N lines.
	// starts[] contains the start of each line (including a start after each newline).
	sb_u32 total_starts = st.start_count;
	if (total_starts == 0 || st.len == 0) {
		return 0;
	}

	// If the input ends with a newline, the final recorded start is for an empty line;
	// ignore it for line counting so `tail -n 2` prints the last 2 non-empty lines.
	sb_u32 effective_starts = total_starts;
	{
		sb_u32 last_idx = (st.head + (sb_u32)TAIL_BUF_SIZE - 1) % (sb_u32)TAIL_BUF_SIZE;
		if (st.buf[last_idx] == (sb_u8)'\n' && effective_starts > 0) {
			effective_starts--;
		}
	}

	// For tail semantics, printing from (effective_starts - nlines) (clamped) is acceptable.
	sb_u32 want = (nlines > (sb_u64)0xFFFFFFFFu) ? 0xFFFFFFFFu : (sb_u32)nlines;
	sb_u32 start_idx = 0;
	if (effective_starts > want) {
		start_idx = effective_starts - want;
	}

	sb_u32 from = st.starts[start_idx];
	sb_u32 to = st.head;
	// If buffer is full and head == from, that could mean "all bytes"; still write full buffer.
	if (st.len == (sb_u32)TAIL_BUF_SIZE && from == to) {
		tail_write_range(argv0, &st, to, to); // no-op
		// Write full buffer from oldest.
		sb_u32 oldest = (st.head + (sb_u32)TAIL_BUF_SIZE - st.len) % (sb_u32)TAIL_BUF_SIZE;
		tail_write_range(argv0, &st, oldest, st.head);
		return 0;
	}

	tail_write_range(argv0, &st, from, to);
	return 0;
}

// Seek-based tail -n for regular files: scan backwards for N newlines, then stream forward.
// Returns 0 on success, 1 if the fd is not seekable (ESPIPE).
static int tail_fd_lines_seek(const char *argv0, sb_i32 fd, sb_u64 nlines) {
	if (nlines == 0) return 0;

	sb_i64 end = sb_sys_lseek(fd, 0, SB_SEEK_END);
	if (end < 0) {
		if ((sb_u64)(-end) == (sb_u64)SB_ESPIPE) return 1;
		sb_die_errno(argv0, "lseek", end);
	}
	if (end == 0) return 0;

	int ignore_trailing_nl = 0;
	{
		sb_u8 last = 0;
		sb_i64 sr = sb_sys_lseek(fd, end - 1, SB_SEEK_SET);
		if (sr < 0) sb_die_errno(argv0, "lseek", sr);
		sb_i64 rr = sb_sys_read(fd, &last, 1);
		if (rr < 0) sb_die_errno(argv0, "read", rr);
		if (rr == 1 && last == (sb_u8)'\n') ignore_trailing_nl = 1;
	}

	sb_u8 buf[32768];
	sb_i64 pos = end;
	sb_u64 found = 0;
	sb_i64 start = 0;

	while (pos > 0 && found < nlines) {
		sb_i64 step = (pos > (sb_i64)sizeof(buf)) ? (sb_i64)sizeof(buf) : pos;
		pos -= step;

		sb_i64 sr = sb_sys_lseek(fd, pos, SB_SEEK_SET);
		if (sr < 0) sb_die_errno(argv0, "lseek", sr);

		sb_i64 r = sb_sys_read(fd, buf, (sb_usize)step);
		if (r < 0) sb_die_errno(argv0, "read", r);
		if (r == 0) break;

		for (sb_i64 i = r - 1; i >= 0; i--) {
			sb_i64 abs = pos + i;
			if (ignore_trailing_nl && abs == end - 1) {
				continue;
			}
			if (buf[(sb_usize)i] == (sb_u8)'\n') {
				found++;
				if (found == nlines) {
					start = abs + 1;
					pos = 0;
					break;
				}
			}
		}
	}

	if (found < nlines) {
		start = 0;
	}

	// Stream from start to end.
	{
		sb_i64 sr = sb_sys_lseek(fd, start, SB_SEEK_SET);
		if (sr < 0) sb_die_errno(argv0, "lseek", sr);

		for (;;) {
			sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
			if (r < 0) sb_die_errno(argv0, "read", r);
			if (r == 0) break;
			sb_i64 w = sb_write_all(1, buf, (sb_usize)r);
			if (w < 0) sb_die_errno(argv0, "write", w);
		}
	}

	return 0;
}

static int tail_fd_bytes(const char *argv0, sb_i32 fd, sb_u64 nbytes) {
	if (nbytes == 0) return 0;

	// Try seek-based implementation.
	sb_i64 end = sb_sys_lseek(fd, 0, SB_SEEK_END);
	if (end >= 0) {
		sb_i64 start = 0;
		if ((sb_u64)end > nbytes) {
			// end - nbytes fits in signed range here.
			start = end - (sb_i64)nbytes;
		}
		sb_i64 sr = sb_sys_lseek(fd, start, SB_SEEK_SET);
		if (sr < 0) sb_die_errno(argv0, "lseek", sr);

		sb_u8 buf[32768];
		for (;;) {
			sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
			if (r < 0) sb_die_errno(argv0, "read", r);
			if (r == 0) break;
			sb_i64 w = sb_write_all(1, buf, (sb_usize)r);
			if (w < 0) sb_die_errno(argv0, "write", w);
		}
		return 0;
	}

	// Non-seekable fallback.
	if ((sb_u64)(-end) != (sb_u64)SB_ESPIPE) {
		sb_die_errno(argv0, "lseek", end);
	}

	struct tail_bytes_state st;
	st.cap = (nbytes > (sb_u64)TAIL_BUF_SIZE) ? (sb_u32)TAIL_BUF_SIZE : (sb_u32)nbytes;
	st.head = 0;
	st.len = 0;

	sb_u8 buf[32768];
	for (;;) {
		sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
		if (r < 0) sb_die_errno(argv0, "read", r);
		if (r == 0) break;
		tail_bytes_feed(&st, buf, (sb_usize)r);
	}
	if (st.cap == 0) return 0;
	tail_bytes_write(argv0, &st);
	return 0;
}

static int tail_path(const char *argv0, const char *path, int bytes_mode, sb_u64 n) {
	if (sb_streq(path, "-")) {
		if (bytes_mode) return tail_fd_bytes(argv0, 0, n);
		return tail_fd(argv0, 0, n);
	}
	
	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) {
		sb_die_errno(argv0, path, fd);
	}
	if (bytes_mode) {
		(void)tail_fd_bytes(argv0, (sb_i32)fd, n);
	} else {
		int seek_rc = tail_fd_lines_seek(argv0, (sb_i32)fd, n);
		if (seek_rc == 1) {
			(void)tail_fd(argv0, (sb_i32)fd, n);
		}
	}
	(void)sb_sys_close((sb_i32)fd);
	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "tail";

	int bytes_mode = 0;
	sb_u64 n = 10;

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
		if (sb_streq(a, "-n")) {
			if (i + 1 >= argc) {
				sb_die_usage(argv0, "tail [-n N] [-c N] [FILE...]");
			}
			bytes_mode = 0;
			n = tail_parse_n_or_die(argv0, argv[i + 1]);
			i++;
			continue;
		}
		if (sb_streq(a, "-c")) {
			if (i + 1 >= argc) {
				sb_die_usage(argv0, "tail [-n N] [-c N] [FILE...]");
			}
			bytes_mode = 1;
			n = tail_parse_c_or_die(argv0, argv[i + 1]);
			i++;
			continue;
		}
		if (a[1] == 'n' && a[2] != 0) {
			bytes_mode = 0;
			n = tail_parse_n_or_die(argv0, a + 2);
			continue;
		}
		if (a[1] == 'c' && a[2] != 0) {
			bytes_mode = 1;
			n = tail_parse_c_or_die(argv0, a + 2);
			continue;
		}
		sb_die_usage(argv0, "tail [-n N] [-c N] [FILE...]");
	}

	if (i >= argc) {
		if (bytes_mode) return tail_fd_bytes(argv0, 0, n);
		return tail_fd(argv0, 0, n);
	}

	for (; i < argc; i++) {
		const char *path = argv[i] ? argv[i] : "";
		(void)tail_path(argv0, path, bytes_mode, n);
	}
	return 0;
}
