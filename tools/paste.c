#include "../src/sb.h"

#define PASTE_MAX_FILES 32
#define PASTE_LINE_CAP 4096

struct paste_lr {
	sb_i32 fd;
	sb_u8 buf[4096];
	sb_u32 pos;
	sb_u32 len;
	int eof;
};

static SB_NORETURN void paste_die(const char *argv0, const char *msg) {
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, msg);
	(void)sb_write_str(2, "\n");
	sb_exit(1);
}

static int paste_lr_read_line(const char *argv0, struct paste_lr *lr, char *out, sb_u32 cap, sb_u32 *out_len, int *out_had_nl) {
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
			paste_die(argv0, "line too long");
		}
		out[(*out_len)++] = (char)c;
	}
}

static sb_i32 paste_open_or_stdin(const char *argv0, const char *path) {
	if (sb_streq(path, "-")) return 0;
	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) sb_die_errno(argv0, path, fd);
	return (sb_i32)fd;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "paste";

	int opt_serial = 0;
	const char *delims = "\t";
	sb_u32 deln = 1;

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
		if (sb_streq(a, "-s")) {
			opt_serial = 1;
			continue;
		}
		if (sb_streq(a, "-d")) {
			if (i + 1 >= argc) sb_die_usage(argv0, "paste [-s] [-d LIST] [FILE...] ");
			delims = argv[++i];
			deln = (sb_u32)sb_strlen(delims);
			if (deln == 0) sb_die_usage(argv0, "paste [-s] [-d LIST] [FILE...]");
			continue;
		}
		if (a[1] == 'd' && a[2]) {
			delims = a + 2;
			deln = (sb_u32)sb_strlen(delims);
			if (deln == 0) sb_die_usage(argv0, "paste [-s] [-d LIST] [FILE...]");
			continue;
		}
		sb_die_usage(argv0, "paste [-s] [-d LIST] [FILE...]");
	}

	int nfiles = argc - i;
	const char *paths[PASTE_MAX_FILES];
	if (nfiles <= 0) {
		paths[0] = "-";
		nfiles = 1;
	} else {
		if (nfiles > PASTE_MAX_FILES) sb_die_usage(argv0, "paste [-s] [-d LIST] [FILE...]");
		for (int k = 0; k < nfiles; k++) paths[k] = argv[i + k];
	}

	int stdin_count = 0;
	for (int k = 0; k < nfiles; k++) {
		if (sb_streq(paths[k], "-")) stdin_count++;
	}
	if (stdin_count > 1) {
		// Multiple '-' are ambiguous for this minimal implementation.
		sb_die_usage(argv0, "paste [-s] [-d LIST] [FILE...]");
	}

	if (opt_serial) {
		char line[PASTE_LINE_CAP];
		for (int k = 0; k < nfiles; k++) {
			sb_i32 fd = paste_open_or_stdin(argv0, paths[k]);
			struct paste_lr lr = {0};
			lr.fd = fd;

			sb_u32 lineno = 0;
			for (;;) {
				sb_u32 n = 0;
				int had_nl = 0;
				int ok = paste_lr_read_line(argv0, &lr, line, (sb_u32)sizeof(line), &n, &had_nl);
				if (!ok) break;
				if (lineno > 0) {
					char d = delims[(lineno - 1u) % deln];
					sb_i64 w = sb_write_all(1, &d, 1);
					if (w < 0) sb_die_errno(argv0, "write", w);
				}
				if (n) {
					sb_i64 w = sb_write_all(1, line, (sb_usize)n);
					if (w < 0) sb_die_errno(argv0, "write", w);
				}
				lineno++;
			}
			{
				char nl = '\n';
				sb_i64 w = sb_write_all(1, &nl, 1);
				if (w < 0) sb_die_errno(argv0, "write", w);
			}
			if (fd != 0) (void)sb_sys_close(fd);
		}
		return 0;
	}

	// Parallel mode.
	struct paste_lr lrs[PASTE_MAX_FILES];
	for (int k = 0; k < nfiles; k++) {
		lrs[k].fd = paste_open_or_stdin(argv0, paths[k]);
		lrs[k].pos = 0;
		lrs[k].len = 0;
		lrs[k].eof = 0;
	}

	static char lines[PASTE_MAX_FILES][PASTE_LINE_CAP];
	sb_u32 lens[PASTE_MAX_FILES];
	int had_nl[PASTE_MAX_FILES];

	for (;;) {
		int any = 0;
		for (int k = 0; k < nfiles; k++) {
			lens[k] = 0;
			had_nl[k] = 0;
			int ok = paste_lr_read_line(argv0, &lrs[k], lines[k], (sb_u32)sizeof(lines[k]), &lens[k], &had_nl[k]);
			if (ok) any = 1;
		}
		if (!any) break;

		for (int k = 0; k < nfiles; k++) {
			if (k > 0) {
				char d = delims[(sb_u32)(k - 1) % deln];
				sb_i64 w = sb_write_all(1, &d, 1);
				if (w < 0) sb_die_errno(argv0, "write", w);
			}
			if (lens[k]) {
				sb_i64 w = sb_write_all(1, lines[k], (sb_usize)lens[k]);
				if (w < 0) sb_die_errno(argv0, "write", w);
			}
		}
		{
			char nl = '\n';
			sb_i64 w = sb_write_all(1, &nl, 1);
			if (w < 0) sb_die_errno(argv0, "write", w);
		}
	}

	for (int k = 0; k < nfiles; k++) {
		if (lrs[k].fd != 0) (void)sb_sys_close(lrs[k].fd);
	}
	return 0;
}
