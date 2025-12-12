#include "../src/sb.h"

static void write_perm3(sb_u32 perm) {
	char p[3];
	p[0] = (char)('0' + ((perm >> 6) & 7u));
	p[1] = (char)('0' + ((perm >> 3) & 7u));
	p[2] = (char)('0' + (perm & 7u));
	(void)sb_write_all(1, p, 3);
}

static void write_type(sb_u32 mode) {
	sb_u32 t = mode & SB_S_IFMT;
	if (t == SB_S_IFREG) {
		(void)sb_write_str(1, "reg");
		return;
	}
	if (t == SB_S_IFDIR) {
		(void)sb_write_str(1, "dir");
		return;
	}
	(void)sb_write_str(1, "other");
}

static void warn_errno(const char *argv0, const char *path, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, path);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "stat";

	if (argc < 2) {
		sb_die_usage(argv0, "stat FILE...");
	}

	int rc = 0;
	for (int i = 1; i < argc; i++) {
		const char *path = argv[i];
		if (!path || sb_streq(path, "--")) {
			continue;
		}

		sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (fd < 0) {
			warn_errno(argv0, path, fd);
			rc = 1;
			continue;
		}

		struct sb_stat st;
		sb_i64 r = sb_sys_fstat((sb_i32)fd, &st);
		(void)sb_sys_close((sb_i32)fd);
		if (r < 0) {
			warn_errno(argv0, path, r);
			rc = 1;
			continue;
		}

		// Output: <path>: type=<...> perm=<...> uid=<...> gid=<...> size=<...>
		(void)sb_write_str(1, path);
		(void)sb_write_str(1, ": type=");
		write_type(st.st_mode);
		(void)sb_write_str(1, " perm=");
		write_perm3((sb_u32)(st.st_mode & 0777u));
		(void)sb_write_str(1, " uid=");
		(void)sb_write_u64_dec(1, (sb_u64)st.st_uid);
		(void)sb_write_str(1, " gid=");
		(void)sb_write_u64_dec(1, (sb_u64)st.st_gid);
		(void)sb_write_str(1, " size=");
		{
			sb_u64 sz = (st.st_size < 0) ? 0 : (sb_u64)st.st_size;
			(void)sb_write_u64_dec(1, sz);
		}
		(void)sb_write_all(1, "\n", 1);
	}

	return rc;
}
