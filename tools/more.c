#include "../src/sb.h"

#define MORE_LINES_PER_PAGE 24

struct r {
	sb_i32 fd;
	sb_u8 buf[4096];
	sb_usize off;
	sb_usize len;
};

static sb_i32 r_fill(struct r *r) {
	r->off = 0;
	sb_i64 n = sb_sys_read(r->fd, r->buf, sizeof(r->buf));
	if (n < 0) return (sb_i32)n;
	r->len = (sb_usize)n;
	return (sb_i32)n;
}

static int r_getc(struct r *r, sb_u8 *out) {
	if (r->off >= r->len) {
		sb_i32 fr = r_fill(r);
		if (fr < 0) return -1;
		if (fr == 0) return 0;
	}
	*out = r->buf[r->off++];
	return 1;
}

static void more_prompt_clear(void) {
	(void)sb_write_str(2, "\r\033[K");
}

static int more_prompt(sb_i32 ttyfd) {
	// Returns: 0 continue, 1 quit.
	(void)sb_write_str(2, "--More--");
	sb_u8 c = 0;
	sb_i64 n = sb_sys_read(ttyfd, &c, 1);
	more_prompt_clear();
	if (n <= 0) return 0;
	if (c == 'q' || c == 'Q') return 1;
	return 0;
}

static int more_cat_fd(const char *argv0, sb_i32 in_fd, sb_i32 ttyfd) {
	(void)argv0;
	struct r rr = { .fd = in_fd, .off = 0, .len = 0 };
	sb_u32 lines = 0;

	while (1) {
		sb_u8 c = 0;
		int rc = r_getc(&rr, &c);
		if (rc < 0) sb_die_errno(argv0, "read", (sb_i64)-SB_EINVAL);
		if (rc == 0) break;

		(void)sb_write_all(1, &c, 1);
		if (c == (sb_u8)'\n') {
			lines++;
			if (ttyfd >= 0 && lines >= MORE_LINES_PER_PAGE) {
				if (more_prompt(ttyfd)) return 1;
				lines = 0;
			}
		}
	}
	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "more";

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] == '-') sb_die_usage(argv0, "more [FILE...]");
		break;
	}

	// Use /dev/tty for prompts when available.
	sb_i32 ttyfd = -1;
	sb_i64 tfd = sb_sys_openat(SB_AT_FDCWD, "/dev/tty", SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (tfd >= 0) ttyfd = (sb_i32)tfd;

	if (i >= argc) {
		// stdin
		int quit = more_cat_fd(argv0, 0, ttyfd);
		if (ttyfd >= 0) (void)sb_sys_close(ttyfd);
		return quit ? 0 : 0;
	}

	for (; i < argc; i++) {
		const char *path = argv[i];
		if (!path) continue;
		sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (fd < 0) sb_die_errno(argv0, path, fd);
		int quit = more_cat_fd(argv0, (sb_i32)fd, ttyfd);
		(void)sb_sys_close((sb_i32)fd);
		if (quit) break;
	}

	if (ttyfd >= 0) (void)sb_sys_close(ttyfd);
	return 0;
}
