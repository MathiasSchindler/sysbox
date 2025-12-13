#include "../src/sb.h"

static int parse_meminfo_u64(const char *buf, const char *key, sb_u64 *out) {
	// Match lines like: "Key: <num> kB"
	if (!buf || !key || !out) return 0;
	sb_usize klen = sb_strlen(key);
	for (const char *p = buf; *p;) {
		const char *line = p;
		while (*p && *p != '\n') p++;
		sb_usize linelen = (sb_usize)(p - line);
		if (*p == '\n') p++;

		if (linelen < klen) continue;
		int ok = 1;
		for (sb_usize i = 0; i < klen; i++) {
			if (line[i] != key[i]) {
				ok = 0;
				break;
			}
		}
		if (!ok) continue;

		const char *q = line + klen;
		while (*q && (*q == ' ' || *q == '\t')) q++;
		sb_u64 v = 0;
		int any = 0;
		while (*q >= '0' && *q <= '9') {
			any = 1;
			sb_u64 d = (sb_u64)(*q - '0');
			v = v * 10u + d;
			q++;
		}
		if (!any) continue;
		*out = v;
		return 1;
	}
	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "free";

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] == '-') sb_die_usage(argv0, "free");
		break;
	}
	if (i != argc) sb_die_usage(argv0, "free");

	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, "/proc/meminfo", SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) sb_die_errno(argv0, "/proc/meminfo", fd);

	char buf[32768];
	sb_i64 n = sb_sys_read((sb_i32)fd, buf, sizeof(buf) - 1);
	(void)sb_sys_close((sb_i32)fd);
	if (n < 0) sb_die_errno(argv0, "read", n);
	buf[(sb_usize)n] = 0;

	sb_u64 mem_total = 0, mem_free = 0, mem_avail = 0, buffers = 0, cached = 0;
	sb_u64 swap_total = 0, swap_free = 0;
	(void)parse_meminfo_u64(buf, "MemTotal:", &mem_total);
	(void)parse_meminfo_u64(buf, "MemFree:", &mem_free);
	(void)parse_meminfo_u64(buf, "MemAvailable:", &mem_avail);
	(void)parse_meminfo_u64(buf, "Buffers:", &buffers);
	(void)parse_meminfo_u64(buf, "Cached:", &cached);
	(void)parse_meminfo_u64(buf, "SwapTotal:", &swap_total);
	(void)parse_meminfo_u64(buf, "SwapFree:", &swap_free);

	(void)sb_write_str(1, "mem\t");
	(void)sb_write_u64_dec(1, mem_total);
	(void)sb_write_all(1, "\t", 1);
	(void)sb_write_u64_dec(1, mem_free);
	(void)sb_write_all(1, "\t", 1);
	(void)sb_write_u64_dec(1, mem_avail);
	(void)sb_write_all(1, "\t", 1);
	(void)sb_write_u64_dec(1, buffers);
	(void)sb_write_all(1, "\t", 1);
	(void)sb_write_u64_dec(1, cached);
	(void)sb_write_all(1, "\n", 1);

	(void)sb_write_str(1, "swap\t");
	(void)sb_write_u64_dec(1, swap_total);
	(void)sb_write_all(1, "\t", 1);
	(void)sb_write_u64_dec(1, swap_free);
	(void)sb_write_all(1, "\n", 1);

	return 0;
}
