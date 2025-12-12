#include "../src/sb.h"

static void ignore_sigpipe_best_effort(void) {
	struct sb_sigaction sa;
	sa.sa_handler = (void (*)(int))1; // SIG_IGN
	sa.sa_flags = 0;
	sa.sa_restorer = 0;
	sa.sa_mask[0] = 0;
	(void)sb_sys_rt_sigaction(SB_SIGPIPE, &sa, 0, 8);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "yes";

	ignore_sigpipe_best_effort();

	char buf[4096];
	sb_usize n = 0;

	int ai = 1;
	if (ai < argc && argv[ai] && sb_streq(argv[ai], "--")) {
		ai++;
	}

	if (ai >= argc) {
		buf[0] = 'y';
		n = 1;
	} else {
		for (int i = ai; i < argc; i++) {
			const char *s = argv[i] ? argv[i] : "";
			if (i != ai) {
				if (n + 1 >= sizeof(buf)) {
					sb_die_usage(argv0, "yes [STRING...]");
				}
				buf[n++] = ' ';
			}
			for (const char *p = s; *p; p++) {
				if (n + 1 >= sizeof(buf)) {
					sb_die_usage(argv0, "yes [STRING...]");
				}
				buf[n++] = *p;
			}
		}
	}

	if (n + 1 > sizeof(buf)) {
		sb_die_usage(argv0, "yes [STRING...]");
	}
	buf[n++] = '\n';

	for (;;) {
		sb_usize off = 0;
		while (off < n) {
			sb_i64 r = sb_sys_write(1, buf + off, n - off);
			if (r < 0) {
				if (r == -(sb_i64)SB_EPIPE) {
					return 0;
				}
				sb_die_errno(argv0, "write", r);
			}
			if (r == 0) {
				return 0;
			}
			off += (sb_usize)r;
		}
	}
}
