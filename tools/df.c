#include "../src/sb.h"

static void warn_errno(const char *argv0, const char *path, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, path);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static void write_u64_tab(sb_u64 v) {
	(void)sb_write_u64_dec(1, v);
	(void)sb_write_all(1, "\t", 1);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "df";

	int rc = 0;
	int any = 0;
	for (int i = 1; i < argc; i++) {
		if (argv[i] && !sb_streq(argv[i], "--")) {
			any = 1;
			break;
		}
	}

	int start = 1;
	if (start < argc && argv[start] && sb_streq(argv[start], "--")) {
		start++;
	}

	int count = (any ? (argc - start) : 1);
	for (int j = 0; j < count; j++) {
		const char *path = any ? argv[start + j] : ".";
		if (!path) path = ".";

		struct sb_statfs fs;
		sb_i64 r = sb_sys_statfs(path, &fs);
		if (r < 0) {
			warn_errno(argv0, path, r);
			rc = 1;
			continue;
		}

		sb_u64 bsize = (fs.f_frsize > 0) ? (sb_u64)fs.f_frsize : (sb_u64)fs.f_bsize;
		if (bsize == 0) bsize = 1024;

		sb_u64 total = (fs.f_blocks * bsize) >> 10;
		sb_u64 used = ((fs.f_blocks - fs.f_bfree) * bsize) >> 10;
		sb_u64 avail = (fs.f_bavail * bsize) >> 10;

		// Output: <path>\t<total_k>\t<used_k>\t<avail_k>\n
		(void)sb_write_str(1, path);
		(void)sb_write_all(1, "\t", 1);
		write_u64_tab(total);
		write_u64_tab(used);
		(void)sb_write_u64_dec(1, avail);
		(void)sb_write_all(1, "\n", 1);
	}

	return rc;
}
