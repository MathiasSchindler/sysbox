#include "../src/sb.h"

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;

	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "mv";

	int i = 1;
	if (i < argc && argv[i] && sb_streq(argv[i], "--")) {
		i++;
	}

	if (argc - i != 2) {
		sb_die_usage(argv0, "mv SRC DST");
	}

	const char *src = argv[i] ? argv[i] : "";
	const char *dst = argv[i + 1] ? argv[i + 1] : "";

	sb_i64 r = sb_sys_renameat(SB_AT_FDCWD, src, SB_AT_FDCWD, dst);
	if (r < 0) {
		// Minimal spec: if cross-FS (EXDEV), fail (later: copy+unlink).
		sb_die_errno(argv0, "rename", r);
	}

	return 0;
}
