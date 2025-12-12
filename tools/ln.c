#include "../src/sb.h"

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "ln";
	int symbolic = 0;
	int force = 0;

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
		if (a[1] && a[1] != '-' && a[2]) {
			// Combined short options: -sf
			for (sb_u32 j = 1; a[j]; j++) {
				if (a[j] == 's') symbolic = 1;
				else if (a[j] == 'f') force = 1;
				else sb_die_usage(argv0, "ln [-s] [-f] [--] SRC DST");
			}
			continue;
		}
		if (sb_streq(a, "-s")) {
			symbolic = 1;
			continue;
		}
		if (sb_streq(a, "-f")) {
			force = 1;
			continue;
		}
		sb_die_usage(argv0, "ln [-s] [-f] [--] SRC DST");
	}

	if ((argc - i) != 2) {
		sb_die_usage(argv0, "ln [-s] [-f] [--] SRC DST");
	}

	const char *src = argv[i];
	const char *dst = argv[i + 1];
	if (force) {
		sb_i64 ur = sb_sys_unlinkat(SB_AT_FDCWD, dst, 0);
		if (ur < 0 && (sb_u64)(-ur) != (sb_u64)SB_ENOENT) {
			sb_die_errno(argv0, "unlinkat", ur);
		}
	}

	sb_i64 r;
	if (symbolic) {
		r = sb_sys_symlinkat(src, SB_AT_FDCWD, dst);
	} else {
		r = sb_sys_linkat(SB_AT_FDCWD, src, SB_AT_FDCWD, dst, 0);
	}
	if (r < 0) {
		sb_die_errno(argv0, symbolic ? "symlinkat" : "linkat", r);
	}
	return 0;
}
