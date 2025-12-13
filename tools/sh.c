#include "../src/sb.h"

// Minimal syscall-only shell.
// Subset:
// - sh [-c CMD] [FILE]
// - separators: ';' and newlines
// - pipelines: |
// - redirections: <, >, >>
// - quoting: single '...' and double "..." (backslash escapes in double)
// - builtins: cd [DIR], exit [N]
// No variable expansion, no globbing, no job control.

#define SH_MAX_LINE 8192
#define SH_MAX_TOKS 256
#define SH_MAX_ARGS 64
#define SH_MAX_CMDS 16

enum sh_tok_kind {
	SH_TOK_WORD = 0,
	SH_TOK_PIPE,
	SH_TOK_SEMI,
	SH_TOK_REDIR_IN,
	SH_TOK_REDIR_OUT,
	SH_TOK_REDIR_OUT_APP,
	SH_TOK_END,
};

struct sh_tok {
	enum sh_tok_kind kind;
	const char *s;
	sb_usize n;
};

struct sh_redir {
	const char *in_path;
	sb_usize in_len;
	const char *out_path;
	sb_usize out_len;
	int out_append;
};

struct sh_cmd {
	const char *argv[SH_MAX_ARGS + 1];
	sb_u32 argc;
	struct sh_redir redir;
};

static void sh_write_err(const char *argv0, const char *msg) {
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, msg);
	(void)sb_write_str(2, "\n");
}

static void sh_write_err2(const char *argv0, const char *a, const char *b) {
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, a);
	(void)sb_write_str(2, b);
	(void)sb_write_str(2, "\n");
}

static void sh_print_errno(const char *argv0, const char *ctx, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, ctx);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static int sh_tokenize(const char *line, char *wordbuf, sb_usize wordbuf_sz, struct sh_tok *toks, sb_u32 *out_ntoks, const char *argv0) {
	sb_u32 nt = 0;
	sb_usize woff = 0;
	const char *p = line;
	while (*p) {
		// Skip whitespace
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (*p == 0) break;

		// Comment (only if starts a token)
		if (*p == '#') {
			break;
		}

		if (nt + 1 >= SH_MAX_TOKS) {
			sh_write_err(argv0, "line too complex");
			return -1;
		}

		if (*p == ';') {
			toks[nt++] = (struct sh_tok){ .kind = SH_TOK_SEMI, .s = p, .n = 1 };
			p++;
			continue;
		}
		if (*p == '|') {
			toks[nt++] = (struct sh_tok){ .kind = SH_TOK_PIPE, .s = p, .n = 1 };
			p++;
			continue;
		}
		if (*p == '<') {
			toks[nt++] = (struct sh_tok){ .kind = SH_TOK_REDIR_IN, .s = p, .n = 1 };
			p++;
			continue;
		}
		if (*p == '>') {
			if (*(p + 1) == '>') {
				toks[nt++] = (struct sh_tok){ .kind = SH_TOK_REDIR_OUT_APP, .s = p, .n = 2 };
				p += 2;
			} else {
				toks[nt++] = (struct sh_tok){ .kind = SH_TOK_REDIR_OUT, .s = p, .n = 1 };
				p++;
			}
			continue;
		}

		// WORD (supports quotes)
		if (woff + 1 >= wordbuf_sz) {
			sh_write_err(argv0, "line too long");
			return -1;
		}
		char *w_start = wordbuf + woff;
		char *w_out = w_start;
		while (*p) {
			char c = *p;
			if (c == ' ' || c == '\t' || c == '\r' || c == ';' || c == '|' || c == '<' || c == '>') {
				break;
			}
			if (c == '#') {
				// Treat as comment starter if unquoted and at token boundary.
				break;
			}
			if (c == '\'' || c == '"') {
				char q = c;
				p++;
				while (*p && *p != q) {
					char qc = *p;
					if (q == '"' && qc == '\\') {
						p++;
						if (!*p) break;
						char esc = *p;
						if (esc == 'n') qc = '\n';
						else if (esc == 't') qc = '\t';
						else qc = esc;
						if ((sb_usize)(w_out - wordbuf) + 2 > wordbuf_sz) {
							sh_write_err(argv0, "line too long");
							return -1;
						}
						*w_out++ = qc;
						p++;
						continue;
					}
					if ((sb_usize)(w_out - wordbuf) + 2 > wordbuf_sz) {
						sh_write_err(argv0, "line too long");
						return -1;
					}
					*w_out++ = qc;
					p++;
				}
				if (*p != q) {
					sh_write_err(argv0, "unterminated quote");
					return -1;
				}
				p++;
				continue;
			}
			if (c == '\\') {
				// Simple escape: take next byte literally.
				p++;
				if (!*p) break;
				if ((sb_usize)(w_out - wordbuf) + 2 > wordbuf_sz) {
					sh_write_err(argv0, "line too long");
					return -1;
				}
				*w_out++ = *p++;
				continue;
			}
			if ((sb_usize)(w_out - wordbuf) + 2 > wordbuf_sz) {
				sh_write_err(argv0, "line too long");
				return -1;
			}
			*w_out++ = *p++;
		}
		*w_out = 0;
		sb_usize wn = (sb_usize)(w_out - w_start);
		toks[nt++] = (struct sh_tok){ .kind = SH_TOK_WORD, .s = w_start, .n = wn };
		woff += wn + 1;
		if (*p == '#') break;
	}
	if (nt + 1 >= SH_MAX_TOKS) {
		sh_write_err(argv0, "line too complex");
		return -1;
	}
	toks[nt++] = (struct sh_tok){ .kind = SH_TOK_END, .s = p, .n = 0 };
	*out_ntoks = nt;
	return 0;
}

static int sh_parse_pipeline(struct sh_tok *toks, sb_u32 ntoks, sb_u32 *io, struct sh_cmd *cmds, sb_u32 *out_ncmds, const char *argv0) {
	sb_u32 i = *io;
	sb_u32 nc = 0;
	while (1) {
		if (nc >= SH_MAX_CMDS) {
			sh_write_err(argv0, "too many pipeline commands");
			return -1;
		}
		struct sh_cmd *cmd = &cmds[nc];
		cmd->argc = 0;
		cmd->redir.in_path = 0;
		cmd->redir.in_len = 0;
		cmd->redir.out_path = 0;
		cmd->redir.out_len = 0;
		cmd->redir.out_append = 0;

		int saw_word = 0;
		while (i < ntoks) {
			enum sh_tok_kind k = toks[i].kind;
			if (k == SH_TOK_WORD) {
				saw_word = 1;
				if (cmd->argc + 1 >= SH_MAX_ARGS) {
					sh_write_err(argv0, "too many args");
					return -1;
				}
				cmd->argv[cmd->argc++] = toks[i].s;
				i++;
				continue;
			}
			if (k == SH_TOK_REDIR_IN || k == SH_TOK_REDIR_OUT || k == SH_TOK_REDIR_OUT_APP) {
				enum sh_tok_kind rk = k;
				i++;
				if (i >= ntoks || toks[i].kind != SH_TOK_WORD) {
					sh_write_err(argv0, "redir missing path");
					return -1;
				}
				if (rk == SH_TOK_REDIR_IN) {
					cmd->redir.in_path = toks[i].s;
					cmd->redir.in_len = toks[i].n;
				} else {
					cmd->redir.out_path = toks[i].s;
					cmd->redir.out_len = toks[i].n;
					cmd->redir.out_append = (rk == SH_TOK_REDIR_OUT_APP);
				}
				i++;
				continue;
			}
			break;
		}

		if (!saw_word) {
			// Empty command; allow at end.
			return -1;
		}
		cmd->argv[cmd->argc] = 0;
		nc++;

		if (i < ntoks && toks[i].kind == SH_TOK_PIPE) {
			i++;
			continue;
		}
		break;
	}
	*io = i;
	*out_ncmds = nc;
	return 0;
}

static sb_i32 sh_status_to_exit(sb_i32 st) {
	// Linux wait status encoding.
	if ((st & 0x7f) == 0) {
		return (sb_i32)((st >> 8) & 0xff);
	}
	return (sb_i32)(128 + (st & 0x7f));
}

static sb_i32 sh_exec_search(const char *argv0, char **envp, const char *cmd, char *const argv_exec[]) {
	// Returns only on error; prints message. Exit code to use: 127 for not found, 126 for other exec failure.
	if (!cmd || !*cmd) {
		sh_write_err(argv0, "empty command");
		return 127;
	}
	if (sb_has_slash(cmd)) {
		sb_i64 r = sb_sys_execve(cmd, argv_exec, envp);
		sh_print_errno(argv0, "execve", r);
		return 126;
	}

	const char *path_env = sb_getenv_kv(envp, "PATH=");
	if (!path_env) {
		path_env = "/bin:/usr/bin";
	}

	char cand[4096];
	const char *p = path_env;
	sb_i64 last_err = -SB_ENOENT;
	while (1) {
		const char *seg = p;
		while (*p && *p != ':') p++;
		sb_usize seg_len = (sb_usize)(p - seg);
		sb_usize cmd_len = sb_strlen(cmd);

		if (seg_len == 0) {
			// current directory
			if (cmd_len + 1 <= sizeof(cand)) {
				for (sb_usize i = 0; i < cmd_len; i++) cand[i] = cmd[i];
				cand[cmd_len] = 0;
				sb_i64 r = sb_sys_execve(cand, argv_exec, envp);
				if (r >= 0) return 0;
				last_err = r;
				// continue on ENOENT/ENOTDIR
			}
		} else {
			int needs_slash = 1;
			if (seg_len > 0 && seg[seg_len - 1] == '/') needs_slash = 0;
			sb_usize total = seg_len + (needs_slash ? 1u : 0u) + cmd_len;
			if (total + 1 <= sizeof(cand)) {
				for (sb_usize i = 0; i < seg_len; i++) cand[i] = seg[i];
				sb_usize off = seg_len;
				if (needs_slash) cand[off++] = '/';
				for (sb_usize i = 0; i < cmd_len; i++) cand[off + i] = cmd[i];
				cand[off + cmd_len] = 0;
				sb_i64 r = sb_sys_execve(cand, argv_exec, envp);
				if (r >= 0) return 0;
				last_err = r;
			}
		}

		if (*p == ':') {
			p++;
			continue;
		}
		break;
	}

	if (last_err == -SB_ENOENT || last_err == -SB_ENOTDIR) {
		sh_write_err2(argv0, cmd, ": not found");
		return 127;
	}
	sh_print_errno(argv0, "execve", last_err);
	return 126;
}

static sb_i32 sh_run_pipeline(const char *argv0, struct sh_cmd *cmds, sb_u32 ncmds, char **envp) {
	sb_i32 pids[SH_MAX_CMDS];
	sb_u32 pn = 0;
	sb_i32 in_fd = -1;

	for (sb_u32 ci = 0; ci < ncmds; ci++) {
		sb_i32 pipefd[2] = { -1, -1 };
		int has_next = (ci + 1 < ncmds);
		if (has_next) {
			sb_i64 pr = sb_sys_pipe2(pipefd, SB_O_CLOEXEC);
			if (pr < 0) {
				sh_print_errno(argv0, "pipe2", pr);
				return 1;
			}
		}

		sb_i64 vr = sb_sys_vfork();
		if (vr < 0) {
			sh_print_errno(argv0, "vfork", vr);
			return 1;
		}
		if (vr == 0) {
			// child
			if (in_fd != -1) {
				sb_i64 dr = sb_sys_dup2(in_fd, 0);
				if (dr < 0) {
					sh_print_errno(argv0, "dup2", dr);
					sb_exit(127);
				}
			}
			if (has_next) {
				sb_i64 dr = sb_sys_dup2(pipefd[1], 1);
				if (dr < 0) {
					sh_print_errno(argv0, "dup2", dr);
					sb_exit(127);
				}
			}
			// Close pipe fds
			if (in_fd != -1) (void)sb_sys_close(in_fd);
			if (has_next) {
				(void)sb_sys_close(pipefd[0]);
				(void)sb_sys_close(pipefd[1]);
			}

			// Apply redirections
			if (cmds[ci].redir.in_path) {
				sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, cmds[ci].redir.in_path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
				if (fd < 0) {
					sh_print_errno(argv0, "open", fd);
					sb_exit(1);
				}
				sb_i64 dr = sb_sys_dup2((sb_i32)fd, 0);
				if (dr < 0) {
					sh_print_errno(argv0, "dup2", dr);
					sb_exit(1);
				}
				(void)sb_sys_close((sb_i32)fd);
			}
			if (cmds[ci].redir.out_path) {
				sb_i32 flags = SB_O_WRONLY | SB_O_CREAT | SB_O_CLOEXEC;
				flags |= cmds[ci].redir.out_append ? SB_O_APPEND : SB_O_TRUNC;
				sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, cmds[ci].redir.out_path, flags, 0666);
				if (fd < 0) {
					sh_print_errno(argv0, "open", fd);
					sb_exit(1);
				}
				sb_i64 dr = sb_sys_dup2((sb_i32)fd, 1);
				if (dr < 0) {
					sh_print_errno(argv0, "dup2", dr);
					sb_exit(1);
				}
				(void)sb_sys_close((sb_i32)fd);
			}

			// Exec
			sb_i32 rc = sh_exec_search(argv0, envp, cmds[ci].argv[0], (char *const *)cmds[ci].argv);
			sb_exit(rc);
		}

		// parent
		pids[pn++] = (sb_i32)vr;
		if (in_fd != -1) (void)sb_sys_close(in_fd);
		if (has_next) {
			(void)sb_sys_close(pipefd[1]);
			in_fd = pipefd[0];
		} else {
			in_fd = -1;
		}
	}
	if (in_fd != -1) (void)sb_sys_close(in_fd);

	// Wait all, but return last pipeline element status.
	sb_i32 last_status = 0;
	for (sb_u32 wi = 0; wi < pn; wi++) {
		sb_i32 st = 0;
		sb_i64 wr = sb_sys_wait4(pids[wi], &st, 0, 0);
		if (wr < 0) {
			sh_print_errno(argv0, "wait4", wr);
			last_status = (sb_i32)wr;
			continue;
		}
		if (wi + 1 == pn) last_status = st;
	}
	return sh_status_to_exit(last_status);
}

static int sh_is_builtin(const char *s, const char *name) {
	if (!s || !name) return 0;
	return sb_streq(s, name);
}

static sb_i32 sh_run_line(const char *argv0, char *line, char **envp) {
	struct sh_tok toks[SH_MAX_TOKS];
	char wordbuf[SH_MAX_LINE];
	sb_u32 ntoks = 0;
	if (sh_tokenize(line, wordbuf, sizeof(wordbuf), toks, &ntoks, argv0) != 0) return 2;

	sb_u32 i = 0;
	sb_i32 last_rc = 0;
	while (i < ntoks) {
		// Skip separators
		while (i < ntoks && (toks[i].kind == SH_TOK_SEMI)) i++;
		if (i >= ntoks || toks[i].kind == SH_TOK_END) break;

		struct sh_cmd cmds[SH_MAX_CMDS];
		sb_u32 ncmds = 0;
		if (sh_parse_pipeline(toks, ntoks, &i, cmds, &ncmds, argv0) != 0) {
			// If parse fails, skip to next separator.
			while (i < ntoks && toks[i].kind != SH_TOK_SEMI && toks[i].kind != SH_TOK_END) i++;
			last_rc = 2;
			continue;
		}

		// End of command: optional ';'
		if (i < ntoks && toks[i].kind == SH_TOK_SEMI) i++;

		// Builtins only when not in a pipeline.
		if (ncmds == 1 && cmds[0].argc > 0) {
			const char *cmd0 = cmds[0].argv[0];
			if (sh_is_builtin(cmd0, "cd")) {
				const char *dir = 0;
				if (cmds[0].argc >= 2) {
					dir = cmds[0].argv[1];
				} else {
					dir = sb_getenv_kv(envp, "HOME=");
				}
				if (!dir || !*dir) {
					sh_write_err(argv0, "cd: missing directory (and HOME unset)");
					last_rc = 1;
				} else {
					sb_i64 cr = sb_sys_chdir(dir);
					if (cr < 0) {
						sh_print_errno(argv0, "chdir", cr);
						last_rc = 1;
					} else {
						last_rc = 0;
					}
				}
				continue;
			}
			if (sh_is_builtin(cmd0, "exit")) {
				sb_i32 code = 0;
				if (cmds[0].argc >= 2) {
					sb_i64 v = 0;
					if (sb_parse_i64_dec(cmds[0].argv[1], &v) != 0) {
						code = 2;
					} else {
						code = (sb_i32)v;
					}
				}
				sb_exit(code);
			}
		}

		last_rc = sh_run_pipeline(argv0, cmds, ncmds, envp);
	}
	return last_rc;
}

struct sh_reader {
	sb_i32 fd;
	char buf[4096];
	sb_usize off;
	sb_usize len;
};

static sb_i32 sh_reader_fill(struct sh_reader *r) {
	r->off = 0;
	sb_i64 n = sb_sys_read(r->fd, r->buf, sizeof(r->buf));
	if (n < 0) return (sb_i32)n;
	r->len = (sb_usize)n;
	return (sb_i32)n;
}

static int sh_read_line(struct sh_reader *r, char *out, sb_usize out_sz, sb_usize *out_n) {
	// Returns: 1 line read, 0 EOF, -1 error.
	sb_usize n = 0;
	while (1) {
		if (r->off >= r->len) {
			sb_i32 fr = sh_reader_fill(r);
			if (fr < 0) return -1;
			if (fr == 0) {
				if (n == 0) return 0;
				out[n] = 0;
				*out_n = n;
				return 1;
			}
		}
		char c = r->buf[r->off++];
		if (c == '\n') {
			out[n] = 0;
			*out_n = n;
			return 1;
		}
		if (n + 1 >= out_sz) {
			return -1;
		}
		out[n++] = c;
	}
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "sh";

	int i = 1;
	const char *cmd_str = 0;
	const char *file = 0;

	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sb_streq(a, "-c")) {
			if (i + 1 >= argc || !argv[i + 1]) {
				sb_die_usage(argv0, "sh [-c CMD] [FILE]");
			}
			cmd_str = argv[++i];
			continue;
		}
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		// First non-flag is FILE.
		file = a;
		i++;
		break;
	}
	if (i < argc && argv[i]) {
		// Extra args not supported (keeps parsing simple).
		sb_die_usage(argv0, "sh [-c CMD] [FILE]");
	}

	if (cmd_str) {
		char linebuf[SH_MAX_LINE];
		sb_usize n = sb_strlen(cmd_str);
		if (n + 1 > sizeof(linebuf)) {
			sh_write_err(argv0, "-c command too long");
			return 2;
		}
		for (sb_usize j = 0; j <= n; j++) linebuf[j] = cmd_str[j];
		return sh_run_line(argv0, linebuf, envp);
	}

	sb_i32 fd = 0;
	if (file) {
		sb_i64 ofd = sb_sys_openat(SB_AT_FDCWD, file, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (ofd < 0) {
			sh_print_errno(argv0, "open", ofd);
			return 1;
		}
		fd = (sb_i32)ofd;
	}

	struct sh_reader r = { .fd = fd, .off = 0, .len = 0 };
	char line[SH_MAX_LINE];
	sb_i32 last_rc = 0;
	while (1) {
		if (!file) {
			// Minimal prompt.
			(void)sb_write_str(1, "$ ");
		}
		sb_usize ln = 0;
		int rr = sh_read_line(&r, line, sizeof(line), &ln);
		if (rr < 0) {
			sh_write_err(argv0, "read error or line too long");
			last_rc = 1;
			break;
		}
		if (rr == 0) break;
		// Skip empty lines.
		char *p = line;
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (*p == 0) continue;
		last_rc = sh_run_line(argv0, line, envp);
	}
	if (file) (void)sb_sys_close(fd);
	return last_rc;
}
