#include "../src/sb.h"

static sb_usize rmdir_strlen(const char *s) {
	return sb_strlen(s);
}

static int rmdir_copy_path(char *dst, sb_usize dstsz, const char *src) {
	if (!dst || dstsz == 0) {
		return -1;
	}
	if (!src) {
		src = "";
	}
	sb_usize i = 0;
	for (; src[i] != '\0'; i++) {
		if (i + 1 >= dstsz) {
			return -1;
		}
		dst[i] = src[i];
	}
	dst[i] = '\0';
	return 0;
}

static sb_usize rmdir_trim_trailing_slashes(char *buf) {
	sb_usize len = rmdir_strlen(buf);
	while (len > 1 && buf[len - 1] == '/') {
		len--;
		buf[len] = '\0';
	}
	return len;
}

// Pop one path component in-place. Returns 1 if a parent path remains, 0 if we should stop.
static int rmdir_pop_parent(char *buf) {
	sb_usize len = rmdir_trim_trailing_slashes(buf);
	if (len == 0) {
		return 0;
	}
	if (len == 1 && buf[0] == '/') {
		return 0;
	}

	sb_usize j = len;
	while (j > 0 && buf[j - 1] != '/') {
		j--;
	}
	if (j == 0) {
		return 0;
	}

	sb_usize newlen = j - 1;
	if (newlen == 0) {
		newlen = 1;
	}
	buf[newlen] = '\0';
	(void)rmdir_trim_trailing_slashes(buf);

	if (buf[0] == '/' && buf[1] == '\0') {
		return 0;
	}
	return 1;
}

static sb_i64 rmdir_p(const char *path) {
	char buf[4096];
	if (rmdir_copy_path(buf, sizeof(buf), path) != 0) {
		return -SB_EINVAL;
	}
	(void)rmdir_trim_trailing_slashes(buf);

	sb_i64 r = sb_sys_unlinkat(SB_AT_FDCWD, buf, SB_AT_REMOVEDIR);
	if (r < 0) {
		return r;
	}

	while (rmdir_pop_parent(buf)) {
		r = sb_sys_unlinkat(SB_AT_FDCWD, buf, SB_AT_REMOVEDIR);
		if (r < 0) {
			if (r == -SB_ENOTEMPTY || r == -SB_ENOENT) {
				break;
			}
			return r;
		}
	}
	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;

	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "rmdir";

	int i = 1;
	int parents = 0;
	for (; i < argc && argv[i]; i++) {
		if (sb_streq(argv[i], "--")) {
			i++;
			break;
		}
		if (sb_streq(argv[i], "-p")) {
			parents = 1;
			continue;
		}
		break;
	}

	if (argc - i != 1) {
		sb_die_usage(argv0, "rmdir [-p] DIR");
	}

	const char *path = argv[i] ? argv[i] : "";
	sb_i64 r;
	if (parents) {
		r = rmdir_p(path);
	} else {
		r = sb_sys_unlinkat(SB_AT_FDCWD, path, SB_AT_REMOVEDIR);
	}
	if (r < 0) {
		sb_die_errno(argv0, path, r);
	}
	return 0;
}
