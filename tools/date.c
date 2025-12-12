#include "../src/sb.h"

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "date";

	// Minimal: no flags/args, print epoch seconds.
	if (argc > 1) {
		// Allow "--" with nothing after it.
		if (!(argc == 2 && argv[1] && sb_streq(argv[1], "--"))) {
			sb_die_usage(argv0, "date");
		}
	}

	struct sb_timespec ts;
	sb_i64 r = sb_sys_clock_gettime(SB_CLOCK_REALTIME, &ts);
	if (r < 0) {
		sb_die_errno(argv0, "clock_gettime", r);
	}

	sb_u64 sec = (ts.tv_sec < 0) ? 0 : (sb_u64)ts.tv_sec;
	sb_i64 w = sb_write_u64_dec(1, sec);
	if (w < 0) {
		sb_die_errno(argv0, "write", w);
	}
	w = sb_write_all(1, "\n", 1);
	if (w < 0) {
		sb_die_errno(argv0, "write", w);
	}
	return 0;
}
