#include "../src/sb.h"

static sb_u64 sleep_parse_seconds_or_die(const char *argv0, const char *s) {
	sb_u64 v = 0;
	if (sb_parse_u64_dec(s, &v) != 0) {
		sb_die_usage(argv0, "sleep SECONDS");
	}
	return v;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "sleep";

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
		// Minimal sleep has no flags.
		sb_die_usage(argv0, "sleep SECONDS");
	}

	if (i >= argc || (argc - i) != 1) {
		sb_die_usage(argv0, "sleep SECONDS");
	}

	sb_u64 seconds = sleep_parse_seconds_or_die(argv0, argv[i]);
	if (seconds == 0) {
		return 0;
	}

	struct sb_timespec req;
	struct sb_timespec rem;
	req.tv_sec = (sb_i64)seconds;
	req.tv_nsec = 0;

	for (;;) {
		sb_i64 r = sb_sys_nanosleep(&req, &rem);
		if (r == 0) {
			break;
		}
		if (r < 0 && (sb_u64)(-r) == (sb_u64)SB_EINTR) {
			req = rem;
			continue;
		}
		sb_die_errno(argv0, "nanosleep", r);
	}
	return 0;
}
