#include "../src/sb.h"

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)argv;
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "whoami";

	// Minimal sysbox behavior: print numeric uid (no /etc/passwd parsing).
	sb_i64 uid = sb_sys_getuid();
	if (uid < 0) {
		sb_die_errno(argv0, "getuid", uid);
	}
	if (sb_write_u64_dec(1, (sb_u64)uid) < 0) sb_die_errno(argv0, "write", -1);
	if (sb_write_all(1, "\n", 1) < 0) sb_die_errno(argv0, "write", -1);
	return 0;
}
