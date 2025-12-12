#include "../src/sb.h"

static sb_i64 sb_write_cstr_ln(const char *s) {
	sb_i64 r = sb_write_str(1, s);
	if (r < 0) return r;
	return sb_write_all(1, "\n", 1);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "uname";

	int mode = 0; // 0 sysname, 1 machine, 2 all
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!a) continue;
		if (sb_streq(a, "--")) {
			// Ignore end-of-options marker; no operands.
			if (i != argc - 1) {
				sb_die_usage(argv0, "uname [-s|-m|-a]");
			}
			break;
		}
		if (sb_streq(a, "-s")) {
			mode = 0;
			continue;
		}
		if (sb_streq(a, "-m")) {
			mode = 1;
			continue;
		}
		if (sb_streq(a, "-a")) {
			mode = 2;
			continue;
		}
		sb_die_usage(argv0, "uname [-s|-m|-a]");
	}

	struct sb_utsname u;
	sb_i64 r = sb_sys_uname(&u);
	if (r < 0) {
		sb_die_errno(argv0, "uname", r);
	}

	if (mode == 2) {
		// sysname nodename release version machine
		sb_i64 w;
		w = sb_write_str(1, u.sysname);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, " ", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_str(1, u.nodename);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, " ", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_str(1, u.release);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, " ", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_str(1, u.version);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, " ", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_str(1, u.machine);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, "\n", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		return 0;
	}

	if (mode == 1) {
		sb_i64 w = sb_write_cstr_ln(u.machine);
		if (w < 0) sb_die_errno(argv0, "write", w);
		return 0;
	}

	{
		sb_i64 w = sb_write_cstr_ln(u.sysname);
		if (w < 0) sb_die_errno(argv0, "write", w);
		return 0;
	}
}
