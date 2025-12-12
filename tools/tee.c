#include "../src/sb.h"

#define TEE_MAX_OUT 32

static void tee_print_errno(const char *argv0, const char *ctx, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, ctx);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static sb_i64 tee_open_out(const char *path, int append) {
	sb_i32 flags = SB_O_WRONLY | SB_O_CREAT | SB_O_CLOEXEC;
	if (append) {
		flags |= SB_O_APPEND;
	} else {
		flags |= SB_O_TRUNC;
	}
	return sb_sys_openat(SB_AT_FDCWD, path, flags, 0666);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "tee";
	int append = 0;

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
			// Combined short options.
			for (sb_u32 j = 1; a[j]; j++) {
				if (a[j] == 'a') append = 1;
				else sb_die_usage(argv0, "tee [-a] [FILE...]");
			}
			continue;
		}
		if (sb_streq(a, "-a")) {
			append = 1;
			continue;
		}
		sb_die_usage(argv0, "tee [-a] [FILE...]");
	}

	sb_i32 out_fds[TEE_MAX_OUT];
	const char *out_names[TEE_MAX_OUT];
	int out_n = 0;
	int had_error = 0;

	// stdout is always output 0.
	out_fds[out_n] = 1;
	out_names[out_n] = "write";
	out_n++;

	for (; i < argc; i++) {
		const char *path = argv[i];
		if (!path) break;
		if (sb_streq(path, "-")) {
			// Treat '-' as stdout.
			continue;
		}
		if (out_n >= TEE_MAX_OUT) {
			sb_die_usage(argv0, "tee [-a] [FILE...]");
		}
		sb_i64 fd = tee_open_out(path, append);
		if (fd < 0) {
			tee_print_errno(argv0, path, fd);
			had_error = 1;
			continue;
		}
		out_fds[out_n] = (sb_i32)fd;
		out_names[out_n] = path;
		out_n++;
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

		for (int oi = 0; oi < out_n; oi++) {
			sb_i32 fd = out_fds[oi];
			if (fd < 0) {
				continue;
			}
			sb_i64 w = sb_write_all(fd, buf, (sb_usize)r);
			if (w < 0) {
				if (fd == 1) {
					sb_die_errno(argv0, "write", w);
				}
				tee_print_errno(argv0, out_names[oi], w);
				had_error = 1;
				(void)sb_sys_close(fd);
				out_fds[oi] = -1;
			}
		}
	}

	for (int oi = 1; oi < out_n; oi++) {
		if (out_fds[oi] >= 0) {
			(void)sb_sys_close(out_fds[oi]);
		}
	}

	return had_error ? 1 : 0;
}
