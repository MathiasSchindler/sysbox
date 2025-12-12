#include "../src/sb.h"

static void write_hex_u64_noprefix(sb_u64 v) {
	char buf[16];
	sb_usize n = 0;
	if (v == 0) {
		(void)sb_write_all(1, "0", 1);
		return;
	}
	while (v) {
		sb_u8 d = (sb_u8)(v & 0xF);
		buf[n++] = (d < 10) ? (char)('0' + d) : (char)('a' + (d - 10));
		v >>= 4;
	}
	for (sb_usize i = 0; i < n / 2; i++) {
		char t = buf[i];
		buf[i] = buf[n - 1 - i];
		buf[n - 1 - i] = t;
	}
	(void)sb_write_all(1, buf, n);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "printf";

	if (argc < 2) {
		sb_die_usage(argv0, "printf FORMAT [ARG...]");
	}

	int ai = 2;
	const char *fmt = argv[1] ? argv[1] : "";

	for (const char *p = fmt; *p; p++) {
		char c = *p;
		if (c == '\\') {
			p++;
			char e = *p;
			if (!e) {
				char b = '\\';
				sb_i64 w = sb_write_all(1, &b, 1);
				if (w < 0) sb_die_errno(argv0, "write", w);
				break;
			}
			char out = e;
			if (e == 'n') out = '\n';
			else if (e == 't') out = '\t';
			else if (e == '\\') out = '\\';
			sb_i64 w = sb_write_all(1, &out, 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
			continue;
		}
		if (c != '%') {
			sb_i64 w = sb_write_all(1, &c, 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
			continue;
		}

		p++;
		char f = *p;
		if (!f) {
			char pc = '%';
			sb_i64 w = sb_write_all(1, &pc, 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
			break;
		}

		if (f == '%') {
			char pc = '%';
			sb_i64 w = sb_write_all(1, &pc, 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
			continue;
		}

		const char *arg = (ai < argc && argv[ai]) ? argv[ai] : "";
		if (ai < argc) ai++;

		if (f == 's') {
			sb_i64 w = sb_write_str(1, arg);
			if (w < 0) sb_die_errno(argv0, "write", w);
			continue;
		}
		if (f == 'c') {
			char out = arg[0] ? arg[0] : 0;
			sb_i64 w = sb_write_all(1, &out, 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
			continue;
		}
		if (f == 'd') {
			sb_i64 v = 0;
			if (sb_parse_i64_dec(arg, &v) != 0) {
				sb_die_usage(argv0, "printf FORMAT [ARG...]");
			}
			sb_i64 w = sb_write_i64_dec(1, v);
			if (w < 0) sb_die_errno(argv0, "write", w);
			continue;
		}
		if (f == 'u') {
			sb_u64 v = 0;
			if (sb_parse_u64_dec(arg, &v) != 0) {
				sb_die_usage(argv0, "printf FORMAT [ARG...]");
			}
			sb_i64 w = sb_write_u64_dec(1, v);
			if (w < 0) sb_die_errno(argv0, "write", w);
			continue;
		}
		if (f == 'x') {
			sb_u64 v = 0;
			if (sb_parse_u64_dec(arg, &v) != 0) {
				sb_die_usage(argv0, "printf FORMAT [ARG...]");
			}
			write_hex_u64_noprefix(v);
			continue;
		}

		sb_die_usage(argv0, "printf FORMAT [ARG...]");
	}

	return 0;
}
