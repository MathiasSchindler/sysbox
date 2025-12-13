#include "../src/sb.h"

#define CUT_MAX_FIELD 1024u
#define CUT_BITSET_U32S ((CUT_MAX_FIELD + 31u) / 32u)

static void cut_set_field(sb_u32 *bits, sb_u32 field) {
	if (field == 0 || field > CUT_MAX_FIELD) {
		return;
	}
	field -= 1u;
	bits[field / 32u] |= (1u << (field % 32u));
}

static int cut_field_selected(const sb_u32 *bits, sb_u32 field) {
	if (field == 0 || field > CUT_MAX_FIELD) {
		return 0;
	}
	field -= 1u;
	return (bits[field / 32u] >> (field % 32u)) & 1u;
}

static SB_NORETURN void cut_die_usage(const char *argv0) {
	sb_die_usage(argv0, "cut -f LIST [-d DELIM] [FILE...]");
}

static int cut_parse_u32_dec_range(const char *s, sb_u32 *out) {
	sb_u32 v = 0;
	if (sb_parse_u32_dec(s, &v) != 0) {
		return -1;
	}
	*out = v;
	return 0;
}

static void cut_parse_list_or_die(const char *argv0, const char *list, sb_u32 *bits) {
	// Supports: N, N-M, comma-separated.
	const char *p = list;
	if (!p || !*p) {
		cut_die_usage(argv0);
	}

	while (*p) {
		// Parse first number
		const char *a = p;
		while (*p && *p != ',' && *p != '-') p++;
		if (p == a) cut_die_usage(argv0);

		char tmp[32];
		sb_usize n = (sb_usize)(p - a);
		if (n >= sizeof(tmp)) cut_die_usage(argv0);
		for (sb_usize i = 0; i < n; i++) tmp[i] = a[i];
		tmp[n] = 0;

		sb_u32 start = 0;
		if (cut_parse_u32_dec_range(tmp, &start) != 0 || start == 0) {
			cut_die_usage(argv0);
		}

		if (*p == '-') {
			// Range
			p++;
			const char *b = p;
			while (*p && *p != ',') p++;
			if (p == b) cut_die_usage(argv0);

			sb_usize m = (sb_usize)(p - b);
			if (m >= sizeof(tmp)) cut_die_usage(argv0);
			for (sb_usize i = 0; i < m; i++) tmp[i] = b[i];
			tmp[m] = 0;

			sb_u32 end = 0;
			if (cut_parse_u32_dec_range(tmp, &end) != 0 || end == 0 || end < start) {
				cut_die_usage(argv0);
			}
			for (sb_u32 f = start; f <= end && f <= CUT_MAX_FIELD; f++) {
				cut_set_field(bits, f);
			}
		} else {
			cut_set_field(bits, start);
		}

		if (*p == ',') {
			p++;
			if (!*p) cut_die_usage(argv0);
		}
	}
}

struct cut_state {
	const sb_u32 *bits;
	sb_u8 delim;
	sb_u32 field;
	int at_field_start;
	int printing;
	int printed_any;
};

static void cut_state_reset_line(struct cut_state *st) {
	st->field = 1;
	st->at_field_start = 1;
	st->printing = 0;
	st->printed_any = 0;
}

static void cut_maybe_start_field(const char *argv0, struct cut_state *st) {
	if (!st->at_field_start) {
		return;
	}
	st->printing = cut_field_selected(st->bits, st->field);
	if (st->printing && st->printed_any) {
		sb_i64 w = sb_write_all(1, &st->delim, 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
	}
	st->at_field_start = 0;
}

static void cut_feed_byte(const char *argv0, struct cut_state *st, sb_u8 c) {
	if (c == (sb_u8)'\n') {
		sb_i64 w = sb_write_all(1, "\n", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		cut_state_reset_line(st);
		return;
	}
	if (c == st->delim) {
		st->field++;
		st->at_field_start = 1;
		st->printing = 0;
		return;
	}

	cut_maybe_start_field(argv0, st);
	if (st->printing) {
		sb_i64 w = sb_write_all(1, &c, 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		st->printed_any = 1;
	}
}

static int cut_fd(const char *argv0, sb_i32 fd, const sb_u32 *bits, sb_u8 delim) {
	sb_u8 buf[32768];
	struct cut_state st;
	st.bits = bits;
	st.delim = delim;
	cut_state_reset_line(&st);

	for (;;) {
		sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) {
			break;
		}
		for (sb_i64 i = 0; i < r; i++) {
			cut_feed_byte(argv0, &st, buf[i]);
		}
	}
	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "cut";

	sb_u32 bits[CUT_BITSET_U32S];
	for (sb_u32 j = 0; j < CUT_BITSET_U32S; j++) bits[j] = 0;

	const char *list = 0;
	sb_u8 delim = (sb_u8)'\t';

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
		if (sb_streq(a, "-f")) {
			if (i + 1 >= argc) cut_die_usage(argv0);
			list = argv[i + 1];
			i++;
			continue;
		}
		if (sb_streq(a, "-d")) {
			if (i + 1 >= argc || !argv[i + 1]) cut_die_usage(argv0);
			const char *d = argv[i + 1];
			if (!d[0] || d[1]) cut_die_usage(argv0);
			delim = (sb_u8)d[0];
			i++;
			continue;
		}
		// Allow -fLIST
		if (a[1] == 'f' && a[2] != 0) {
			list = a + 2;
			continue;
		}
		// Allow -dX
		if (a[1] == 'd' && a[2] != 0) {
			if (a[3] != 0) cut_die_usage(argv0);
			delim = (sb_u8)a[2];
			continue;
		}
		cut_die_usage(argv0);
	}

	if (!list) {
		cut_die_usage(argv0);
	}
	cut_parse_list_or_die(argv0, list, bits);

	int had_error = 0;
	if (i >= argc) {
		(void)cut_fd(argv0, 0, bits, delim);
		return 0;
	}

	for (; i < argc; i++) {
		const char *path = argv[i];
		if (!path) break;
		if (sb_streq(path, "-")) {
			(void)cut_fd(argv0, 0, bits, delim);
			continue;
		}
		sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (fd < 0) {
			sb_print_errno(argv0, path, fd);
			had_error = 1;
			continue;
		}
		(void)cut_fd(argv0, (sb_i32)fd, bits, delim);
		(void)sb_sys_close((sb_i32)fd);
	}

	return had_error ? 1 : 0;
}
