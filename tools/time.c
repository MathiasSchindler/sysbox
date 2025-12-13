#include "../src/sb.h"

static const char *time_getenv(char **envp, const char *key_eq) {
	if (!envp || !key_eq) {
		return 0;
	}
	sb_usize klen = sb_strlen(key_eq);
	for (sb_usize i = 0; envp[i]; i++) {
		const char *e = envp[i];
		int ok = 1;
		for (sb_usize j = 0; j < klen; j++) {
			if (e[j] != key_eq[j]) {
				ok = 0;
				break;
			}
		}
		if (ok) {
			return e + klen;
		}
	}
	return 0;
}

static int time_has_slash(const char *s) {
	if (!s) return 0;
	for (const char *p = s; *p; p++) {
		if (*p == '/') return 1;
	}
	return 0;
}

static sb_i64 time_try_exec_path(const char *path, char **argv, char **envp) {
	return sb_sys_execve(path, argv, envp);
}

static sb_i64 time_execvp(const char *file, char **argv, char **envp) {
	if (!file || !*file) {
		return (sb_i64)-SB_ENOENT;
	}
	if (time_has_slash(file)) {
		return time_try_exec_path(file, argv, envp);
	}

	const char *path_env = time_getenv(envp, "PATH=");
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

			sb_i64 r = time_try_exec_path(full, argv, envp);
			// If execve failed with ENOENT/ENOTDIR, continue searching; otherwise return.
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

static void time_write_u64_09(sb_i32 fd, sb_u64 v) {
	char buf[9];
	for (int i = 8; i >= 0; i--) {
		sb_u64 q = v / 10u;
		sb_u64 r = v - q * 10u;
		buf[i] = (char)('0' + (char)r);
		v = q;
	}
	(void)sb_write_all(fd, buf, 9);
}

static int time_exit_code_from_wait_status(sb_i32 status) {
	sb_u32 u = (sb_u32)status;
	sb_u32 sig = u & 0x7Fu;
	if (sig != 0) {
		return 128 + (int)sig;
	}
	return (int)((u >> 8) & 0xFFu);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "time";

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
		sb_die_usage(argv0, "time [--] CMD [ARGS...]");
	}

	if (i >= argc) {
		sb_die_usage(argv0, "time [--] CMD [ARGS...]");
	}

	char **cmd_argv = &argv[i];
	const char *cmd = argv[i] ? argv[i] : "";

	struct sb_timespec t0;
	struct sb_timespec t1;
	sb_i64 r = sb_sys_clock_gettime(SB_CLOCK_MONOTONIC, &t0);
	if (r < 0) {
		sb_die_errno(argv0, "clock_gettime", r);
	}

	sb_i64 pid = sb_sys_vfork();
	if (pid < 0) {
		sb_die_errno(argv0, "vfork", pid);
	}

	if (pid == 0) {
		sb_i64 er = time_execvp(cmd, cmd_argv, envp);
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
	if (w < 0) {
		sb_die_errno(argv0, "wait4", w);
	}

	r = sb_sys_clock_gettime(SB_CLOCK_MONOTONIC, &t1);
	if (r < 0) {
		sb_die_errno(argv0, "clock_gettime", r);
	}

	sb_i64 sec = t1.tv_sec - t0.tv_sec;
	sb_i64 nsec = t1.tv_nsec - t0.tv_nsec;
	if (nsec < 0) {
		sec -= 1;
		nsec += 1000000000LL;
	}
	if (sec < 0) {
		sec = 0;
		nsec = 0;
	}

	(void)sb_write_str(2, "real ");
	(void)sb_write_i64_dec(2, sec);
	(void)sb_write_str(2, ".");
	time_write_u64_09(2, (sb_u64)nsec);
	(void)sb_write_str(2, "\n");

	return time_exit_code_from_wait_status(status);
}
