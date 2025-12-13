#include "../src/sb.h"

static void sleep_parse_timespec_or_die(const char *argv0, const char *s, struct sb_timespec *out) {
	if (!s || !*s || !out) {
		sb_die_usage(argv0, "sleep SECONDS");
	}

	sb_u64 sec = 0;
	sb_u32 nsec = 0;
	int saw_digit = 0;
	int saw_dot = 0;
	sb_u32 frac_digits = 0;

	for (const char *p = s; *p; p++) {
		char c = *p;
		if (c >= '0' && c <= '9') {
			saw_digit = 1;
			if (!saw_dot) {
				sb_u64 d = (sb_u64)(c - '0');
				if (sec > (~(sb_u64)0) / 10u) sb_die_usage(argv0, "sleep SECONDS");
				sec = sec * 10u + d;
			} else {
				if (frac_digits < 9u) {
					nsec = nsec * 10u + (sb_u32)(c - '0');
					frac_digits++;
				}
			}
			continue;
		}
		if (c == '.' && !saw_dot) {
			saw_dot = 1;
			continue;
		}
		// Unknown character.
		sb_die_usage(argv0, "sleep SECONDS");
	}

	if (!saw_digit) {
		sb_die_usage(argv0, "sleep SECONDS");
	}

	// Scale fractional part to nanoseconds.
	while (frac_digits < 9u) {
		nsec *= 10u;
		frac_digits++;
	}

	if (sec > 0x7FFFFFFFFFFFFFFFULL) {
		sb_die_usage(argv0, "sleep SECONDS");
	}
	out->tv_sec = (sb_i64)sec;
	out->tv_nsec = (sb_i64)nsec;
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

	struct sb_timespec req;
	struct sb_timespec rem;
	sleep_parse_timespec_or_die(argv0, argv[i], &req);
	if (req.tv_sec == 0 && req.tv_nsec == 0) return 0;

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
