#include "../src/sb.h"

static int ps_is_all_digits(const char *s) {
	if (!s || !*s) return 0;
	for (const char *p = s; *p; p++) {
		if (*p < '0' || *p > '9') return 0;
	}
	return 1;
}

static sb_usize ps_strnlen0(const char *s, sb_usize max) {
	sb_usize n = 0;
	while (n < max && s && s[n]) n++;
	return n;
}

static void ps_write_pid_cmd(sb_u32 pid, const char *cmd, sb_usize cmdlen) {
	(void)sb_write_u64_dec(1, (sb_u64)pid);
	(void)sb_write_all(1, " ", 1);
	(void)sb_write_all(1, cmd, cmdlen);
	(void)sb_write_all(1, "\n", 1);
}

static void ps_parse_and_print_stat(const char *argv0, sb_i32 procfd, const char *pidname) {
	char rel[64];
	sb_usize pn = ps_strnlen0(pidname, 32);
	if (pn == 0 || pn + 5 + 1 > sizeof(rel)) {
		return;
	}
	for (sb_usize i = 0; i < pn; i++) rel[i] = pidname[i];
	rel[pn + 0] = '/';
	rel[pn + 1] = 's';
	rel[pn + 2] = 't';
	rel[pn + 3] = 'a';
	rel[pn + 4] = 't';
	rel[pn + 5] = 0;

	sb_i64 fd = sb_sys_openat(procfd, rel, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) {
		return;
	}

	sb_u8 buf[1024];
	sb_i64 n = sb_sys_read((sb_i32)fd, buf, sizeof(buf) - 1);
	(void)sb_sys_close((sb_i32)fd);
	if (n <= 0) {
		return;
	}
	buf[(sb_u32)n] = 0;

	// Extract comm: second field in /proc/PID/stat, in parentheses. Use first '(' and last ')'.
	sb_u32 l = (sb_u32)n;
	sb_u32 lp = 0xFFFFFFFFu;
	sb_u32 rp = 0xFFFFFFFFu;
	for (sb_u32 i = 0; i < l; i++) {
		if (buf[i] == '(') {
			lp = i;
			break;
		}
	}
	for (sb_u32 i = l; i > 0; i--) {
		if (buf[i - 1] == ')') {
			rp = i - 1;
			break;
		}
	}
	if (lp == 0xFFFFFFFFu || rp == 0xFFFFFFFFu || rp <= lp + 1) {
		return;
	}

	// pid from directory name
	sb_u32 pid = 0;
	if (sb_parse_u32_dec(pidname, &pid) != 0) {
		return;
	}

	const char *cmd = (const char *)&buf[lp + 1];
	sb_usize cmdlen = (sb_usize)(rp - (lp + 1));
	ps_write_pid_cmd(pid, cmd, cmdlen);
	(void)argv0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "ps";

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a || a[0] != '-' || sb_streq(a, "-")) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		sb_die_usage(argv0, "ps");
	}
	if (i != argc) {
		sb_die_usage(argv0, "ps");
	}

	sb_i64 procfd = sb_sys_openat(SB_AT_FDCWD, "/proc", SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY, 0);
	if (procfd < 0) {
		sb_die_errno(argv0, "/proc", procfd);
	}

	(void)sb_write_all(1, "PID CMD\n", 8);

	sb_u8 d_buf[32768];
	for (;;) {
		sb_i64 nread = sb_sys_getdents64((sb_i32)procfd, d_buf, (sb_u32)sizeof(d_buf));
		if (nread < 0) {
			sb_die_errno(argv0, "getdents64", nread);
		}
		if (nread == 0) break;

		sb_u32 bpos = 0;
		while (bpos < (sb_u32)nread) {
			struct sb_linux_dirent64 *d = (struct sb_linux_dirent64 *)(d_buf + bpos);
			const char *name = d->d_name;
			if (ps_is_all_digits(name)) {
				ps_parse_and_print_stat(argv0, (sb_i32)procfd, name);
			}
			bpos += d->d_reclen;
		}
	}

	(void)sb_sys_close((sb_i32)procfd);
	return 0;
}
