#include "../src/sb.h"

static int sb_ends_with(const char *s, const char *suffix) {
	if (!s || !suffix) {
		return 0;
	}
	sb_usize slen = sb_strlen(s);
	sb_usize tlen = sb_strlen(suffix);
	if (tlen > slen) {
		return 0;
	}
	const char *p = s + (slen - tlen);
	return sb_streq(p, suffix);
}

static int test_file_exists(const char *path) {
	struct sb_stat st;
	sb_i64 r = sb_sys_newfstatat(SB_AT_FDCWD, path, &st, 0);
	return (r >= 0);
}

static int test_file_kind(const char *path, sb_u32 kind) {
	struct sb_stat st;
	sb_i64 r = sb_sys_newfstatat(SB_AT_FDCWD, path, &st, 0);
	if (r < 0) {
		return 0;
	}
	return ((st.st_mode & SB_S_IFMT) == kind);
}

static int test_access(const char *path, sb_i32 mode) {
	sb_i64 r = sb_sys_faccessat(SB_AT_FDCWD, path, mode, 0);
	return (r >= 0);
}

static int eval_unary(const char *op, const char *arg, const char *argv0, int bracket_mode) {
	(void)argv0;
	(void)bracket_mode;
	if (sb_streq(op, "-e")) {
		return test_file_exists(arg);
	}
	if (sb_streq(op, "-f")) {
		return test_file_kind(arg, SB_S_IFREG);
	}
	if (sb_streq(op, "-d")) {
		return test_file_kind(arg, SB_S_IFDIR);
	}
	if (sb_streq(op, "-r")) {
		return test_access(arg, SB_R_OK);
	}
	if (sb_streq(op, "-w")) {
		return test_access(arg, SB_W_OK);
	}
	if (sb_streq(op, "-x")) {
		return test_access(arg, SB_X_OK);
	}
	if (sb_streq(op, "-z")) {
		return sb_strlen(arg) == 0;
	}
	if (sb_streq(op, "-n")) {
		return sb_strlen(arg) != 0;
	}
	return -1;
}

static int eval_binary(const char *a, const char *op, const char *b, const char *argv0, int bracket_mode) {
	(void)argv0;
	(void)bracket_mode;
	if (sb_streq(op, "=")) {
		return sb_streq(a, b);
	}
	if (sb_streq(op, "!=")) {
		return !sb_streq(a, b);
	}

	// Integer comparisons
	if (sb_streq(op, "-eq") || sb_streq(op, "-ne") || sb_streq(op, "-lt") || sb_streq(op, "-le") || sb_streq(op, "-gt") || sb_streq(op, "-ge")) {
		sb_i64 ia = 0;
		sb_i64 ib = 0;
		if (sb_parse_i64_dec(a, &ia) != 0 || sb_parse_i64_dec(b, &ib) != 0) {
			return -2; // invalid integer
		}
		if (sb_streq(op, "-eq")) return ia == ib;
		if (sb_streq(op, "-ne")) return ia != ib;
		if (sb_streq(op, "-lt")) return ia < ib;
		if (sb_streq(op, "-le")) return ia <= ib;
		if (sb_streq(op, "-gt")) return ia > ib;
		if (sb_streq(op, "-ge")) return ia >= ib;
	}
	return -1;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "test";
	int bracket_mode = sb_ends_with(argv0, "/[") || sb_streq(argv0, "[") || (sb_strlen(argv0) > 0 && argv0[sb_strlen(argv0) - 1] == '[');

	int i = 1;
	if (bracket_mode) {
		if (argc < 2) {
			sb_die_usage(argv0, "[ EXPRESSION ]");
		}
		if (!argv[argc - 1] || !sb_streq(argv[argc - 1], "]")) {
			sb_die_usage(argv0, "[ EXPRESSION ]");
		}
		argc--; // drop closing ']'
	}

	int n = argc - i;
	if (n <= 0) {
		// No expression: false (matches POSIX `test`).
		return 1;
	}

	if (n == 1) {
		const char *s = argv[i] ? argv[i] : "";
		return (sb_strlen(s) != 0) ? 0 : 1;
	}

	if (n == 2) {
		const char *op = argv[i] ? argv[i] : "";
		const char *arg = argv[i + 1] ? argv[i + 1] : "";
		int r = eval_unary(op, arg, argv0, bracket_mode);
		if (r < 0) {
			if (bracket_mode) sb_die_usage(argv0, "[ EXPRESSION ]");
			sb_die_usage(argv0, "test EXPRESSION");
		}
		return r ? 0 : 1;
	}

	if (n == 3) {
		const char *a = argv[i] ? argv[i] : "";
		const char *op = argv[i + 1] ? argv[i + 1] : "";
		const char *b = argv[i + 2] ? argv[i + 2] : "";
		int r = eval_binary(a, op, b, argv0, bracket_mode);
		if (r == -2) {
			// Invalid integer.
			if (bracket_mode) sb_die_usage(argv0, "[ EXPRESSION ]");
			sb_die_usage(argv0, "test EXPRESSION");
		}
		if (r < 0) {
			if (bracket_mode) sb_die_usage(argv0, "[ EXPRESSION ]");
			sb_die_usage(argv0, "test EXPRESSION");
		}
		return r ? 0 : 1;
	}

	if (bracket_mode) sb_die_usage(argv0, "[ EXPRESSION ]");
	sb_die_usage(argv0, "test EXPRESSION");
}
