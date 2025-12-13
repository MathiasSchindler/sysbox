#include "../src/sb.h"

static sb_usize sb_strnlen(const char *s, sb_usize max) {
	sb_usize n = 0;
	while (n < max && s && s[n]) {
		n++;
	}
	return n;
}

static void mkdir_p(const char *argv0, const char *path, sb_u32 mode) {
	if (!path || !*path) {
		sb_die_usage(argv0, "mkdir [-p] [-m MODE] DIR");
	}

	// Keep this small and deterministic; refuse extremely long paths.
	char cur[4096];
	const sb_usize path_len = sb_strnlen(path, sizeof(cur));
	if (path_len == sizeof(cur)) {
		sb_die_errno(argv0, path, (sb_i64)-SB_EINVAL);
	}

	// Special-case paths consisting only of slashes: mkdir -p / => success.
	int any_non_slash = 0;
	for (sb_usize k = 0; k < path_len; k++) {
		if (path[k] != '/') {
			any_non_slash = 1;
			break;
		}
	}
	if (!any_non_slash) {
		return;
	}

	sb_usize cur_len = 0;
	sb_usize i = 0;
	if (path[0] == '/') {
		cur[cur_len++] = '/';
		i = 1;
		while (i < path_len && path[i] == '/') {
			i++;
		}
	}

	while (i < path_len) {
		// Parse next component.
		sb_usize start = i;
		while (i < path_len && path[i] != '/') {
			i++;
		}
		sb_usize comp_len = i - start;

		while (i < path_len && path[i] == '/') {
			i++;
		}

		if (comp_len == 0) {
			continue;
		}

		if (cur_len > 0 && cur[cur_len - 1] != '/') {
			if (cur_len + 1 >= sizeof(cur)) {
				sb_die_errno(argv0, path, (sb_i64)-SB_EINVAL);
			}
			cur[cur_len++] = '/';
		}
		if (cur_len + comp_len + 1 > sizeof(cur)) {
			sb_die_errno(argv0, path, (sb_i64)-SB_EINVAL);
		}

		for (sb_usize k = 0; k < comp_len; k++) {
			cur[cur_len + k] = path[start + k];
		}
		cur_len += comp_len;
		cur[cur_len] = 0;

		sb_i64 r = sb_sys_mkdirat(SB_AT_FDCWD, cur, mode);
		if (r < 0) {
			sb_u64 e = (sb_u64)(-r);
			if (e == (sb_u64)SB_EEXIST) {
				// Confirm the existing path is a directory.
				sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, cur, SB_O_RDONLY | SB_O_CLOEXEC | SB_O_DIRECTORY, 0);
				if (fd < 0) {
					sb_die_errno(argv0, cur, fd);
				}
				(void)sb_sys_close((sb_i32)fd);
			} else {
				sb_die_errno(argv0, cur, r);
			}
		}
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;

	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "mkdir";
	int parents = 0;
	sb_u32 mode = 0777;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a || a[0] != '-' || sb_streq(a, "-")) {
			break;
		}
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (sb_streq(a, "-p")) {
			parents = 1;
			continue;
		}
		if (sb_streq(a, "-m")) {
			if (i + 1 >= argc) {
				sb_die_usage(argv0, "mkdir [-p] [-m MODE] DIR");
			}
			const char *m = argv[++i];
			if (sb_parse_u32_octal(m, &mode) != 0 || mode > 07777u) {
				sb_die_usage(argv0, "mkdir [-p] [-m MODE] DIR");
			}
			continue;
		}
		sb_die_usage(argv0, "mkdir [-p] [-m MODE] DIR");
	}

	if (argc - i != 1) {
		sb_die_usage(argv0, "mkdir [-p] [-m MODE] DIR");
	}

	const char *path = argv[i] ? argv[i] : "";
	if (parents) {
		mkdir_p(argv0, path, mode);
		return 0;
	}

	sb_i64 r = sb_sys_mkdirat(SB_AT_FDCWD, path, mode);
	if (r < 0) sb_die_errno(argv0, path, r);
	return 0;
}
