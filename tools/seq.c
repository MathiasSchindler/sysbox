#include "../src/sb.h"

static int parse_i64_or_die(const char *argv0, const char *s, sb_i64 *out) {
	if (sb_parse_i64_dec(s, out) != 0) {
		sb_die_usage(argv0, "seq LAST | seq FIRST LAST | seq FIRST INCR LAST");
	}
	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "seq";

	// Allow optional leading "--".
	int ai = 1;
	if (ai < argc && argv[ai] && sb_streq(argv[ai], "--")) {
		ai++;
	}

	int n = argc - ai;
	if (n < 1 || n > 3) {
		sb_die_usage(argv0, "seq LAST | seq FIRST LAST | seq FIRST INCR LAST");
	}

	sb_i64 first = 1;
	sb_i64 incr = 1;
	sb_i64 last = 0;

	if (n == 1) {
		parse_i64_or_die(argv0, argv[ai + 0], &last);
	} else if (n == 2) {
		parse_i64_or_die(argv0, argv[ai + 0], &first);
		parse_i64_or_die(argv0, argv[ai + 1], &last);
	} else {
		parse_i64_or_die(argv0, argv[ai + 0], &first);
		parse_i64_or_die(argv0, argv[ai + 1], &incr);
		parse_i64_or_die(argv0, argv[ai + 2], &last);
	}

	if (incr == 0) {
		sb_die_usage(argv0, "seq LAST | seq FIRST LAST | seq FIRST INCR LAST");
	}

	int rc = 0;
	sb_i64 v = first;
	if (incr > 0) {
		while (v <= last) {
			sb_i64 w = sb_write_i64_dec(1, v);
			if (w < 0) sb_die_errno(argv0, "write", w);
			w = sb_write_all(1, "\n", 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
			// Advance (best-effort overflow handling)
			if (v > (sb_i64)0x7FFFFFFFFFFFFFFFULL - incr) {
				break;
			}
			v += incr;
		}
		return rc;
	}

	while (v >= last) {
		sb_i64 w = sb_write_i64_dec(1, v);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, "\n", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		if (v < (sb_i64)0x8000000000000000ULL - incr) {
			break;
		}
		v += incr;
	}
	return rc;
}
