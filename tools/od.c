#include "../src/sb.h"

#define OD_COLS 16u

static sb_usize od_u64_octal(char *buf, sb_usize cap, sb_u64 v) {
	char tmp[32];
	sb_usize n = 0;
	if (v == 0) {
		if (cap) buf[0] = '0';
		return cap ? 1u : 0u;
	}
	while (v && n < sizeof(tmp)) {
		sb_u64 q = v / 8u;
		sb_u64 r = v - q * 8u;
		tmp[n++] = (char)('0' + (char)r);
		v = q;
	}
	sb_usize out = (n < cap) ? n : cap;
	for (sb_usize i = 0; i < out; i++) buf[i] = tmp[n - 1 - i];
	return n;
}

static void od_write_octal_padded(const char *argv0, sb_u64 v, sb_u32 width) {
	char buf[32];
	sb_usize n = od_u64_octal(buf, sizeof(buf), v);
	if ((sb_u32)n < width) {
		char z = '0';
		for (sb_u32 i = 0; i < width - (sb_u32)n; i++) {
			sb_i64 w = sb_write_all(1, &z, 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
		}
	}
	sb_i64 w = sb_write_all(1, buf, n);
	if (w < 0) sb_die_errno(argv0, "write", w);
}

static void od_write_byte_octal(const char *argv0, sb_u8 b) {
	char out[3];
	out[2] = (char)('0' + (b & 7u));
	out[1] = (char)('0' + ((b >> 3) & 7u));
	out[0] = (char)('0' + ((b >> 6) & 3u));
	sb_i64 w = sb_write_all(1, out, 3);
	if (w < 0) sb_die_errno(argv0, "write", w);
}

static sb_i32 od_open_or_stdin(const char *argv0, const char *path) {
	if (sb_streq(path, "-")) return 0;
	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) sb_die_errno(argv0, path, fd);
	return (sb_i32)fd;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "od";

	int opt_no_addr = 0;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] != '-' || sb_streq(a, "-")) break;
		if (sb_streq(a, "-An") || (sb_streq(a, "-A") && (i + 1 < argc) && sb_streq(argv[i + 1], "n"))) {
			opt_no_addr = 1;
			if (sb_streq(a, "-A")) i++;
			continue;
		}
		sb_die_usage(argv0, "od [-An] [FILE...]");
	}

	int nfiles = argc - i;
	const char *paths[32];
	if (nfiles <= 0) {
		paths[0] = "-";
		nfiles = 1;
	} else {
		if (nfiles > 32) sb_die_usage(argv0, "od [-An] [FILE...]");
		for (int k = 0; k < nfiles; k++) paths[k] = argv[i + k];
	}

	sb_u8 row[OD_COLS];
	sb_u32 row_n = 0;
	sb_u64 off = 0;

	for (int f = 0; f < nfiles; f++) {
		sb_i32 fd = od_open_or_stdin(argv0, paths[f]);
		sb_u8 buf[4096];
		for (;;) {
			sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
			if (r < 0) sb_die_errno(argv0, "read", r);
			if (r == 0) break;
			for (sb_i64 j = 0; j < r; j++) {
				row[row_n++] = buf[j];
				off++;
				if (row_n == OD_COLS) {
					if (!opt_no_addr) {
						od_write_octal_padded(argv0, off - OD_COLS, 7);
					}
					for (sb_u32 k = 0; k < row_n; k++) {
						char sp = ' ';
						sb_i64 w = sb_write_all(1, &sp, 1);
						if (w < 0) sb_die_errno(argv0, "write", w);
						od_write_byte_octal(argv0, row[k]);
					}
					char nl = '\n';
					sb_i64 w = sb_write_all(1, &nl, 1);
					if (w < 0) sb_die_errno(argv0, "write", w);
					row_n = 0;
				}
			}
		}
		if (fd != 0) (void)sb_sys_close(fd);
	}

	if (row_n) {
		if (!opt_no_addr) {
			od_write_octal_padded(argv0, off - (sb_u64)row_n, 7);
		}
		for (sb_u32 k = 0; k < row_n; k++) {
			char sp = ' ';
			sb_i64 w = sb_write_all(1, &sp, 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
			od_write_byte_octal(argv0, row[k]);
		}
		char nl = '\n';
		sb_i64 w = sb_write_all(1, &nl, 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
	}

	if (!opt_no_addr) {
		od_write_octal_padded(argv0, off, 7);
		char nl = '\n';
		sb_i64 w = sb_write_all(1, &nl, 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
	}
	return 0;
}
