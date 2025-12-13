#include "../src/sb.h"

#define NL_LINE_CAP 4096

struct nl_lr {
	sb_i32 fd;
	sb_u8 buf[4096];
	sb_u32 pos;
	sb_u32 len;
	int eof;
};

static int nl_lr_read_line(const char *argv0, struct nl_lr *lr, char *out, sb_u32 cap, sb_u32 *out_len, int *out_had_nl) {
	if (!lr || !out || cap == 0 || !out_len || !out_had_nl) return 0;
	*out_len = 0;
	*out_had_nl = 0;

	for (;;) {
		if (lr->eof) {
			return (*out_len > 0) ? 1 : 0;
		}
		if (lr->pos == lr->len) {
			sb_i64 r = sb_sys_read(lr->fd, lr->buf, (sb_usize)sizeof(lr->buf));
			if (r < 0) sb_die_errno(argv0, "read", r);
			if (r == 0) {
				lr->eof = 1;
				return (*out_len > 0) ? 1 : 0;
			}
			lr->pos = 0;
			lr->len = (sb_u32)r;
		}

		sb_u8 c = lr->buf[lr->pos++];
		if (c == (sb_u8)'\n') {
			*out_had_nl = 1;
			return 1;
		}
		if (*out_len + 1 >= cap) {
			(void)sb_write_str(2, argv0);
			(void)sb_write_str(2, ": line too long\n");
			sb_exit(1);
		}
		out[(*out_len)++] = (char)c;
	}
}

static sb_usize nl_u64_dec(char *buf, sb_usize cap, sb_u64 v) {
	char tmp[32];
	sb_usize n = 0;
	if (v == 0) {
		if (cap) buf[0] = '0';
		return cap ? 1u : 0u;
	}
	while (v && n < sizeof(tmp)) {
		sb_u64 q = v / 10u;
		sb_u64 r = v - q * 10u;
		tmp[n++] = (char)('0' + (char)r);
		v = q;
	}
	sb_usize out = (n < cap) ? n : cap;
	for (sb_usize i = 0; i < out; i++) buf[i] = tmp[n - 1 - i];
	return n;
}

static void nl_write_pad(const char *argv0, char ch, sb_u32 n) {
	char buf[64];
	for (sb_u32 i = 0; i < (sb_u32)sizeof(buf); i++) buf[i] = ch;
	while (n) {
		sb_u32 chunk = n;
		if (chunk > (sb_u32)sizeof(buf)) chunk = (sb_u32)sizeof(buf);
		sb_i64 w = sb_write_all(1, buf, (sb_usize)chunk);
		if (w < 0) sb_die_errno(argv0, "write", w);
		n -= chunk;
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "nl";

	int number_all = 0; // default: number non-empty only
	sb_u32 width = 6;
	const char *sep = "\t";

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] != '-' || sb_streq(a, "-")) break;

		if (sb_streq(a, "-ba")) {
			number_all = 1;
			continue;
		}
		if (sb_streq(a, "-bt")) {
			number_all = 0;
			continue;
		}
		if (sb_streq(a, "-w")) {
			if (i + 1 >= argc) sb_die_usage(argv0, "nl [-ba|-bt] [-w WIDTH] [-s SEP] [FILE]");
			if (sb_parse_u32_dec(argv[++i], &width) != 0 || width == 0) {
				sb_die_usage(argv0, "nl [-ba|-bt] [-w WIDTH] [-s SEP] [FILE]");
			}
			continue;
		}
		if (a[1] == 'w' && a[2]) {
			if (sb_parse_u32_dec(a + 2, &width) != 0 || width == 0) {
				sb_die_usage(argv0, "nl [-ba|-bt] [-w WIDTH] [-s SEP] [FILE]");
			}
			continue;
		}
		if (sb_streq(a, "-s")) {
			if (i + 1 >= argc) sb_die_usage(argv0, "nl [-ba|-bt] [-w WIDTH] [-s SEP] [FILE]");
			sep = argv[++i];
			continue;
		}
		if (a[1] == 's' && a[2]) {
			sep = a + 2;
			continue;
		}

		sb_die_usage(argv0, "nl [-ba|-bt] [-w WIDTH] [-s SEP] [FILE]");
	}

	const char *path = 0;
	int npos = argc - i;
	if (npos == 0) {
		path = "-";
	} else if (npos == 1) {
		path = argv[i];
	} else {
		sb_die_usage(argv0, "nl [-ba|-bt] [-w WIDTH] [-s SEP] [FILE]");
	}

	sb_i32 fd = 0;
	if (!sb_streq(path, "-")) {
		sb_i64 rfd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (rfd < 0) sb_die_errno(argv0, path, rfd);
		fd = (sb_i32)rfd;
	}

	struct nl_lr lr = {0};
	lr.fd = fd;

	char line[NL_LINE_CAP];
	sb_u64 n = 1;
	for (;;) {
		sb_u32 len = 0;
		int had_nl = 0;
		int ok = nl_lr_read_line(argv0, &lr, line, (sb_u32)sizeof(line), &len, &had_nl);
		if (!ok) break;

		int is_blank = (len == 0);
		if (!number_all && is_blank) {
			if (had_nl) {
				char c = '\n';
				sb_i64 w = sb_write_all(1, &c, 1);
				if (w < 0) sb_die_errno(argv0, "write", w);
			}
			continue;
		}

		char numbuf[32];
		sb_usize nd = nl_u64_dec(numbuf, sizeof(numbuf), n);
		if ((sb_u32)nd < width) nl_write_pad(argv0, ' ', width - (sb_u32)nd);
		sb_i64 w = sb_write_all(1, numbuf, nd);
		if (w < 0) sb_die_errno(argv0, "write", w);

		w = sb_write_str(1, sep);
		if (w < 0) sb_die_errno(argv0, "write", w);

		if (len) {
			w = sb_write_all(1, line, (sb_usize)len);
			if (w < 0) sb_die_errno(argv0, "write", w);
		}
		if (had_nl) {
			char c = '\n';
			w = sb_write_all(1, &c, 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
		}

		n++;
	}

	if (fd != 0) (void)sb_sys_close(fd);
	return 0;
}
