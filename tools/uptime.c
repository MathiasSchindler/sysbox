#include "../src/sb.h"

static sb_usize tok_len(const char *s) {
	sb_usize n = 0;
	while (s && s[n] && s[n] != ' ' && s[n] != '\t' && s[n] != '\n' && s[n] != '\r') n++;
	return n;
}

static int read_small_file(const char *argv0, const char *path, char *buf, sb_usize cap, sb_usize *out_n) {
	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) {
		sb_die_errno(argv0, path, fd);
	}
	sb_i64 n = sb_sys_read((sb_i32)fd, buf, cap - 1);
	(void)sb_sys_close((sb_i32)fd);
	if (n < 0) {
		sb_die_errno(argv0, "read", n);
	}
	buf[(sb_usize)n] = 0;
	*out_n = (sb_usize)n;
	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "uptime";

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] == '-') {
			sb_die_usage(argv0, "uptime");
		}
		break;
	}
	if (i != argc) {
		sb_die_usage(argv0, "uptime");
	}

	char up[256];
	sb_usize upn = 0;
	read_small_file(argv0, "/proc/uptime", up, sizeof(up), &upn);
	(void)upn;

	char la[256];
	sb_usize lan = 0;
	read_small_file(argv0, "/proc/loadavg", la, sizeof(la), &lan);
	(void)lan;

	// /proc/uptime: "<uptime> <idle>"
	// /proc/loadavg: "<l1> <l5> <l15> ..."
	const char *upt = up;
	sb_usize uptl = tok_len(upt);

	const char *p = la;
	const char *l1 = p;
	sb_usize l1l = tok_len(l1);
	p += l1l;
	while (*p == ' ' || *p == '\t') p++;
	const char *l5 = p;
	sb_usize l5l = tok_len(l5);
	p += l5l;
	while (*p == ' ' || *p == '\t') p++;
	const char *l15 = p;
	sb_usize l15l = tok_len(l15);

	(void)sb_write_str(1, "up ");
	(void)sb_write_all(1, upt, uptl);
	(void)sb_write_str(1, " load ");
	(void)sb_write_all(1, l1, l1l);
	(void)sb_write_all(1, " ", 1);
	(void)sb_write_all(1, l5, l5l);
	(void)sb_write_all(1, " ", 1);
	(void)sb_write_all(1, l15, l15l);
	(void)sb_write_all(1, "\n", 1);
	return 0;
}
