#include "../src/sb.h"

#define GREP_READ_BUF_SIZE 32768u
#define GREP_LINE_MAX 65536u

struct grep_reader {
	sb_i32 fd;
	sb_u8 buf[GREP_READ_BUF_SIZE];
	sb_u32 pos;
	sb_u32 len;
	int eof;
};

static SB_NORETURN void grep_die_msg(const char *argv0, const char *msg) {
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, msg);
	(void)sb_write_str(2, "\n");
	sb_exit(1);
}

static int grep_is_upper_ascii(sb_u8 c) {
	return (c >= (sb_u8)'A' && c <= (sb_u8)'Z');
}

static sb_u8 grep_tolower_ascii(sb_u8 c) {
	if (grep_is_upper_ascii(c)) {
		return (sb_u8)(c + (sb_u8)('a' - 'A'));
	}
	return c;
}

static int grep_fill(struct grep_reader *r, const char *argv0) {
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
static int grep_read_line(struct grep_reader *r, const char *argv0, sb_u8 *out, sb_u32 *out_len) {
	sb_u32 n = 0;
	int have_any = 0;

	for (;;) {
		if (r->pos >= r->len) {
			if (!grep_fill(r, argv0)) {
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
			if (n >= GREP_LINE_MAX) {
				grep_die_msg(argv0, "line too long");
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

static int grep_match_at(const sb_u8 *hay, sb_u32 hlen, const sb_u8 *needle, sb_u32 nlen, sb_u32 pos, int insensitive) {
	if (pos + nlen > hlen) {
		return 0;
	}
	for (sb_u32 i = 0; i < nlen; i++) {
		sb_u8 hc = hay[pos + i];
		sb_u8 nc = needle[i];
		if (insensitive) {
			hc = grep_tolower_ascii(hc);
			nc = grep_tolower_ascii(nc);
		}
		if (hc != nc) {
			return 0;
		}
	}
	return 1;
}

static int grep_line_matches(const sb_u8 *line, sb_u32 line_len, const sb_u8 *pat, sb_u32 pat_len, int insensitive) {
	if (pat_len == 0) {
		return 1;
	}
	if (pat_len > line_len) {
		return 0;
	}
	for (sb_u32 i = 0; i + pat_len <= line_len; i++) {
		if (grep_match_at(line, line_len, pat, pat_len, i, insensitive)) {
			return 1;
		}
	}
	return 0;
}


static int grep_line_matches_regex(const char *pattern, sb_u8 *line, sb_u32 line_len, sb_u32 flags) {
	// The line buffer is guaranteed to have room for a trailing NUL.
	if (line_len >= GREP_LINE_MAX) return 0;
	line[line_len] = 0;
	const char *ms = 0;
	const char *me = 0;
	int r = sb_regex_match_first(pattern, (const char *)line, flags, &ms, &me, 0);
	if (r < 0) return -1;
	return r;
}

static sb_i64 grep_write_line(const sb_u8 *line, sb_u32 line_len, int with_lineno, sb_u64 lineno) {
	sb_i64 w;
	if (with_lineno) {
		w = sb_write_u64_dec(1, lineno);
		if (w < 0) return w;
		w = sb_write_all(1, ":", 1);
		if (w < 0) return w;
	}
	w = sb_write_all(1, line, (sb_usize)line_len);
	if (w < 0) return w;
	w = sb_write_all(1, "\n", 1);
	return w;
}

static int grep_fd(const char *argv0, sb_i32 fd, const char *pattern, const sb_u8 *pat, sb_u32 pat_len, int use_fixed, int opt_i, int opt_v, int opt_c, int opt_n, int opt_q, sb_u64 *io_count, int *io_matched) {
	struct grep_reader r;
	r.fd = fd;
	r.pos = 0;
	r.len = 0;
	r.eof = 0;

	sb_u8 line[GREP_LINE_MAX];
	sb_u32 line_len = 0;
	sb_u64 lineno = 0;

	for (;;) {
		int ok = grep_read_line(&r, argv0, line, &line_len);
		if (!ok) {
			break;
		}
		lineno++;

		int m;
		if (use_fixed) {
			m = grep_line_matches(line, line_len, pat, pat_len, opt_i);
		} else {
			sb_u32 flags = opt_i ? SB_REGEX_ICASE : 0u;
			m = grep_line_matches_regex(pattern, line, line_len, flags);
			if (m < 0) {
				sb_die_usage(argv0, "grep [-i] [-v] [-c] [-n] [-q] [-F] PATTERN [FILE...] (invalid regex)");
			}
		}
		if (opt_v) {
			m = !m;
		}
		if (!m) {
			continue;
		}

		*io_matched = 1;
		(*io_count)++;

		if (opt_q) {
			return 0;
		}
		if (!opt_c) {
			sb_i64 w = grep_write_line(line, line_len, opt_n, lineno);
			if (w < 0) {
				sb_die_errno(argv0, "write", w);
			}
		}
	}

	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "grep";

	int opt_i = 0;
	int opt_v = 0;
	int opt_c = 0;
	int opt_n = 0;
	int opt_q = 0;
	int opt_F = 0;

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
		if (sb_streq(a, "-i")) {
			opt_i = 1;
			continue;
		}
		if (sb_streq(a, "-v")) {
			opt_v = 1;
			continue;
		}
		if (sb_streq(a, "-c")) {
			opt_c = 1;
			continue;
		}
		if (sb_streq(a, "-n")) {
			opt_n = 1;
			continue;
		}
		if (sb_streq(a, "-q")) {
			opt_q = 1;
			continue;
		}
		if (sb_streq(a, "-F")) {
			opt_F = 1;
			continue;
		}
		sb_die_usage(argv0, "grep [-i] [-v] [-c] [-n] [-q] [-F] PATTERN [FILE...]");
	}

	if (i >= argc || !argv[i]) {
		sb_die_usage(argv0, "grep [-i] [-v] [-c] [-n] [-q] [-F] PATTERN [FILE...]");
	}
	const char *pattern = argv[i++];

	sb_u8 pat[GREP_LINE_MAX];
	sb_usize pat_len_usz = sb_strlen(pattern);
	if (pat_len_usz >= (sb_usize)GREP_LINE_MAX) {
		sb_die_usage(argv0, "grep [-i] [-v] [-c] [-n] [-q] [-F] PATTERN [FILE...]");
	}
	sb_u32 pat_len = (sb_u32)pat_len_usz;
	for (sb_u32 k = 0; k < pat_len; k++) {
		pat[k] = (sb_u8)pattern[k];
	}

	if (!opt_F) {
		// Validate the pattern once up front.
		const char *ms = 0;
		const char *me = 0;
		int r = sb_regex_match_first(pattern, "", opt_i ? SB_REGEX_ICASE : 0u, &ms, &me, 0);
		if (r < 0) {
			sb_die_usage(argv0, "grep [-i] [-v] [-c] [-n] [-q] [-F] PATTERN [FILE...] (invalid regex)");
		}
	}

	int matched = 0;
	sb_u64 count = 0;

	if (i >= argc) {
		(void)grep_fd(argv0, 0, pattern, pat, pat_len, opt_F, opt_i, opt_v, opt_c, opt_n, opt_q, &count, &matched);
	} else {
		for (; i < argc; i++) {
			const char *path = argv[i];
			if (!path) break;
			if (sb_streq(path, "-")) {
				(void)grep_fd(argv0, 0, pattern, pat, pat_len, opt_F, opt_i, opt_v, opt_c, opt_n, opt_q, &count, &matched);
				if (opt_q && matched) {
					return 0;
				}
				continue;
			}
			sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
			if (fd < 0) {
				sb_print_errno(argv0, path, fd);
				continue;
			}
			(void)grep_fd(argv0, (sb_i32)fd, pattern, pat, pat_len, opt_F, opt_i, opt_v, opt_c, opt_n, opt_q, &count, &matched);
			(void)sb_sys_close((sb_i32)fd);
			if (opt_q && matched) {
				return 0;
			}
		}
	}

	if (opt_c && !opt_q) {
		sb_i64 w = sb_write_u64_dec(1, count);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, "\n", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
	}

	if (matched) {
		return 0;
	}
	// No matches (or operational errors). Either way, exit 1.
	return 1;
}
