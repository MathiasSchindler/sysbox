#include "../src/sb.h"

#define ID_MAX_GROUPS 256

static void id_print_errno(const char *argv0, const char *ctx, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, ctx);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static sb_i64 id_write_kv_u64(const char *k, sb_u64 v) {
	sb_i64 w = sb_write_str(1, k);
	if (w < 0) return w;
	w = sb_write_u64_dec(1, v);
	return w;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "id";

	int opt_u = 0;
	int opt_g = 0;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] != '-') {
			break;
		}
		if (sb_streq(a, "-u")) {
			opt_u = 1;
			continue;
		}
		if (sb_streq(a, "-g")) {
			opt_g = 1;
			continue;
		}
		sb_die_usage(argv0, "id [-u|-g]");
	}

	if (i != argc) {
		sb_die_usage(argv0, "id [-u|-g]");
	}
	if (opt_u && opt_g) {
		sb_die_usage(argv0, "id [-u|-g]");
	}

	sb_i64 uid = sb_sys_getuid();
	sb_i64 gid = sb_sys_getgid();
	if (uid < 0) {
		id_print_errno(argv0, "getuid", uid);
		return 1;
	}
	if (gid < 0) {
		id_print_errno(argv0, "getgid", gid);
		return 1;
	}

	if (opt_u) {
		sb_i64 w = sb_write_i64_dec(1, uid);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, "\n", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		return 0;
	}
	if (opt_g) {
		sb_i64 w = sb_write_i64_dec(1, gid);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_all(1, "\n", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		return 0;
	}

	sb_i64 ng = sb_sys_getgroups(0, (sb_u32 *)0);
	if (ng < 0) {
		id_print_errno(argv0, "getgroups", ng);
		return 1;
	}
	if (ng > ID_MAX_GROUPS) {
		(void)sb_write_str(2, argv0);
		(void)sb_write_str(2, ": too many groups\n");
		return 1;
	}

	sb_u32 groups[ID_MAX_GROUPS];
	for (sb_i64 k = 0; k < ng; k++) {
		groups[(sb_u32)k] = 0;
	}
	if (ng > 0) {
		sb_i64 r = sb_sys_getgroups((sb_i32)ng, groups);
		if (r < 0) {
			id_print_errno(argv0, "getgroups", r);
			return 1;
		}
	}

	sb_i64 w = id_write_kv_u64("uid=", (sb_u64)uid);
	if (w < 0) sb_die_errno(argv0, "write", w);
	w = sb_write_all(1, " ", 1);
	if (w < 0) sb_die_errno(argv0, "write", w);
	w = id_write_kv_u64("gid=", (sb_u64)gid);
	if (w < 0) sb_die_errno(argv0, "write", w);
	w = sb_write_all(1, " ", 1);
	if (w < 0) sb_die_errno(argv0, "write", w);
	w = sb_write_str(1, "groups=");
	if (w < 0) sb_die_errno(argv0, "write", w);

	// Match common `id -G` ordering: primary gid first, then supplementary.
	w = sb_write_u64_dec(1, (sb_u64)gid);
	if (w < 0) sb_die_errno(argv0, "write", w);

	for (sb_i64 k = 0; k < ng; k++) {
		sb_u32 gk = groups[(sb_u32)k];
		if ((sb_u64)gk == (sb_u64)gid) {
			continue;
		}
		w = sb_write_all(1, ",", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
		w = sb_write_u64_dec(1, (sb_u64)gk);
		if (w < 0) sb_die_errno(argv0, "write", w);
	}

	w = sb_write_all(1, "\n", 1);
	if (w < 0) sb_die_errno(argv0, "write", w);
	return 0;
}
