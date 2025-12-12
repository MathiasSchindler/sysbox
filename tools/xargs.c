#include "../src/sb.h"

static int starts_with(const char *s, const char *pre) {
	while (*pre) {
		if (*s != *pre) return 0;
		s++;
		pre++;
	}
	return 1;
}

static int has_slash(const char *s) {
	for (const char *p = s; *p; p++) {
		if (*p == '/') return 1;
	}
	return 0;
}

static const char *getenv_kv(char **envp, const char *key_eq) {
	if (!envp) return 0;
	for (sb_usize i = 0; envp[i]; i++) {
		const char *e = envp[i];
		if (!e) continue;
		if (starts_with(e, key_eq)) {
			return e + sb_strlen(key_eq);
		}
	}
	return 0;
}

static int find_executable(char **envp, const char *cmd, char *out_path, sb_usize out_sz) {
	if (!cmd || !*cmd) return 0;
	if (has_slash(cmd)) {
		// Use directly.
		sb_usize n = sb_strlen(cmd);
		if (n + 1 > out_sz) return 0;
		for (sb_usize i = 0; i < n; i++) out_path[i] = cmd[i];
		out_path[n] = 0;
		return 1;
	}

	const char *path_env = getenv_kv(envp, "PATH=");
	if (!path_env) return 0;

	char cand[4096];
	const char *p = path_env;
	while (1) {
		const char *seg = p;
		while (*p && *p != ':') {
			p++;
		}
		sb_usize seg_len = (sb_usize)(p - seg);
		sb_usize cmd_len = sb_strlen(cmd);

		// Empty segment means current directory.
		if (seg_len == 0) {
			if (cmd_len + 1 <= sizeof(cand)) {
				for (sb_usize i = 0; i < cmd_len; i++) cand[i] = cmd[i];
				cand[cmd_len] = 0;
				sb_i64 r = sb_sys_faccessat(SB_AT_FDCWD, cand, SB_X_OK, 0);
				if (r >= 0) {
					if (cmd_len + 1 > out_sz) return 0;
					for (sb_usize i = 0; i < cmd_len; i++) out_path[i] = cand[i];
					out_path[cmd_len] = 0;
					return 1;
				}
			}
		} else {
			int needs_slash = 1;
			if (seg_len > 0 && seg[seg_len - 1] == '/') needs_slash = 0;
			sb_usize total = seg_len + (needs_slash ? 1 : 0) + cmd_len;
			if (total + 1 <= sizeof(cand)) {
				for (sb_usize i = 0; i < seg_len; i++) cand[i] = seg[i];
				sb_usize off = seg_len;
				if (needs_slash) cand[off++] = '/';
				for (sb_usize i = 0; i < cmd_len; i++) cand[off + i] = cmd[i];
				cand[off + cmd_len] = 0;
				sb_i64 r = sb_sys_faccessat(SB_AT_FDCWD, cand, SB_X_OK, 0);
				if (r >= 0) {
					if (total + 1 > out_sz) return 0;
					for (sb_usize i = 0; i < total; i++) out_path[i] = cand[i];
					out_path[total] = 0;
					return 1;
				}
			}
		}

		if (*p == ':') {
			p++;
			continue;
		}
		break;
	}

	return 0;
}

static int is_ws(char c) {
	return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

static int status_ok(sb_i32 st) {
	// Linux wait status encoding.
	if ((st & 0x7f) == 0) {
		int ec = (st >> 8) & 0xff;
		return ec == 0;
	}
	return 0;
}

static int run_one(const char *argv0, const char *path, char **argv_exec, char **envp) {
	sb_i64 pid = sb_sys_vfork();
	if (pid < 0) {
		sb_die_errno(argv0, "vfork", pid);
	}
	if (pid == 0) {
		sb_i64 r = sb_sys_execve(path, argv_exec, envp);
		// If exec fails, we must _exit.
		sb_die_errno(argv0, "execve", r);
	}

	sb_i32 st = 0;
	sb_i64 w = sb_sys_wait4((sb_i32)pid, &st, 0, 0);
	if (w < 0) {
		sb_die_errno(argv0, "wait4", w);
	}
	return status_ok(st) ? 0 : 1;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "xargs";

	sb_u32 max_per = 0; // 0 means "use maximum possible"

	int ai = 1;
	for (; ai < argc; ai++) {
		const char *a = argv[ai];
		if (!a) break;
		if (sb_streq(a, "--")) {
			ai++;
			break;
		}
		if (a[0] != '-') break;
		if (sb_streq(a, "-n")) {
			ai++;
			if (ai >= argc || !argv[ai]) sb_die_usage(argv0, "xargs [-n N] -- CMD [ARGS...]");
			if (sb_parse_u32_dec(argv[ai], &max_per) != 0 || max_per == 0) {
				sb_die_usage(argv0, "xargs [-n N] -- CMD [ARGS...]");
			}
			continue;
		}
		sb_die_usage(argv0, "xargs [-n N] -- CMD [ARGS...]");
	}

	if (ai >= argc || !argv[ai]) {
		sb_die_usage(argv0, "xargs [-n N] -- CMD [ARGS...]");
	}

	const char *cmd = argv[ai++];

	// Collect fixed args that come after CMD.
	char *fixed[64];
	int fixed_n = 0;
	for (; ai < argc; ai++) {
		if (!argv[ai]) continue;
		if (fixed_n >= (int)(sizeof(fixed) / sizeof(fixed[0]))) {
			sb_die_usage(argv0, "xargs [-n N] -- CMD [ARGS...] (too many fixed args)"
			);
		}
		fixed[fixed_n++] = argv[ai];
	}

	char exec_path[4096];
	if (!find_executable(envp, cmd, exec_path, sizeof(exec_path))) {
		(void)sb_write_str(2, argv0);
		(void)sb_write_str(2, ": ");
		(void)sb_write_str(2, cmd);
		(void)sb_write_str(2, ": not found\n");
		return 1;
	}

	// Cap by our fixed argv buffer.
	const sb_u32 argv_cap = 256;
	sb_u32 batch_cap = argv_cap - 1u - (sb_u32)fixed_n - 1u; // cmd + fixed + batch + NULL
	if (batch_cap > 200u) batch_cap = 200u;
	if (max_per == 0) {
		max_per = batch_cap;
	}
	if (max_per > batch_cap) {
		sb_die_usage(argv0, "xargs [-n N] -- CMD [ARGS...] (N too large)"
		);
	}

	char argbuf[8192];
	sb_usize arg_off = 0;
	sb_u32 batch_n = 0;
	char *batch_args[200];
	int saw_any = 0;

	char inbuf[4096];
	int in_token = 0;
	for (;;) {
		sb_i64 r = sb_sys_read(0, inbuf, sizeof(inbuf));
		if (r < 0) {
			sb_die_errno(argv0, "read", r);
		}
		if (r == 0) break;
		for (sb_i64 i = 0; i < r; i++) {
			char c = inbuf[i];
			if (is_ws(c)) {
				if (in_token) {
					if (arg_off + 1 > sizeof(argbuf)) {
						(void)sb_write_str(2, argv0);
						(void)sb_write_str(2, ": args too long\n");
						return 1;
					}
					argbuf[arg_off++] = 0;
					in_token = 0;
					batch_n++;
					saw_any = 1;
					if (batch_n >= max_per) {
						char *argv_exec[256];
						sb_u32 k = 0;
						argv_exec[k++] = (char *)exec_path;
						for (int fi = 0; fi < fixed_n; fi++) argv_exec[k++] = fixed[fi];
						for (sb_u32 bi = 0; bi < batch_n; bi++) argv_exec[k++] = batch_args[bi];
						argv_exec[k] = 0;
						if (run_one(argv0, exec_path, argv_exec, envp) != 0) {
							// Continue but remember failure.
							saw_any = 2;
						}
						batch_n = 0;
						arg_off = 0;
					}
				}
				continue;
			}

			// non-whitespace
			if (!in_token) {
				if (batch_n >= (sb_u32)(sizeof(batch_args) / sizeof(batch_args[0]))) {
					(void)sb_write_str(2, argv0);
					(void)sb_write_str(2, ": too many args\n");
					return 1;
				}
				batch_args[batch_n] = &argbuf[arg_off];
				in_token = 1;
			}
			if (arg_off + 1 > sizeof(argbuf)) {
				(void)sb_write_str(2, argv0);
				(void)sb_write_str(2, ": args too long\n");
				return 1;
			}
			argbuf[arg_off++] = c;
		}
	}

	if (in_token) {
		if (arg_off + 1 > sizeof(argbuf)) {
			(void)sb_write_str(2, argv0);
			(void)sb_write_str(2, ": args too long\n");
			return 1;
		}
		argbuf[arg_off++] = 0;
		batch_n++;
		saw_any = 1;
	}

	if (batch_n > 0) {
		char *argv_exec[256];
		sb_u32 k = 0;
		argv_exec[k++] = (char *)exec_path;
		for (int fi = 0; fi < fixed_n; fi++) argv_exec[k++] = fixed[fi];
		for (sb_u32 bi = 0; bi < batch_n; bi++) argv_exec[k++] = batch_args[bi];
		argv_exec[k] = 0;
		if (run_one(argv0, exec_path, argv_exec, envp) != 0) {
			saw_any = 2;
		}
	}

	// If there was no input, do nothing.
	if (!saw_any) return 0;
	// saw_any==2 indicates some invocation failed.
	return (saw_any == 2) ? 1 : 0;
}
