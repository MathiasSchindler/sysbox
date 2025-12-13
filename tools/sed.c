#include "../src/sb.h"

// Minimal sed subset:
// - Commands:
//   - s/REGEX/REPL/[g][p]
//   - d
// - Addressing (minimal): Ncmd or $cmd (e.g. 2d, $d, 3s/a/b/)
// - Regex: literals + ., *, ^, $, and backslash-escape for literals.
// - Options: -n (suppress default printing), -e SCRIPT (repeatable), -- end of options.
// - Input: FILE... or stdin.

#define SED_MAX_CMDS 16

static int sed_is_special(char c) {
	return c == '.' || c == '*' || c == '^' || c == '$' || c == '\\';
}

static void sed_print_err(const char *argv0, const char *ctx, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, ctx);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static sb_usize sed_cstr_len(const char *s) {
	return sb_strlen(s);
}

static void sed_memcpy(char *dst, const char *src, sb_usize n) {
	for (sb_usize i = 0; i < n; i++) dst[i] = src[i];
}

static int sed_streq(const char *a, const char *b) {
	return sb_streq(a, b);
}

// --- Tiny regex matcher (K&R style) that returns match end pointer.

static int sed_token_len(const char *re) {
	if (!re || !*re) return 0;
	if (re[0] == '\\' && re[1] != 0) return 2;
	return 1;
}

static int sed_token_matches(const char *token, char t) {
	if (!token || !*token) return 0;
	if (token[0] == '.') {
		return t != 0;
	}
	if (token[0] == '\\' && token[1] != 0) {
		return t == token[1];
	}
	// literal
	if (sed_is_special(token[0])) {
		// Special chars match literally only if escaped; otherwise they keep semantics.
		// If we see them here, treat as literal.
	}
	return t == token[0];
}

static int sed_matchhere(const char *re, const char *text, const char **out_end);

static int sed_matchstar(const char *token, const char *re_rest, const char *text, const char **out_end) {
	// token* re_rest: match zero or more tokens, then rest.
	const char *t = text;
	// Greedy: advance as far as we can.
	while (*t && sed_token_matches(token, *t)) {
		t++;
	}
	// Backtrack.
	for (;;) {
		if (sed_matchhere(re_rest, t, out_end)) return 1;
		if (t == text) break;
		t--;
	}
	return 0;
}

static int sed_matchhere(const char *re, const char *text, const char **out_end) {
	if (!re || re[0] == 0) {
		*out_end = text;
		return 1;
	}
	// $ at end
	if (re[0] == '$' && re[1] == 0) {
		if (*text == 0) {
			*out_end = text;
			return 1;
		}
		return 0;
	}

	int tlen = sed_token_len(re);
	if (tlen == 0) {
		*out_end = text;
		return 1;
	}

	// token followed by *
	if (re[tlen] == '*') {
		return sed_matchstar(re, re + tlen + 1, text, out_end);
	}

	if (*text && sed_token_matches(re, *text)) {
		return sed_matchhere(re + tlen, text + 1, out_end);
	}
	return 0;
}

static int sed_match_first(const char *re, const char *text, const char **out_start, const char **out_end) {
	if (!re || !text || !out_start || !out_end) return 0;
	if (re[0] == '^') {
		if (sed_matchhere(re + 1, text, out_end)) {
			*out_start = text;
			return 1;
		}
		return 0;
	}

	for (const char *t = text; ; t++) {
		if (sed_matchhere(re, t, out_end)) {
			*out_start = t;
			return 1;
		}
		if (*t == 0) break;
	}
	return 0;
}

// --- Parsing s/// command

struct sed_subst {
	char regex[512];
	char repl[512];
	int global;
	int print_on_subst;
};

enum sed_addr_kind {
	SED_ADDR_NONE = 0,
	SED_ADDR_LINE,
	SED_ADDR_LAST,
};

enum sed_cmd_kind {
	SED_CMD_SUBST = 0,
	SED_CMD_DEL,
};

struct sed_cmd {
	enum sed_cmd_kind kind;
	enum sed_addr_kind addr_kind;
	sb_u64 addr_line;
	struct sed_subst subst;
};

static int sed_parse_until_delim(const char *s, char delim, char *out, sb_usize out_sz, const char **out_next) {
	sb_usize o = 0;
	const char *p = s;
	while (*p) {
		char c = *p;
		if (c == delim) {
			out[o] = 0;
			*out_next = p + 1;
			return 0;
		}
		if (c == '\\' && p[1] != 0) {
			// Keep escapes as-is; matcher/repl emitter interpret them.
			if (o + 2 >= out_sz) return -1;
			out[o++] = *p++;
			out[o++] = *p++;
			continue;
		}
		if (o + 1 >= out_sz) return -1;
		out[o++] = c;
		p++;
	}
	return -1;
}


static int sed_parse_subst(const char *script, struct sed_subst *out) {
	if (!script || !*script || !out) return -1;
	if (script[0] != 's') return -1;
	char delim = script[1];
	if (delim == 0) return -1;

	const char *p = script + 2;
	const char *next = 0;
	if (sed_parse_until_delim(p, delim, out->regex, sizeof(out->regex), &next) != 0) return -1;
	if (out->regex[0] == 0) return -1;
	p = next;
	if (sed_parse_until_delim(p, delim, out->repl, sizeof(out->repl), &next) != 0) return -1;
	p = next;

	out->global = 0;
	out->print_on_subst = 0;
	for (; *p; p++) {
		if (*p == 'g') {
			out->global = 1;
			continue;
		}
		if (*p == 'p') {
			out->print_on_subst = 1;
			continue;
		}
		return -1;
	}
	return 0;
}

static int sed_is_digit(char c) {
	return (c >= '0' && c <= '9');
}

static int sed_parse_addr(const char *s, enum sed_addr_kind *out_kind, sb_u64 *out_line, const char **out_next) {
	*out_kind = SED_ADDR_NONE;
	*out_line = 0;
	*out_next = s;
	if (!s || !*s) return 0;
	if (*s == '$') {
		*out_kind = SED_ADDR_LAST;
		*out_next = s + 1;
		return 0;
	}
	if (!sed_is_digit(*s)) {
		return 0;
	}
	sb_u64 v = 0;
	const char *p = s;
	while (*p && sed_is_digit(*p)) {
		v = v * 10u + (sb_u64)(*p - '0');
		p++;
	}
	*out_kind = SED_ADDR_LINE;
	*out_line = v;
	*out_next = p;
	return 0;
}

static int sed_parse_cmd(const char *script, struct sed_cmd *out) {
	if (!script || !*script || !out) return -1;

	enum sed_addr_kind ak;
	sb_u64 al;
	const char *p = 0;
	if (sed_parse_addr(script, &ak, &al, &p) != 0) return -1;

	if (!p || !*p) return -1;
	out->addr_kind = ak;
	out->addr_line = al;

	if (*p == 'd' && p[1] == 0) {
		out->kind = SED_CMD_DEL;
		return 0;
	}
	if (*p == 's') {
		out->kind = SED_CMD_SUBST;
		if (sed_parse_subst(p, &out->subst) != 0) return -1;
		return 0;
	}
	return -1;
}

static int sed_emit_repl(char *dst, sb_usize dst_sz, sb_usize *ioff, const char *repl, const char *mstart, const char *mend) {
	// Expand:
	// - & => whole match
	// - \X => literal X (including \&)
	// - \n => newline
	const char *p = repl;
	while (*p) {
		char c = *p++;
		if (c == '&') {
			sb_usize n = (sb_usize)(mend - mstart);
			if (*ioff + n >= dst_sz) return -1;
			sed_memcpy(dst + *ioff, mstart, n);
			*ioff += n;
			continue;
		}
		if (c == '\\' && *p) {
			char e = *p++;
			if (e == 'n') {
				e = '\n';
			}
			if (*ioff + 1 >= dst_sz) return -1;
			dst[(*ioff)++] = e;
			continue;
		}
		if (*ioff + 1 >= dst_sz) return -1;
		dst[(*ioff)++] = c;
	}
	return 0;
}

static int sed_apply_subst(const struct sed_subst *sc, const char *in, char *out, sb_usize out_sz, int *did_subst) {
	*did_subst = 0;
	// in is a NUL-terminated line without trailing newline.
	const char *pos = in;
	char *w = out;
	sb_usize woff = 0;

	for (;;) {
		const char *mstart = 0;
		const char *mend = 0;
		if (!sed_match_first(sc->regex, pos, &mstart, &mend)) {
			// Copy remainder.
			sb_usize n = sed_cstr_len(pos);
			if (woff + n + 1 > out_sz) return -1;
			sed_memcpy(w + woff, pos, n);
			woff += n;
			break;
		}

		*did_subst = 1;

		// Copy prefix [pos, mstart)
		sb_usize pre = (sb_usize)(mstart - pos);
		if (woff + pre + 1 > out_sz) return -1;
		sed_memcpy(w + woff, pos, pre);
		woff += pre;

		// Emit replacement.
		if (sed_emit_repl(w, out_sz, &woff, sc->repl, mstart, mend) != 0) return -1;

		// Advance.
		pos = mend;

		if (!sc->global) {
			// Copy remainder.
			sb_usize n = sed_cstr_len(pos);
			if (woff + n + 1 > out_sz) return -1;
			sed_memcpy(w + woff, pos, n);
			woff += n;
			break;
		}

		// Avoid infinite loops on empty matches.
		if (mend == mstart) {
			if (*pos == 0) break;
			if (woff + 1 + 1 > out_sz) return -1;
			w[woff++] = *pos;
			pos++;
		}
	}

	w[woff] = 0;
	return 0;
}

static int sed_cmd_addr_matches(const struct sed_cmd *cmd, sb_u64 lineno, int is_last) {
	if (!cmd) return 0;
	if (cmd->addr_kind == SED_ADDR_NONE) return 1;
	if (cmd->addr_kind == SED_ADDR_LINE) return lineno == cmd->addr_line;
	if (cmd->addr_kind == SED_ADDR_LAST) return is_last;
	return 0;
}

static int sed_print_line(const char *argv0, const char *s, int has_nl) {
	sb_i64 w = sb_write_str(1, s);
	if (w < 0) sb_die_errno(argv0, "write", w);
	if (has_nl) {
		w = sb_write_all(1, "\n", 1);
		if (w < 0) sb_die_errno(argv0, "write", w);
	}
	return 0;
}

static int sed_process_line(const char *argv0, const struct sed_cmd *cmds, sb_u32 ncmds, int suppress_default, const char *line, sb_usize len, sb_u64 lineno, int is_last, int *any_fail) {
	(void)any_fail;
	// Separate trailing newline (if present).
	int has_nl = 0;
	if (len > 0 && line[len - 1] == '\n') {
		has_nl = 1;
		len--;
	}

	// Copy into a NUL-terminated buffer.
	char pat[65536];
	if (len >= sizeof(pat)) {
		(void)sb_write_str(2, argv0);
		(void)sb_write_str(2, ": line too long\n");
		return -1;
	}
	for (sb_usize i = 0; i < len; i++) pat[i] = line[i];
	pat[len] = 0;

	char tmp[262144];
	int deleted = 0;
	int printed = 0;

	for (sb_u32 ci = 0; ci < ncmds; ci++) {
		const struct sed_cmd *cmd = &cmds[ci];
		if (!sed_cmd_addr_matches(cmd, lineno, is_last)) {
			continue;
		}
		if (cmd->kind == SED_CMD_DEL) {
			deleted = 1;
			break;
		}
		if (cmd->kind == SED_CMD_SUBST) {
			int did = 0;
			if (sed_apply_subst(&cmd->subst, pat, tmp, sizeof(tmp), &did) != 0) {
				(void)sb_write_str(2, argv0);
				(void)sb_write_str(2, ": output too long\n");
				return -1;
			}
			// Update pattern space.
			sb_usize tlen = sed_cstr_len(tmp);
			if (tlen >= sizeof(pat)) {
				(void)sb_write_str(2, argv0);
				(void)sb_write_str(2, ": output too long\n");
				return -1;
			}
			for (sb_usize k = 0; k <= tlen; k++) pat[k] = tmp[k];

			if (did && cmd->subst.print_on_subst) {
				printed = 1;
				(void)sed_print_line(argv0, pat, has_nl);
			}
			continue;
		}
	}

	if (!deleted && !suppress_default && !printed) {
		(void)sed_print_line(argv0, pat, has_nl);
	}

	return 0;
}

static int sed_process_fd(const char *argv0, const struct sed_cmd *cmds, sb_u32 ncmds, int suppress_default, sb_i32 fd, int *any_fail) {
	char rbuf[32768];
	sb_usize rpos = 0;
	sb_usize rlen = 0;

	char linebuf[65536];
	sb_usize llen = 0;

	char prev[65536];
	sb_usize prev_len = 0;
	int have_prev = 0;
	sb_u64 lineno = 0;

	for (;;) {
		if (rpos == rlen) {
			sb_i64 r = sb_sys_read(fd, rbuf, sizeof(rbuf));
			if (r < 0) {
				sed_print_err(argv0, "read", r);
				*any_fail = 1;
				return -1;
			}
			if (r == 0) break;
			rpos = 0;
			rlen = (sb_usize)r;
		}

		char c = rbuf[rpos++];
		if (llen + 1 >= sizeof(linebuf)) {
			(void)sb_write_str(2, argv0);
			(void)sb_write_str(2, ": line too long\n");
			*any_fail = 1;
			return -1;
		}
		linebuf[llen++] = c;
		if (c == '\n') {
			if (have_prev) {
				lineno++;
				if (sed_process_line(argv0, cmds, ncmds, suppress_default, prev, prev_len, lineno, 0, any_fail) != 0) {
					*any_fail = 1;
					return -1;
				}
			}
			// move current to prev
			if (llen >= sizeof(prev)) {
				(void)sb_write_str(2, argv0);
				(void)sb_write_str(2, ": line too long\n");
				*any_fail = 1;
				return -1;
			}
			for (sb_usize i = 0; i < llen; i++) prev[i] = linebuf[i];
			prev_len = llen;
			have_prev = 1;
			llen = 0;
		}
	}

	// If there's a final partial line (no newline), make it prev and process it as last.
	if (llen > 0) {
		if (have_prev) {
			lineno++;
			if (sed_process_line(argv0, cmds, ncmds, suppress_default, prev, prev_len, lineno, 0, any_fail) != 0) {
				*any_fail = 1;
				return -1;
			}
		}
		for (sb_usize i = 0; i < llen; i++) prev[i] = linebuf[i];
		prev_len = llen;
		have_prev = 1;
		llen = 0;
	}

	if (have_prev) {
		lineno++;
		if (sed_process_line(argv0, cmds, ncmds, suppress_default, prev, prev_len, lineno, 1, any_fail) != 0) {
			*any_fail = 1;
			return -1;
		}
	}

	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "sed";

	int suppress_default = 0; // -n
	struct sed_cmd cmds[SED_MAX_CMDS];
	sb_u32 ncmds = 0;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!a) break;
		if (sed_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] != '-') break;
		if (sed_streq(a, "-n")) {
			suppress_default = 1;
			continue;
		}
		if (sed_streq(a, "-e")) {
			i++;
			if (i >= argc || !argv[i]) sb_die_usage(argv0, "sed [-n] [-e SCRIPT] [SCRIPT] [FILE...]");
			if (ncmds >= SED_MAX_CMDS) sb_die_usage(argv0, "sed [-n] [-e SCRIPT] [SCRIPT] [FILE...]");
			if (sed_parse_cmd(argv[i], &cmds[ncmds]) != 0) {
				sb_die_usage(argv0, "sed [-n] [-e SCRIPT] [SCRIPT] [FILE...] (only s/REGEX/REPL/[gp] and d supported)");
			}
			ncmds++;
			continue;
		}
		// Unknown option.
		sb_die_usage(argv0, "sed [-n] [-e SCRIPT] [SCRIPT] [FILE...]");
	}

	if (ncmds == 0) {
		if (i >= argc || !argv[i]) {
			sb_die_usage(argv0, "sed [-n] [-e SCRIPT] [SCRIPT] [FILE...]");
		}
		if (sed_parse_cmd(argv[i], &cmds[ncmds]) != 0) {
			sb_die_usage(argv0, "sed [-n] [-e SCRIPT] [SCRIPT] [FILE...] (only s/REGEX/REPL/[gp] and d supported)");
		}
		ncmds++;
		i++;
	}

	int any_fail = 0;
	if (i >= argc) {
		// stdin
		(void)sed_process_fd(argv0, cmds, ncmds, suppress_default, 0, &any_fail);
		return any_fail ? 1 : 0;
	}

	for (; i < argc; i++) {
		const char *path = argv[i];
		if (!path) continue;
		if (sed_streq(path, "-")) {
			(void)sed_process_fd(argv0, cmds, ncmds, suppress_default, 0, &any_fail);
			continue;
		}
		sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
		if (fd < 0) {
			sed_print_err(argv0, path, fd);
			any_fail = 1;
			continue;
		}
		(void)sed_process_fd(argv0, cmds, ncmds, suppress_default, (sb_i32)fd, &any_fail);
		(void)sb_sys_close((sb_i32)fd);
	}

	return any_fail ? 1 : 0;
}
