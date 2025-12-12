#include "../src/sb.h"

static sb_usize dirname_trim_trailing_slashes(const char *s, sb_usize n) {
	while (n > 0 && s[n - 1] == '/') {
		n--;
	}
	return n;
}

static int dirname_all_slashes(const char *s, sb_usize n) {
	for (sb_usize i = 0; i < n; i++) {
		if (s[i] != '/') {
			return 0;
		}
	}
	return 1;
}

static void dirname_write_slice(const char *argv0, const char *s, sb_usize len) {
	sb_i64 w = sb_write_all(1, s, len);
	if (w < 0) {
		sb_die_errno(argv0, "write", w);
	}
	w = sb_write_all(1, "\n", 1);
	if (w < 0) {
		sb_die_errno(argv0, "write", w);
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "dirname";

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
		sb_die_usage(argv0, "dirname PATH");
	}

	if ((argc - i) != 1) {
		sb_die_usage(argv0, "dirname PATH");
	}

	const char *path = argv[i];
	if (!path) {
		path = "";
	}

	sb_usize n = sb_strlen(path);
	if (n == 0) {
		dirname_write_slice(argv0, ".", 1);
		return 0;
	}

	if (dirname_all_slashes(path, n)) {
		dirname_write_slice(argv0, "/", 1);
		return 0;
	}

	// Trim trailing slashes.
	n = dirname_trim_trailing_slashes(path, n);

	// Find last '/'.
	sb_usize slash = (sb_usize)-1;
	for (sb_usize j = n; j > 0; j--) {
		if (path[j - 1] == '/') {
			slash = j - 1;
			break;
		}
	}

	if (slash == (sb_usize)-1) {
		// No slash.
		dirname_write_slice(argv0, ".", 1);
		return 0;
	}

	// Strip trailing slashes from the directory part.
	sb_usize dirlen = slash;
	while (dirlen > 0 && path[dirlen - 1] == '/') {
		dirlen--;
	}

	if (dirlen == 0) {
		// Root.
		dirname_write_slice(argv0, "/", 1);
		return 0;
	}

	dirname_write_slice(argv0, path, dirlen);
	return 0;
}
