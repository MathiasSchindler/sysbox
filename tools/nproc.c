#include "../src/sb.h"

static int sb_popcount_u8(sb_u8 b) {
	int c = 0;
	while (b) {
		c += (b & 1u) ? 1 : 0;
		b >>= 1;
	}
	return c;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "nproc";
	if (argc > 1) {
		sb_die_usage(argv0, "nproc");
	}

	sb_u8 mask[128];
	for (sb_usize i = 0; i < sizeof(mask); i++) mask[i] = 0;

	sb_i64 r = sb_sys_sched_getaffinity(0, (sb_usize)sizeof(mask), mask);
	if (r < 0) sb_die_errno(argv0, "sched_getaffinity", r);

	int n = 0;
	for (sb_usize i = 0; i < sizeof(mask); i++) {
		n += sb_popcount_u8(mask[i]);
	}
	if (n <= 0) n = 1;

	sb_i64 w = sb_write_i64_dec(1, (sb_i64)n);
	if (w < 0) sb_die_errno(argv0, "write", w);
	w = sb_write_all(1, "\n", 1);
	if (w < 0) sb_die_errno(argv0, "write", w);
	return 0;
}
