#include "../src/sb.h"

#define REV_LINE_MAX 65536

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

static void rev_fd(const char *argv0, sb_i32 fd) {
	sb_u8 line[REV_LINE_MAX];
	sb_usize n = 0;
	struct r rr = { .fd = fd, .off = 0, .len = 0 };

	while (1) {
		sb_u8 c = 0;
		int rc = r_getc(&rr, &c);
		if (rc < 0) sb_die_errno(argv0, "read", (sb_i64)-SB_EINVAL);
		if (rc == 0) {
			// flush last partial line
			for (sb_usize i = 0; i < n / 2; i++) {
				sb_u8 t = line[i];
				line[i] = line[n - 1 - i];
				line[n - 1 - i] = t;
			}
			if (n) (void)sb_write_all(1, line, n);
			break;
		}
		if (c == '\n') {
			for (sb_usize i = 0; i < n / 2; i++) {
				sb_u8 t = line[i];
				line[i] = line[n - 1 - i];
				line[n - 1 - i] = t;
			}
			if (n) (void)sb_write_all(1, line, n);
			(void)sb_write_all(1, "\n", 1);
			n = 0;
			continue;
		}
		if (n + 1 >= sizeof(line)) {
			sb_die_errno(argv0, "line too long", (sb_i64)-SB_EINVAL);
		}
		line[n++] = c;
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "rev";

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] == '-') sb_die_usage(argv0, "rev [FILE...]");
		break;
	}

	if (i >= argc) {
		rev_fd(argv0, 0);
		return 0;
	}

	for (; i < argc; i++) {
		const char *path = argv[i];
		if (!path) continue;
		sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (fd < 0) sb_die_errno(argv0, path, fd);
		rev_fd(argv0, (sb_i32)fd);
		(void)sb_sys_close((sb_i32)fd);
	}
	return 0;
}
