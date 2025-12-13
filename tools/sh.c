#include "../src/sb.h"

#define sh_print_errno sb_print_errno

// Minimal syscall-only shell.
// Subset:
// - sh [-c CMD] [FILE]
// - separators: ';' and newlines
// - pipelines: |
// - conditionals: && and ||
// - redirections: <, >, >>
// - quoting: single '...' and double "..." (backslash escapes in double)
// - builtins: cd [DIR], exit [N]
// No variable expansion, no globbing, no job control.

#define SH_MAX_LINE 8192

// Script execution reads the whole file into a fixed buffer so multi-line
// constructs (if/while/for) can work.
#define SH_MAX_PROG 65536

// Tokenization limits for a whole script. These are intentionally modest.
#define SH_MAX_TOKS 2048
#define SH_MAX_WORDBUF 65536

#define SH_MAX_ARGS 64
#define SH_MAX_CMDS 16

#define SH_MAX_VARS 8
#define SH_VAR_NAME_MAX 32
#define SH_VAR_VAL_MAX 256

enum sh_tok_kind {
	SH_TOK_WORD = 0,
	SH_TOK_PIPE,
	SH_TOK_OR_IF,
	SH_TOK_SEMI,
	SH_TOK_AND_IF,
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

struct sh_var {
	char name[SH_VAR_NAME_MAX];
	char val[SH_VAR_VAL_MAX];
};

struct sh_vars {
	sb_u32 n;
	struct sh_var vars[SH_MAX_VARS];
};

struct sh_ctx {
	struct sh_vars vars;
	// Positional parameters:
	// - In script mode: $0 is script path; $1.. are args.
	// - In -c mode: if extra args are provided, first becomes $0, rest $1..
	const char *posv[32];
	sb_u32 posc;
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

static int sh_is_name_start(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int sh_is_name_char(char c) {
	return sh_is_name_start(c) || (c >= '0' && c <= '9');
}

static int sh_is_digit(char c) {
	return (c >= '0' && c <= '9');
}

static const char *sh_vars_lookup(const struct sh_vars *vars, const char *name, sb_usize n) {
	if (!vars || !name || n == 0) return 0;
	for (sb_u32 i = vars->n; i > 0; i--) {
		const struct sh_var *v = &vars->vars[i - 1];
		// Compare name with NUL-terminated v->name.
		sb_usize j = 0;
		for (; j < n && v->name[j] != 0; j++) {
			if (v->name[j] != name[j]) break;
		}
		if (j == n && v->name[j] == 0) {
			return v->val;
		}
	}
	return 0;
}

static const char *sh_pos_lookup(const struct sh_ctx *ctx, sb_u32 idx) {
	if (!ctx) return 0;
	if (idx >= ctx->posc) return 0;
	return ctx->posv[idx];
}

static int sh_append_u64_dec(char *buf, sb_usize buf_sz, sb_usize *ioff, sb_u64 v, const char *argv0) {
	// Appends decimal v (no trailing NUL). Returns 0 on success.
	char tmp[32];
	sb_usize n = 0;
	if (v == 0) {
		tmp[n++] = '0';
	} else {
		while (v && n < sizeof(tmp)) {
			tmp[n++] = (char)('0' + (v % 10u));
			v /= 10u;
		}
	}
	if (*ioff + n + 1 > buf_sz) {
		sh_write_err(argv0, "line too long");
		return -1;
	}
	for (sb_usize i = 0; i < n; i++) buf[(*ioff)++] = tmp[n - 1 - i];
	return 0;
}

static int sh_append_pos_join(char *buf, sb_usize buf_sz, sb_usize *ioff, const struct sh_ctx *ctx, const char *argv0) {
	// Joins $1..$N with single spaces (does not include $0).
	if (!ctx || ctx->posc <= 1) return 0;
	for (sb_u32 i = 1; i < ctx->posc; i++) {
		const char *s = ctx->posv[i];
		if (!s) continue;
		if (i != 1) {
			if (*ioff + 2 > buf_sz) {
				sh_write_err(argv0, "line too long");
				return -1;
			}
			buf[(*ioff)++] = ' ';
		}
		for (sb_usize k = 0; s[k]; k++) {
			if (*ioff + 2 > buf_sz) {
				sh_write_err(argv0, "line too long");
				return -1;
			}
			buf[(*ioff)++] = s[k];
		}
	}
	return 0;
}

static int sh_vars_push(struct sh_vars *vars, const char *name, const char *val, const char *argv0) {
	if (!vars || !name || !val) return -1;
	if (vars->n >= SH_MAX_VARS) {
		sh_write_err(argv0, "variable stack full");
		return -1;
	}
	struct sh_var *v = &vars->vars[vars->n++];
	// Copy name.
	sb_usize ni = 0;
	for (; name[ni] && ni + 1 < sizeof(v->name); ni++) v->name[ni] = name[ni];
	v->name[ni] = 0;
	// Copy value.
	sb_usize vi = 0;
	for (; val[vi] && vi + 1 < sizeof(v->val); vi++) v->val[vi] = val[vi];
	v->val[vi] = 0;
	return 0;
}

static void sh_vars_pop(struct sh_vars *vars) {
	if (!vars || vars->n == 0) return;
	vars->n--;
}

static const char *sh_expand_word(const char *in, struct sh_ctx *ctx, char *buf, sb_usize buf_sz, sb_usize *ioff, const char *argv0) {
	// Expands:
	// - $NAME: for-loop variables (stack)
	// - $0..$N: positional parameters
	// - $#: number of positional parameters (excluding $0)
	// - $@ and $*: join $1..$N with spaces
	// No ${} support.
	if (!in) return "";
	sb_usize start = *ioff;
	const char *p = in;
	while (*p) {
		char c = *p++;
		if (c == '$' && *p == '#') {
			p++;
			sb_u64 argc1 = 0;
			if (ctx && ctx->posc > 1) argc1 = (sb_u64)(ctx->posc - 1);
			if (sh_append_u64_dec(buf, buf_sz, ioff, argc1, argv0) != 0) return 0;
			continue;
		}
		if (c == '$' && (*p == '@' || *p == '*')) {
			p++;
			if (sh_append_pos_join(buf, buf_sz, ioff, ctx, argv0) != 0) return 0;
			continue;
		}
		if (c == '$' && sh_is_digit(*p)) {
			// Positional parameter: one or more digits.
			const char *ds = p;
			sb_usize dn = 0;
			while (*p && sh_is_digit(*p)) {
				p++;
				dn++;
			}
			sb_u32 idx = 0;
			if (sb_parse_u32_dec_n(ds, dn, &idx) == 0) {
				const char *val = sh_pos_lookup(ctx, idx);
				if (val) {
					for (sb_usize k = 0; val[k]; k++) {
						if (*ioff + 2 > buf_sz) {
							sh_write_err(argv0, "line too long");
							return 0;
						}
						buf[(*ioff)++] = val[k];
					}
					continue;
				}
			}
			// Unknown positional: expand to empty.
			continue;
		}
		if (c == '$' && sh_is_name_start(*p)) {
			const char *ns = p;
			sb_usize nn = 0;
			while (*p && sh_is_name_char(*p)) {
				p++;
				nn++;
			}
			const char *val = sh_vars_lookup(&ctx->vars, ns, nn);
			if (val) {
				for (sb_usize k = 0; val[k]; k++) {
					if (*ioff + 2 > buf_sz) {
						sh_write_err(argv0, "line too long");
						return 0;
					}
					buf[(*ioff)++] = val[k];
				}
				continue;
			}
			// Unknown var: keep literal $NAME.
			if (*ioff + 1 + nn + 1 > buf_sz) {
				sh_write_err(argv0, "line too long");
				return 0;
			}
			buf[(*ioff)++] = '$';
			for (sb_usize k = 0; k < nn; k++) buf[(*ioff)++] = ns[k];
			continue;
		}
		if (*ioff + 2 > buf_sz) {
			sh_write_err(argv0, "line too long");
			return 0;
		}
		buf[(*ioff)++] = c;
	}
	if (*ioff + 1 > buf_sz) {
		sh_write_err(argv0, "line too long");
		return 0;
	}
	buf[(*ioff)++] = 0;
	return buf + start;
}

static int sh_tokenize(const char *line, char *wordbuf, sb_usize wordbuf_sz, struct sh_tok *toks, sb_u32 *out_ntoks, const char *argv0) {
	sb_u32 nt = 0;
	sb_usize woff = 0;
	const char *p = line;
	while (*p) {
		// Skip whitespace
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (*p == '\n') {
			// Newlines act like ';' separators for script tokenization.
			toks[nt++] = (struct sh_tok){ .kind = SH_TOK_SEMI, .s = p, .n = 1 };
			p++;
			continue;
		}
		if (*p == 0) break;

		// Comment (only if starts a token): skip to end of line.
		if (*p == '#') {
			while (*p && *p != '\n') p++;
			continue;
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
		if (*p == '&') {
			if (*(p + 1) == '&') {
				toks[nt++] = (struct sh_tok){ .kind = SH_TOK_AND_IF, .s = p, .n = 2 };
				p += 2;
				continue;
			}
			sh_write_err(argv0, "unsupported token '&'");
			return -1;
		}
		if (*p == '|') {
			if (*(p + 1) == '|') {
				toks[nt++] = (struct sh_tok){ .kind = SH_TOK_OR_IF, .s = p, .n = 2 };
				p += 2;
			} else {
				toks[nt++] = (struct sh_tok){ .kind = SH_TOK_PIPE, .s = p, .n = 1 };
				p++;
			}
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
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';' || c == '|' || c == '&' || c == '<' || c == '>') {
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
	}
	if (nt + 1 >= SH_MAX_TOKS) {
		sh_write_err(argv0, "line too complex");
		return -1;
	}
	toks[nt++] = (struct sh_tok){ .kind = SH_TOK_END, .s = p, .n = 0 };
	*out_ntoks = nt;
	return 0;
}

static int sh_tok_is_word(const struct sh_tok *t, const char *w) {
	return t && t->kind == SH_TOK_WORD && w && sb_streq(t->s, w);
}

static void sh_consume_sep(struct sh_tok *toks, sb_u32 end, sb_u32 *io, int *next_and, int *next_or) {
	if (!toks || !io) return;
	sb_u32 i = *io;
	if (i >= end) return;
	if (toks[i].kind == SH_TOK_AND_IF) {
		if (next_and) *next_and = 1;
		if (next_or) *next_or = 0;
		i++;
	} else if (toks[i].kind == SH_TOK_OR_IF) {
		if (next_or) *next_or = 1;
		if (next_and) *next_and = 0;
		i++;
	} else if (toks[i].kind == SH_TOK_SEMI) {
		i++;
	}
	*io = i;
}

static int sh_parse_pipeline(struct sh_tok *toks, sb_u32 ntoks, sb_u32 *io, struct sh_cmd *cmds, sb_u32 *out_ncmds, struct sh_ctx *ctx,
				   char *argbuf, sb_usize argbuf_sz, sb_usize *io_argoff, const char *argv0) {
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
				const char *ex = sh_expand_word(toks[i].s, ctx, argbuf, argbuf_sz, io_argoff, argv0);
				if (!ex) return -1;
				cmd->argv[cmd->argc++] = ex;
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
				const char *path_ex = sh_expand_word(toks[i].s, ctx, argbuf, argbuf_sz, io_argoff, argv0);
				if (!path_ex) return -1;
				if (rk == SH_TOK_REDIR_IN) {
					cmd->redir.in_path = path_ex;
					cmd->redir.in_len = toks[i].n;
				} else {
					cmd->redir.out_path = path_ex;
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

static sb_i32 sh_eval_range(const char *argv0, struct sh_tok *toks, sb_u32 start, sb_u32 end, char **envp, struct sh_ctx *ctx);

static int sh_scan_block_end(struct sh_tok *toks, sb_u32 i, sb_u32 end, const char *kw_end, const char *kw_else, sb_u32 *out_else, sb_u32 *out_end) {
	// Scans for a matching end keyword (fi/done), tracking nested if/while/for.
	// If kw_else is non-null, also reports an else position (only at depth 0).
	sb_u32 depth_if = 0;
	sb_u32 depth_loop = 0;
	sb_u32 else_pos = (sb_u32)-1;
	for (; i < end; i++) {
		if (toks[i].kind != SH_TOK_WORD) continue;
		if (sh_tok_is_word(&toks[i], "if")) {
			depth_if++;
			continue;
		}
		if (sh_tok_is_word(&toks[i], "while") || sh_tok_is_word(&toks[i], "for")) {
			depth_loop++;
			continue;
		}
		if (sh_tok_is_word(&toks[i], "fi")) {
			if (depth_if == 0 && depth_loop == 0 && kw_end && sb_streq(kw_end, "fi")) {
				*out_else = else_pos;
				*out_end = i;
				return 0;
			}
			if (depth_if > 0) depth_if--;
			continue;
		}
		if (sh_tok_is_word(&toks[i], "done")) {
			if (depth_if == 0 && depth_loop == 0 && kw_end && sb_streq(kw_end, "done")) {
				*out_else = else_pos;
				*out_end = i;
				return 0;
			}
			if (depth_loop > 0) depth_loop--;
			continue;
		}
		if (kw_else && sh_tok_is_word(&toks[i], kw_else)) {
			if (depth_if == 0 && depth_loop == 0 && else_pos == (sb_u32)-1) {
				else_pos = i;
			}
			continue;
		}
	}
	return -1;
}

static sb_i32 sh_exec_if(const char *argv0, struct sh_tok *toks, sb_u32 end, sb_u32 *io, int should_run, char **envp, struct sh_ctx *ctx, sb_i32 last_rc) {
	sb_u32 i = *io;
	// toks[i] == "if"
	i++;
	// find "then"
	sb_u32 then_pos = (sb_u32)-1;
	for (sb_u32 j = i; j < end; j++) {
		if (toks[j].kind == SH_TOK_WORD && sb_streq(toks[j].s, "then")) {
			then_pos = j;
			break;
		}
	}
	if (then_pos == (sb_u32)-1) {
		sh_write_err(argv0, "if: missing then");
		return 2;
	}
	sb_u32 else_pos = (sb_u32)-1;
	sb_u32 fi_pos = (sb_u32)-1;
	if (sh_scan_block_end(toks, then_pos + 1, end, "fi", "else", &else_pos, &fi_pos) != 0) {
		sh_write_err(argv0, "if: missing fi");
		return 2;
	}
	*io = fi_pos + 1;
	if (!should_run) {
		return last_rc;
	}

	sb_i32 cond_rc = sh_eval_range(argv0, toks, i, then_pos, envp, ctx);
	if (cond_rc == 0) {
		return sh_eval_range(argv0, toks, then_pos + 1, (else_pos == (sb_u32)-1) ? fi_pos : else_pos, envp, ctx);
	}
	if (else_pos != (sb_u32)-1) {
		return sh_eval_range(argv0, toks, else_pos + 1, fi_pos, envp, ctx);
	}
	return cond_rc;
}

static sb_i32 sh_exec_while(const char *argv0, struct sh_tok *toks, sb_u32 end, sb_u32 *io, int should_run, char **envp, struct sh_ctx *ctx, sb_i32 last_rc) {
	sb_u32 i = *io;
	// toks[i] == "while"
	i++;
	// find "do"
	sb_u32 do_pos = (sb_u32)-1;
	for (sb_u32 j = i; j < end; j++) {
		if (toks[j].kind == SH_TOK_WORD && sb_streq(toks[j].s, "do")) {
			do_pos = j;
			break;
		}
	}
	if (do_pos == (sb_u32)-1) {
		sh_write_err(argv0, "while: missing do");
		return 2;
	}
	sb_u32 ignore_else = (sb_u32)-1;
	sb_u32 done_pos = (sb_u32)-1;
	if (sh_scan_block_end(toks, do_pos + 1, end, "done", 0, &ignore_else, &done_pos) != 0) {
		sh_write_err(argv0, "while: missing done");
		return 2;
	}
	*io = done_pos + 1;
	if (!should_run) {
		return last_rc;
	}

	sb_i32 body_rc = 0;
	int ran = 0;
	for (;;) {
		sb_i32 cond_rc = sh_eval_range(argv0, toks, i, do_pos, envp, ctx);
		if (cond_rc != 0) {
			return ran ? body_rc : cond_rc;
		}
		ran = 1;
		body_rc = sh_eval_range(argv0, toks, do_pos + 1, done_pos, envp, ctx);
	}
}

static sb_i32 sh_exec_for(const char *argv0, struct sh_tok *toks, sb_u32 end, sb_u32 *io, int should_run, char **envp, struct sh_ctx *ctx, sb_i32 last_rc) {
	sb_u32 i = *io;
	// toks[i] == "for"
	i++;
	if (i >= end || toks[i].kind != SH_TOK_WORD) {
		sh_write_err(argv0, "for: missing variable name");
		return 2;
	}
	const char *varname = toks[i].s;
	i++;
	// require "in"
	while (i < end && toks[i].kind == SH_TOK_SEMI) i++;
	if (i >= end || !sh_tok_is_word(&toks[i], "in")) {
		sh_write_err(argv0, "for: expected 'in'");
		return 2;
	}
	i++;
	// scan items until "do"
	sb_u32 do_pos = (sb_u32)-1;
	for (sb_u32 j = i; j < end; j++) {
		if (toks[j].kind == SH_TOK_WORD && sb_streq(toks[j].s, "do")) {
			do_pos = j;
			break;
		}
	}
	if (do_pos == (sb_u32)-1) {
		sh_write_err(argv0, "for: missing do");
		return 2;
	}
	sb_u32 ignore_else = (sb_u32)-1;
	sb_u32 done_pos = (sb_u32)-1;
	if (sh_scan_block_end(toks, do_pos + 1, end, "done", 0, &ignore_else, &done_pos) != 0) {
		sh_write_err(argv0, "for: missing done");
		return 2;
	}
	*io = done_pos + 1;
	if (!should_run) {
		return last_rc;
	}

	// Collect items (WORD tokens) from [i, do_pos).
	const char *items[64];
	sb_u32 nitems = 0;
	for (sb_u32 j = i; j < do_pos; j++) {
		if (toks[j].kind == SH_TOK_SEMI) continue;
		if (toks[j].kind != SH_TOK_WORD) {
			sh_write_err(argv0, "for: bad item list");
			return 2;
		}
		if (nitems >= 64) {
			sh_write_err(argv0, "for: too many items");
			return 2;
		}
		items[nitems++] = toks[j].s;
	}

	sb_i32 rc = 0;
	for (sb_u32 it = 0; it < nitems; it++) {
		if (sh_vars_push(&ctx->vars, varname, items[it], argv0) != 0) return 2;
		rc = sh_eval_range(argv0, toks, do_pos + 1, done_pos, envp, ctx);
		sh_vars_pop(&ctx->vars);
	}
	return rc;
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

static sb_i32 sh_eval_range(const char *argv0, struct sh_tok *toks, sb_u32 start, sb_u32 end, char **envp, struct sh_ctx *ctx) {
	sb_u32 i = start;
	sb_i32 last_rc = 0;
	int next_and = 0;
	int next_or = 0;
	char argbuf[SH_MAX_LINE];
	sb_usize argoff = 0;

	while (i < end) {
		while (i < end && toks[i].kind == SH_TOK_SEMI) i++;
		if (i >= end || toks[i].kind == SH_TOK_END) break;

		int should_run = 1;
		if (next_and) should_run = (last_rc == 0);
		if (next_or) should_run = (last_rc != 0);
		next_and = 0;
		next_or = 0;

		// Reset per-command expansion buffer.
		argoff = 0;

		// Control flow
		if (toks[i].kind == SH_TOK_WORD && sb_streq(toks[i].s, "if")) {
			last_rc = sh_exec_if(argv0, toks, end, &i, should_run, envp, ctx, last_rc);
			sh_consume_sep(toks, end, &i, &next_and, &next_or);
			continue;
		}
		if (toks[i].kind == SH_TOK_WORD && sb_streq(toks[i].s, "while")) {
			last_rc = sh_exec_while(argv0, toks, end, &i, should_run, envp, ctx, last_rc);
			sh_consume_sep(toks, end, &i, &next_and, &next_or);
			continue;
		}
		if (toks[i].kind == SH_TOK_WORD && sb_streq(toks[i].s, "for")) {
			last_rc = sh_exec_for(argv0, toks, end, &i, should_run, envp, ctx, last_rc);
			sh_consume_sep(toks, end, &i, &next_and, &next_or);
			continue;
		}

		// Simple command / pipeline
		struct sh_cmd cmds[SH_MAX_CMDS];
		sb_u32 ncmds = 0;
		if (sh_parse_pipeline(toks, end, &i, cmds, &ncmds, ctx, argbuf, sizeof(argbuf), &argoff, argv0) != 0) {
			while (i < end && toks[i].kind != SH_TOK_SEMI && toks[i].kind != SH_TOK_END) i++;
			last_rc = 2;
			continue;
		}
		sh_consume_sep(toks, end, &i, &next_and, &next_or);
		if (!should_run) continue;

		if (ncmds == 1 && cmds[0].argc > 0) {
			const char *cmd0 = cmds[0].argv[0];
			if (sh_is_builtin(cmd0, "cd")) {
				const char *dir = 0;
				if (cmds[0].argc >= 2) dir = cmds[0].argv[1];
				else dir = sb_getenv_kv(envp, "HOME=");
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
					if (sb_parse_i64_dec(cmds[0].argv[1], &v) != 0) code = 2;
					else code = (sb_i32)v;
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
		// First non-flag is FILE unless -c was used.
		if (cmd_str) {
			break;
		}
		file = a;
		i++;
		break;
	}
	// Remaining argv after FILE (or after -c CMD) become positional parameters.
	int rest_i = i;

	if (cmd_str) {
		static char prog[SH_MAX_PROG];
		sb_usize n = sb_strlen(cmd_str);
		if (n + 1 > sizeof(prog)) {
			sh_write_err(argv0, "-c command too long");
			return 2;
		}
		for (sb_usize j = 0; j <= n; j++) prog[j] = cmd_str[j];
		static char wordbuf[SH_MAX_WORDBUF];
		static struct sh_tok toks[SH_MAX_TOKS];
		sb_u32 ntoks = 0;
		if (sh_tokenize(prog, wordbuf, sizeof(wordbuf), toks, &ntoks, argv0) != 0) return 2;
		struct sh_ctx ctx;
		ctx.vars.n = 0;
		ctx.posc = 0;
		// -c semantics: if extra args exist, first becomes $0, rest are $1..
		if (rest_i < argc && argv[rest_i]) {
			for (; rest_i < argc && argv[rest_i] && ctx.posc < (sb_u32)(sizeof(ctx.posv) / sizeof(ctx.posv[0])); rest_i++) {
				ctx.posv[ctx.posc++] = argv[rest_i];
			}
		}
		return sh_eval_range(argv0, toks, 0, ntoks, envp, &ctx);
	}

	if (file) {
		// Script file mode: read whole file and evaluate as a single token stream.
		sb_i64 ofd = sb_sys_openat(SB_AT_FDCWD, file, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (ofd < 0) {
			sh_print_errno(argv0, "open", ofd);
			return 1;
		}
		sb_i32 fd = (sb_i32)ofd;
		static char prog[SH_MAX_PROG];
		sb_usize off = 0;
		for (;;) {
			if (off + 1 >= sizeof(prog)) {
				sh_write_err(argv0, "script too big");
				(void)sb_sys_close(fd);
				return 2;
			}
			sb_i64 n = sb_sys_read(fd, prog + off, (sb_usize)(sizeof(prog) - off - 1));
			if (n < 0) {
				sh_print_errno(argv0, "read", n);
				(void)sb_sys_close(fd);
				return 1;
			}
			if (n == 0) break;
			off += (sb_usize)n;
		}
		prog[off] = 0;
		(void)sb_sys_close(fd);
		static char wordbuf[SH_MAX_WORDBUF];
		static struct sh_tok toks[SH_MAX_TOKS];
		sb_u32 ntoks = 0;
		if (sh_tokenize(prog, wordbuf, sizeof(wordbuf), toks, &ntoks, argv0) != 0) return 2;
		struct sh_ctx ctx;
		ctx.vars.n = 0;
		ctx.posc = 0;
		// Script semantics: $0 is script path.
		ctx.posv[ctx.posc++] = file;
		for (; rest_i < argc && argv[rest_i] && ctx.posc < (sb_u32)(sizeof(ctx.posv) / sizeof(ctx.posv[0])); rest_i++) {
			ctx.posv[ctx.posc++] = argv[rest_i];
		}
		return sh_eval_range(argv0, toks, 0, ntoks, envp, &ctx);
	}

	// Interactive stdin mode.
	struct sh_reader r = { .fd = 0, .off = 0, .len = 0 };
	char line[SH_MAX_LINE];
	sb_i32 last_rc = 0;
	struct sh_ctx ictx;
	ictx.vars.n = 0;
	ictx.posc = 0;
	for (;;) {
		(void)sb_write_str(1, "$ ");
		sb_usize ln = 0;
		int rr = sh_read_line(&r, line, sizeof(line), &ln);
		if (rr < 0) {
			sh_write_err(argv0, "read error or line too long");
			return 1;
		}
		if (rr == 0) break;
		char *p = line;
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (*p == 0) continue;
		static char wordbuf[SH_MAX_WORDBUF];
		static struct sh_tok toks[SH_MAX_TOKS];
		sb_u32 ntoks = 0;
		if (sh_tokenize(line, wordbuf, sizeof(wordbuf), toks, &ntoks, argv0) != 0) {
			last_rc = 2;
			continue;
		}
		last_rc = sh_eval_range(argv0, toks, 0, ntoks, envp, &ictx);
	}
	return last_rc;
}
