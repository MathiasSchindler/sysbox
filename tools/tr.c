#include "../src/sb.h"

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "tr";

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
		// Minimal tr has no flags.
		sb_die_usage(argv0, "tr SET1 SET2");
	}

	if (i + 2 != argc) {
		sb_die_usage(argv0, "tr SET1 SET2");
	}

	const char *set1 = argv[i];
	const char *set2 = argv[i + 1];

	sb_u32 len1 = (sb_u32)sb_strlen(set1);
	sb_u32 len2 = (sb_u32)sb_strlen(set2);
	if (len1 == 0 || len2 == 0 || len1 != len2) {
		sb_die_usage(argv0, "tr SET1 SET2");
	}

	sb_u8 map[256];
	for (sb_u32 k = 0; k < 256; k++) {
		map[k] = (sb_u8)k;
	}

	for (sb_u32 k = 0; k < len1; k++) {
		map[(sb_u8)set1[k]] = (sb_u8)set2[k];
	}

	sb_u8 buf[32768];
	for (;;) {
		sb_i64 r = sb_sys_read(0, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) {
			break;
		}
		for (sb_i64 j = 0; j < r; j++) {
			buf[j] = map[buf[j]];
		}
		sb_i64 w = sb_write_all(1, buf, (sb_usize)r);
		if (w < 0) {
			sb_die_errno(argv0, "write", w);
		}
	}
	return 0;
}
