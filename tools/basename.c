#include "../src/sb.h"

static sb_usize basename_trim_trailing_slashes(const char *s, sb_usize n) {
	while (n > 0 && s[n - 1] == '/') {
		n--;
	}
	return n;
}

static int basename_all_slashes(const char *s, sb_usize n) {
	for (sb_usize i = 0; i < n; i++) {
		if (s[i] != '/') {
			return 0;
		}
	}
	return 1;
}

static void basename_write_slice(const char *argv0, const char *s, sb_usize off, sb_usize len) {
	sb_i64 w = sb_write_all(1, s + off, len);
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
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "basename";

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
		sb_die_usage(argv0, "basename PATH [SUFFIX]");
	}

	int rem = argc - i;
	if (!(rem == 1 || rem == 2)) {
		sb_die_usage(argv0, "basename PATH [SUFFIX]");
	}

	const char *path = argv[i];
	const char *suffix = (rem == 2) ? argv[i + 1] : 0;
	if (!path) {
		path = "";
	}

	sb_usize n = sb_strlen(path);
	if (n == 0) {
		basename_write_slice(argv0, ".", 0, 1);
		return 0;
	}

	if (basename_all_slashes(path, n)) {
		basename_write_slice(argv0, "/", 0, 1);
		return 0;
	}

	// Trim trailing slashes.
	n = basename_trim_trailing_slashes(path, n);

	// Find last '/'.
	sb_usize start = 0;
	for (sb_usize j = n; j > 0; j--) {
		if (path[j - 1] == '/') {
			start = j;
			break;
		}
	}

	sb_usize len = n - start;

	// Optional: strip SUFFIX if it matches and is not the entire name.
	if (suffix && *suffix) {
		sb_usize slen = sb_strlen(suffix);
		if (slen < len) {
			int match = 1;
			for (sb_usize k = 0; k < slen; k++) {
				if (path[start + (len - slen) + k] != suffix[k]) {
					match = 0;
					break;
				}
			}
			if (match) {
				len -= slen;
			}
		}
	}

	basename_write_slice(argv0, path, start, len);
	return 0;
}
