#include "../src/sb.h"

static void chown_print_err(const char *argv0, const char *path, sb_i64 err_neg) {
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
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "chown";

	int i = 1;
	if (i < argc && argv[i] && sb_streq(argv[i], "--")) {
		i++;
	}

	if (argc - i < 2) {
		sb_die_usage(argv0, "chown UID[:GID] FILE...");
	}

	sb_u32 uid, gid;
	if (sb_parse_uid_gid(argv[i], &uid, &gid) != 0) {
		sb_die_usage(argv0, "chown UID[:GID] FILE...");
	}
	i++;

	int any_fail = 0;
	for (; i < argc; i++) {
		const char *path = argv[i] ? argv[i] : "";
		sb_i64 r = sb_sys_fchownat(SB_AT_FDCWD, path, uid, gid, 0);
		if (r < 0) {
			chown_print_err(argv0, path, r);
			any_fail = 1;
		}
	}

	return any_fail ? 1 : 0;
}
