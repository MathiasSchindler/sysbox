#include "../src/sb.h"

static int watch_has_slash(const char *s) {
	if (!s) return 0;
	for (const char *p = s; *p; p++) {
		if (*p == '/') return 1;
	}
	return 0;
}

static const char *watch_getenv(char **envp, const char *key_eq) {
	return sb_getenv_kv(envp, key_eq);
}

static sb_i64 watch_try_exec_path(const char *path, char **argv, char **envp) {
	return sb_sys_execve(path, argv, envp);
}

static sb_i64 watch_execvp(const char *file, char **argv, char **envp) {
	if (!file || !*file) {
		return (sb_i64)-SB_ENOENT;
	}
	if (watch_has_slash(file)) {
		return watch_try_exec_path(file, argv, envp);
	}

	const char *path_env = watch_getenv(envp, "PATH=");
	if (!path_env || !*path_env) {
		path_env = "/bin:/usr/bin";
	}

	char full[4096];
	sb_usize fn = sb_strlen(file);

	const char *p = path_env;
	for (;;) {
		const char *seg = p;
		while (*p && *p != ':') {
			p++;
		}
		sb_usize seglen = (sb_usize)(p - seg);

		// dir + '/' + file + '\0'
		if (seglen + 1 + fn + 1 <= sizeof(full)) {
			sb_usize k = 0;
			for (; k < seglen; k++) full[k] = seg[k];
			if (k == 0) {
				full[k++] = '.';
			}
			if (full[k - 1] != '/') {
				full[k++] = '/';
			}
			for (sb_usize j = 0; j < fn; j++) full[k + j] = file[j];
			k += fn;
			full[k] = 0;

			sb_i64 r = watch_try_exec_path(full, argv, envp);
			if (r < 0) {
				sb_u64 e = (sb_u64)(-r);
				if (e != (sb_u64)SB_ENOENT && e != (sb_u64)SB_ENOTDIR) {
					return r;
				}
			}
		}

		if (*p == ':') {
			p++;
			continue;
		}
		break;
	}

	return (sb_i64)-SB_ENOENT;
}

static int watch_exit_code_from_wait_status(sb_i32 status) {
	sb_u32 u = (sb_u32)status;
	sb_u32 sig = u & 0x7Fu;
	if (sig != 0) {
		return 128 + (int)sig;
	}
	return (int)((u >> 8) & 0xFFu);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "watch";

	sb_u64 interval = 2;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (sb_streq(a, "-n")) {
			if (i + 1 >= argc || !argv[i + 1]) sb_die_usage(argv0, "watch [-n SECS] [--] CMD [ARGS...]");
			if (sb_parse_u64_dec(argv[++i], &interval) != 0) sb_die_usage(argv0, "watch [-n SECS] [--] CMD [ARGS...]");
			continue;
		}
		if (a[0] == '-') sb_die_usage(argv0, "watch [-n SECS] [--] CMD [ARGS...]");
		break;
	}

	if (i >= argc) sb_die_usage(argv0, "watch [-n SECS] [--] CMD [ARGS...]");

	char **cmd_argv = &argv[i];
	const char *cmd = argv[i] ? argv[i] : "";

	while (1) {
		// Clear screen + home
		(void)sb_write_str(1, "\033[H\033[2J");
		(void)sb_write_str(1, "Every ");
		(void)sb_write_u64_dec(1, interval);
		(void)sb_write_str(1, "s: ");
		(void)sb_write_str(1, cmd);
		(void)sb_write_str(1, "\n\n");

		sb_i64 pid = sb_sys_vfork();
		if (pid < 0) sb_die_errno(argv0, "vfork", pid);
		if (pid == 0) {
			sb_i64 er = watch_execvp(cmd, cmd_argv, envp);
			(void)sb_write_str(2, argv0);
			(void)sb_write_str(2, ": ");
			(void)sb_write_str(2, cmd);
			(void)sb_write_str(2, ": errno=");
			sb_write_hex_u64(2, (sb_u64)(-er));
			(void)sb_write_str(2, "\n");
			sb_exit(127);
		}

		sb_i32 status = 0;
		sb_i64 w = sb_sys_wait4((sb_i32)pid, &status, 0, 0);
		if (w < 0) sb_die_errno(argv0, "wait4", w);
		(void)watch_exit_code_from_wait_status(status);

		struct sb_timespec ts;
		ts.tv_sec = (sb_i64)interval;
		ts.tv_nsec = 0;
		(void)sb_sys_nanosleep(&ts, 0);
	}
}
