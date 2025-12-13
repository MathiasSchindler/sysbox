#include "../src/sb.h"

static sb_i64 sb_write_cstr_ln(const char *s) {
	sb_i64 r = sb_write_str(1, s);
	if (r < 0) return r;
	return sb_write_all(1, "\n", 1);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "hostname";
	if (argc > 1) {
		sb_die_usage(argv0, "hostname");
	}

	struct sb_utsname u;
	sb_i64 r = sb_sys_uname(&u);
	if (r < 0) sb_die_errno(argv0, "uname", r);

	sb_i64 w = sb_write_cstr_ln(u.nodename);
	if (w < 0) sb_die_errno(argv0, "write", w);
	return 0;
}
