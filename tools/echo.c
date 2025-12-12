#include "../src/sb.h"

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;

	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "echo";

	int newline = 1;
	int i = 1;
	int first = 1;

	if (i < argc && argv[i] && sb_streq(argv[i], "--")) {
		i++;
	} else if (i < argc && argv[i] && sb_streq(argv[i], "-n")) {
		newline = 0;
		i++;
		if (i < argc && argv[i] && sb_streq(argv[i], "--")) {
			i++;
		}
	}

	for (; i < argc; i++) {
		if (!first) {
			sb_i64 rspace = sb_write_all(1, " ", 1);
			if (rspace < 0) {
				sb_die_errno(argv0, "write", rspace);
			}
		}
		const char *s = argv[i] ? argv[i] : "";
		sb_i64 r = sb_write_all(1, s, sb_strlen(s));
		if (r < 0) {
			sb_die_errno(argv0, "write", r);
		}
		first = 0;
	}

	if (newline) {
		sb_i64 rnl = sb_write_all(1, "\n", 1);
		if (rnl < 0) {
			sb_die_errno(argv0, "write", rnl);
		}
	}

	return 0;
}
