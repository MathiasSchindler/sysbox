#include "../src/sb.h"

static int rlf_copy(char *dst, sb_usize dstsz, const char *src) {
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

static int rlf_append(char *dst, sb_usize dstsz, const char *src) {
	sb_usize dlen = sb_strlen(dst);
	if (!src) {
		return 0;
	}
	for (sb_usize i = 0; src[i] != '\0'; i++) {
		if (dlen + 1 >= dstsz) {
			return -1;
		}
		dst[dlen++] = src[i];
	}
	dst[dlen] = '\0';
	return 0;
}

static int rlf_append_n(char *dst, sb_usize dstsz, const char *src, sb_usize n) {
	sb_usize dlen = sb_strlen(dst);
	for (sb_usize i = 0; i < n; i++) {
		if (dlen + 1 >= dstsz) {
			return -1;
		}
		dst[dlen++] = src[i];
	}
	dst[dlen] = '\0';
	return 0;
}

static int rlf_streq_n(const char *s, sb_usize n, const char *lit) {
	for (sb_usize i = 0; i < n; i++) {
		if (lit[i] == '\0') {
			return 0;
		}
		if (s[i] != lit[i]) {
			return 0;
		}
	}
	return (lit[n] == '\0');
}

static void rlf_pop_one_component(char *path) {
	sb_usize len = sb_strlen(path);
	if (len == 0) {
		return;
	}
	if (len == 1 && path[0] == '/') {
		return;
	}
	while (len > 1 && path[len - 1] == '/') {
		path[--len] = '\0';
	}
	while (len > 1 && path[len - 1] != '/') {
		path[--len] = '\0';
	}
	while (len > 1 && path[len - 1] == '/') {
		path[--len] = '\0';
	}
	if (len == 0) {
		path[0] = '/';
		path[1] = '\0';
	}
}

static sb_i64 rlf_make_abs(const char *in, char out[4096]) {
	if (!in) {
		return -SB_EINVAL;
	}
	if (in[0] == '/') {
		if (rlf_copy(out, 4096, in) != 0) {
			return -SB_EINVAL;
		}
		return 0;
	}

	char cwd[4096];
	sb_i64 r = sb_sys_getcwd(cwd, (sb_usize)sizeof(cwd));
	if (r < 0) {
		return r;
	}
	cwd[sizeof(cwd) - 1] = '\0';
	if (rlf_copy(out, 4096, cwd) != 0) {
		return -SB_EINVAL;
	}
	if (!(out[0] == '/' && out[1] == '\0')) {
		if (rlf_append(out, 4096, "/") != 0) {
			return -SB_EINVAL;
		}
	}
	if (rlf_append(out, 4096, in) != 0) {
		return -SB_EINVAL;
	}
	return 0;
}

static sb_i64 rlf_canonicalize(const char *path, char out[4096]) {
	char pending[4096];
	sb_i64 ar = rlf_make_abs(path, pending);
	if (ar < 0) {
		return ar;
	}

	int depth = 0;
	for (;;) {
		char resolved[4096];
		resolved[0] = '/';
		resolved[1] = '\0';

		const char *p = pending;
		while (*p == '/') {
			p++;
		}

		for (;;) {
			while (*p == '/') {
				p++;
			}
			if (*p == '\0') {
				if (rlf_copy(out, 4096, resolved) != 0) {
					return -SB_EINVAL;
				}
				return 0;
			}

			const char *cstart = p;
			while (*p != '\0' && *p != '/') {
				p++;
			}
			sb_usize clen = (sb_usize)(p - cstart);
			if (clen == 0) {
				continue;
			}
			if (rlf_streq_n(cstart, clen, ".")) {
				continue;
			}
			if (rlf_streq_n(cstart, clen, "..")) {
				rlf_pop_one_component(resolved);
				continue;
			}

			char parent[4096];
			if (rlf_copy(parent, 4096, resolved) != 0) {
				return -SB_EINVAL;
			}

			if (!(resolved[0] == '/' && resolved[1] == '\0')) {
				if (rlf_append(resolved, 4096, "/") != 0) {
					return -SB_EINVAL;
				}
			}
			if (rlf_append_n(resolved, 4096, cstart, clen) != 0) {
				return -SB_EINVAL;
			}

			struct sb_stat st;
			sb_i64 sr = sb_sys_newfstatat(SB_AT_FDCWD, resolved, &st, SB_AT_SYMLINK_NOFOLLOW);
			if (sr < 0) {
				return sr;
			}
			if ((st.st_mode & SB_S_IFMT) == SB_S_IFLNK) {
				depth++;
				if (depth > 40) {
					return -SB_ELOOP;
				}

				char linkbuf[4096];
				sb_i64 n = sb_sys_readlinkat(SB_AT_FDCWD, resolved, linkbuf, (sb_usize)(sizeof(linkbuf) - 1u));
				if (n < 0) {
					return n;
				}
				linkbuf[(sb_usize)n] = '\0';

				char next[4096];
				next[0] = '\0';
				if (linkbuf[0] == '/') {
					if (rlf_copy(next, 4096, linkbuf) != 0) {
						return -SB_EINVAL;
					}
				} else {
					if (rlf_copy(next, 4096, parent) != 0) {
						return -SB_EINVAL;
					}
					if (!(next[0] == '/' && next[1] == '\0')) {
						if (rlf_append(next, 4096, "/") != 0) {
							return -SB_EINVAL;
						}
					}
					if (rlf_append(next, 4096, linkbuf) != 0) {
						return -SB_EINVAL;
					}
				}
				// Append the remaining suffix starting at p (may be "" or "/...").
				if (*p != '\0') {
					if (rlf_append(next, 4096, p) != 0) {
						return -SB_EINVAL;
					}
				}

				// Restart with new absolute pending path.
				sb_i64 mr = rlf_make_abs(next, pending);
				if (mr < 0) {
					return mr;
				}
				break;
			}
		}
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "readlink";
	int canonical = 0;

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
		if (sb_streq(a, "-f")) {
			canonical = 1;
			continue;
		}
		sb_die_usage(argv0, "readlink [-f] PATH");
	}

	if ((argc - i) != 1) {
		sb_die_usage(argv0, "readlink [-f] PATH");
	}

	const char *path = argv[i];
	if (canonical) {
		char out[4096];
		sb_i64 rr = rlf_canonicalize(path, out);
		if (rr < 0) {
			sb_die_errno(argv0, "readlink -f", rr);
		}
		sb_i64 w = sb_write_all(1, out, sb_strlen(out));
		if (w < 0) {
			sb_die_errno(argv0, "write", w);
		}
		w = sb_write_all(1, "\n", 1);
		if (w < 0) {
			sb_die_errno(argv0, "write", w);
		}
		return 0;
	}

	char buf[4096];
	sb_i64 n = sb_sys_readlinkat(SB_AT_FDCWD, path, buf, (sb_usize)sizeof(buf));
	if (n < 0) {
		sb_die_errno(argv0, "readlinkat", n);
	}

	// readlink does not NUL-terminate.
	sb_i64 w = sb_write_all(1, buf, (sb_usize)n);
	if (w < 0) {
		sb_die_errno(argv0, "write", w);
	}
	w = sb_write_all(1, "\n", 1);
	if (w < 0) {
		sb_die_errno(argv0, "write", w);
	}
	return 0;
}
