#include "sb.h"

SB_NORETURN void sb_exit(sb_i32 code) {
	(void)sb_syscall1(SB_SYS_exit_group, (sb_i64)code);
	// Fallback if exit_group fails for some reason.
	(void)sb_syscall1(SB_SYS_exit, (sb_i64)code);
	for (;;) {
		__asm__ volatile("hlt");
	}
}

sb_usize sb_strlen(const char *s) {
	const char *p = s;
	while (*p) {
		p++;
	}
	return (sb_usize)(p - s);
}

int sb_streq(const char *a, const char *b) {
	while (*a && *b) {
		if (*a != *b) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

int sb_starts_with_n(const char *s, const char *pre, sb_usize n) {
	for (sb_usize i = 0; i < n; i++) {
		if (s[i] != pre[i]) return 0;
		if (pre[i] == 0) return 0;
	}
	return 1;
}

int sb_has_slash(const char *s) {
	for (const char *p = s; *p; p++) {
		if (*p == '/') return 1;
	}
	return 0;
}

int sb_is_dot_or_dotdot(const char *name) {
	if (!name) return 0;
	if (name[0] != '.') return 0;
	if (name[1] == 0) return 1;
	if (name[1] == '.' && name[2] == 0) return 1;
	return 0;
}

const char *sb_getenv_kv(char **envp, const char *key_eq) {
	if (!envp || !key_eq) return 0;
	sb_usize kn = sb_strlen(key_eq);
	for (sb_usize i = 0; envp[i]; i++) {
		const char *e = envp[i];
		if (!e) continue;
		if (sb_starts_with_n(e, key_eq, kn)) return e + kn;
	}
	return 0;
}

int sb_parse_u64_dec(const char *s, sb_u64 *out) {
	if (!s || !*s || !out) {
		return -1;
	}
	sb_u64 v = 0;
	for (const char *p = s; *p; p++) {
		char c = *p;
		if (c < '0' || c > '9') {
			return -1;
		}
		sb_u64 d = (sb_u64)(c - '0');
		// Overflow check: v*10 + d <= U64_MAX
		if (v > (sb_u64)(~(sb_u64)0) / 10) {
			return -1;
		}
		v *= 10;
		if (v > (sb_u64)(~(sb_u64)0) - d) {
			return -1;
		}
		v += d;
	}
	*out = v;
	return 0;
}

int sb_parse_u32_dec(const char *s, sb_u32 *out) {
	if (!s || !*s || !out) {
		return -1;
	}
	sb_u64 v = 0;
	if (sb_parse_u64_dec(s, &v) != 0) {
		return -1;
	}
	if (v > 0xFFFFFFFFu) {
		return -1;
	}
	*out = (sb_u32)v;
	return 0;
}

int sb_parse_u32_octal(const char *s, sb_u32 *out) {
	if (!s || !*s || !out) {
		return -1;
	}
	sb_u64 v = 0;
	for (const char *p = s; *p; p++) {
		char c = *p;
		if (c < '0' || c > '7') {
			return -1;
		}
		sb_u64 d = (sb_u64)(c - '0');
		// v = v*8 + d, check overflow for u32.
		if (v > 0xFFFFFFFFu / 8u) {
			return -1;
		}
		v = v * 8u + d;
		if (v > 0xFFFFFFFFu) {
			return -1;
		}
	}
	*out = (sb_u32)v;
	return 0;
}

int sb_parse_i64_dec(const char *s, sb_i64 *out) {
	if (!s || !*s || !out) {
		return -1;
	}
	int neg = 0;
	if (*s == '-') {
		neg = 1;
		s++;
		if (!*s) {
			return -1;
		}
	}
	// Parse into unsigned magnitude, then apply sign.
	sb_u64 mag = 0;
	if (sb_parse_u64_dec(s, &mag) != 0) {
		return -1;
	}
	if (!neg) {
		if (mag > (sb_u64)0x7FFFFFFFFFFFFFFFULL) {
			return -1;
		}
		*out = (sb_i64)mag;
		return 0;
	}
	// Allow -9223372036854775808
	if (mag > (sb_u64)0x8000000000000000ULL) {
		return -1;
	}
	if (mag == (sb_u64)0x8000000000000000ULL) {
		*out = (sb_i64)0x8000000000000000ULL;
		return 0;
	}
	*out = -(sb_i64)mag;
	return 0;
}

int sb_parse_i32_dec(const char *s, sb_i32 *out) {
	if (!s || !*s || !out) return -1;
	const char *p = s;
	sb_i64 v = 0;
	if (sb_parse_i64_dec_prefix(&p, &v) != 0) return -1;
	if (*p != 0) return -1;
	if (v < (sb_i64)(-2147483647 - 1) || v > (sb_i64)2147483647) return -1;
	*out = (sb_i32)v;
	return 0;
}

int sb_parse_u64_dec_prefix(const char **ps, sb_u64 *out) {
	if (!ps || !*ps || !out) return -1;
	const char *s = *ps;
	if (*s < '0' || *s > '9') return -1;
	sb_u64 v = 0;
	while (*s >= '0' && *s <= '9') {
		sb_u64 d = (sb_u64)(*s - '0');
		// Overflow check: v*10 + d <= U64_MAX
		if (v > (sb_u64)(~(sb_u64)0) / 10) return -1;
		v *= 10;
		if (v > (sb_u64)(~(sb_u64)0) - d) return -1;
		v += d;
		s++;
	}
	*ps = s;
	*out = v;
	return 0;
}

int sb_parse_u32_dec_prefix(const char **ps, sb_u32 *out) {
	if (!ps || !*ps || !out) return -1;
	const char *s = *ps;
	if (*s < '0' || *s > '9') return -1;
	sb_u32 v = 0;
	while (*s >= '0' && *s <= '9') {
		sb_u32 d = (sb_u32)(*s - '0');
		// Overflow check: v*10 + d <= U32_MAX
		if (v > 0xFFFFFFFFu / 10u) return -1;
		v = v * 10u;
		if (v > 0xFFFFFFFFu - d) return -1;
		v += d;
		s++;
	}
	*ps = s;
	*out = v;
	return 0;
}

int sb_parse_i64_dec_prefix(const char **ps, sb_i64 *out) {
	if (!ps || !*ps || !out) return -1;
	const char *s = *ps;
	int neg = 0;
	if (*s == '+' || *s == '-') {
		neg = (*s == '-');
		s++;
	}
	if (*s < '0' || *s > '9') return -1;
	// Parse into unsigned magnitude, then apply sign.
	sb_u64 mag = 0;
	while (*s >= '0' && *s <= '9') {
		sb_u64 d = (sb_u64)(*s - '0');
		if (mag > (sb_u64)(~(sb_u64)0) / 10) return -1;
		mag *= 10;
		if (mag > (sb_u64)(~(sb_u64)0) - d) return -1;
		mag += d;
		s++;
	}
	if (!neg) {
		if (mag > (sb_u64)0x7FFFFFFFFFFFFFFFULL) return -1;
		*out = (sb_i64)mag;
		*ps = s;
		return 0;
	}
	// Allow -9223372036854775808
	if (mag > (sb_u64)0x8000000000000000ULL) return -1;
	if (mag == (sb_u64)0x8000000000000000ULL) {
		*out = (sb_i64)0x8000000000000000ULL;
		*ps = s;
		return 0;
	}
	*out = -(sb_i64)mag;
	*ps = s;
	return 0;
}

int sb_parse_u32_dec_n(const char *s, sb_usize n, sb_u32 *out) {
	if (!s || n == 0 || !out) return -1;
	sb_u32 v = 0;
	for (sb_usize i = 0; i < n; i++) {
		char c = s[i];
		if (c < '0' || c > '9') return -1;
		sb_u32 d = (sb_u32)(c - '0');
		if (v > 0xFFFFFFFFu / 10u) return -1;
		v = v * 10u;
		if (v > 0xFFFFFFFFu - d) return -1;
		v += d;
	}
	*out = v;
	return 0;
}

int sb_parse_uid_gid(const char *s, sb_u32 *out_uid, sb_u32 *out_gid) {
	if (!s || !*s || !out_uid || !out_gid) {
		return -1;
	}

	// Format: UID[:GID]
	const char *colon = 0;
	for (const char *p = s; *p; p++) {
		if (*p == ':') {
			colon = p;
			break;
		}
	}

	if (!colon) {
		// UID only; keep gid unchanged by passing -1 (kernel uses (uid_t)-1 / (gid_t)-1).
		sb_u32 uid;
		if (sb_parse_u32_dec(s, &uid) != 0) return -1;
		*out_uid = uid;
		*out_gid = 0xFFFFFFFFu;
		return 0;
	}

	// Split into left and right without allocating.
	// Parse UID from [s, colon)
	{
		sb_u64 v = 0;
		if (colon == s) return -1;
		for (const char *p = s; p < colon; p++) {
			char c = *p;
			if (c < '0' || c > '9') return -1;
			sb_u64 d = (sb_u64)(c - '0');
			if (v > (sb_u64)(~(sb_u64)0) / 10) return -1;
			v = v * 10 + d;
			if (v > 0xFFFFFFFFu) return -1;
		}
		*out_uid = (sb_u32)v;
	}

	// Parse GID from (colon+1)
	if (*(colon + 1) == 0) {
		// UID: (empty) => leave gid unchanged
		*out_gid = 0xFFFFFFFFu;
		return 0;
	}
	{
		sb_u32 gid;
		if (sb_parse_u32_dec(colon + 1, &gid) != 0) return -1;
		*out_gid = gid;
	}
	return 0;
}

sb_i64 sb_for_each_dirent(sb_i32 dirfd, sb_dirent_cb cb, void *ctx) {
	if (dirfd < 0 || !cb) return (sb_i64)-SB_EINVAL;

	sb_u8 buf[32768];
	for (;;) {
		sb_i64 nread = sb_sys_getdents64(dirfd, buf, (sb_u32)sizeof(buf));
		if (nread < 0) return nread;
		if (nread == 0) return 0;

		sb_u32 bpos = 0;
		while (bpos < (sb_u32)nread) {
			struct sb_linux_dirent64 *d = (struct sb_linux_dirent64 *)(buf + bpos);
			if (d->d_reclen == 0) return (sb_i64)-SB_EINVAL;
			const char *name = d->d_name;
			if (!sb_is_dot_or_dotdot(name)) {
				int rc = cb(ctx, name, d->d_type);
				if (rc != 0) return 0;
			}
			bpos += d->d_reclen;
		}
	}
}

sb_i64 sb_write_all(sb_i32 fd, const void *buf, sb_usize len) {
	const sb_u8 *p = (const sb_u8 *)buf;
	sb_usize off = 0;
	while (off < len) {
		sb_i64 r = sb_sys_write(fd, p + off, len - off);
		if (r < 0) {
			return r;
		}
		if (r == 0) {
			return -1;
		}
		off += (sb_usize)r;
	}
	return 0;
}

sb_i64 sb_write_str(sb_i32 fd, const char *s) {
	return sb_write_all(fd, s, sb_strlen(s));
}

static void sb_write_hex_nibble(sb_i32 fd, sb_u8 v) {
	char c;
	v &= 0xF;
	c = (v < 10) ? (char)('0' + v) : (char)('a' + (v - 10));
	(void)sb_write_all(fd, &c, 1);
}

void sb_write_hex_u64(sb_i32 fd, sb_u64 v) {
	// Print as 0x... without using division.
	(void)sb_write_str(fd, "0x");
	int started = 0;
	for (int shift = 60; shift >= 0; shift -= 4) {
		sb_u8 nib = (sb_u8)((v >> (sb_u64)shift) & 0xFULL);
		if (!started) {
			if (nib == 0 && shift != 0) {
				continue;
			}
			started = 1;
		}
		sb_write_hex_nibble(fd, nib);
	}
}

sb_i64 sb_write_u64_dec(sb_i32 fd, sb_u64 v) {
	char buf[32];
	sb_usize n = 0;
	if (v == 0) {
		char c = '0';
		return sb_write_all(fd, &c, 1);
	}
	while (v != 0) {
		sb_u64 q = v / 10;
		sb_u64 r = v - q * 10;
		buf[n++] = (char)('0' + (char)r);
		v = q;
	}
	// reverse
	for (sb_usize i = 0; i < n / 2; i++) {
		char t = buf[i];
		buf[i] = buf[n - 1 - i];
		buf[n - 1 - i] = t;
	}
	return sb_write_all(fd, buf, n);
}

sb_i64 sb_write_i64_dec(sb_i32 fd, sb_i64 v) {
	if (v < 0) {
		sb_i64 r = sb_write_all(fd, "-", 1);
		if (r < 0) return r;
		// Handle INT64_MIN without overflow.
		sb_u64 mag = (v == (sb_i64)0x8000000000000000ULL) ? (sb_u64)0x8000000000000000ULL : (sb_u64)(-v);
		return sb_write_u64_dec(fd, mag);
	}
	return sb_write_u64_dec(fd, (sb_u64)v);
}

SB_NORETURN void sb_die_usage(const char *argv0, const char *usage) {
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": usage: ");
	(void)sb_write_str(2, usage);
	(void)sb_write_str(2, "\n");
	sb_exit(2);
}

static void sb_write_errno_line(const char *argv0, const char *ctx, sb_i64 err_neg) {
	// Linux returns -errno. Print errno in hex to avoid 64-bit division helpers.
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, ctx);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

SB_NORETURN void sb_die_errno(const char *argv0, const char *ctx, sb_i64 err_neg) {
	sb_write_errno_line(argv0, ctx, err_neg);
	sb_exit(1);
}

void sb_print_errno(const char *argv0, const char *ctx, sb_i64 err_neg) {
	sb_write_errno_line(argv0, ctx, err_neg);
}

void sb_join_path_or_die(const char *argv0, const char *base, const char *name, char *out, sb_usize out_cap) {
	sb_usize blen = sb_strlen(base);
	sb_usize nlen = sb_strlen(name);
	int need_slash = 1;

	if (blen == 0 || (blen == 1 && base[0] == '.')) {
		if (nlen + 1 > out_cap) sb_die_errno(argv0, "path", (sb_i64)-SB_EINVAL);
		for (sb_usize i = 0; i < nlen; i++) out[i] = name[i];
		out[nlen] = 0;
		return;
	}
	if (blen > 0 && base[blen - 1] == '/') need_slash = 0;

	sb_usize total = blen + (need_slash ? 1u : 0u) + nlen;
	if (total + 1 > out_cap) sb_die_errno(argv0, "path", (sb_i64)-SB_EINVAL);
	for (sb_usize i = 0; i < blen; i++) out[i] = base[i];
	sb_usize off = blen;
	if (need_slash) out[off++] = '/';
	for (sb_usize i = 0; i < nlen; i++) out[off + i] = name[i];
	out[total] = 0;
}

// --- Tiny regex matcher (BRE-ish subset) used by grep/sed.

enum sb_re_atom_kind {
	SB_RE_ATOM_LITERAL = 0,
	SB_RE_ATOM_DOT,
	SB_RE_ATOM_CLASS,
	SB_RE_ATOM_GROUP_START,
	SB_RE_ATOM_GROUP_END,
};

static int sb_re_is_group_start(const char *re) {
	return re && re[0] == '\\' && re[1] == '(';
}

static int sb_re_is_group_end(const char *re) {
	return re && re[0] == '\\' && re[1] == ')';
}

static int sb_re_parse_class_len(const char *re) {
	// re[0] == '['
	const char *p = re + 1;
	if (*p == 0) return -1;
	// Allow ']' as first literal member.
	if (*p == ']') p++;
	for (; *p; p++) {
		if (*p == '\\' && p[1] != 0) {
			p++;
			continue;
		}
		if (*p == ']') {
			return (int)((p - re) + 1);
		}
	}
	return -1;
}

static int sb_re_atom_len_and_kind(const char *re, enum sb_re_atom_kind *out_kind) {
	if (!re || !*re) return 0;
	if (re[0] == '\\' && re[1] != 0) {
		if (re[1] == '(') {
			*out_kind = SB_RE_ATOM_GROUP_START;
			return 2;
		}
		if (re[1] == ')') {
			*out_kind = SB_RE_ATOM_GROUP_END;
			return 2;
		}
		*out_kind = SB_RE_ATOM_LITERAL;
		return 2;
	}
	if (re[0] == '.') {
		*out_kind = SB_RE_ATOM_DOT;
		return 1;
	}
	if (re[0] == '[') {
		int n = sb_re_parse_class_len(re);
		if (n < 0) return -1;
		*out_kind = SB_RE_ATOM_CLASS;
		return n;
	}
	*out_kind = SB_RE_ATOM_LITERAL;
	return 1;
}

static sb_u8 sb_re_fold(sb_u8 c, sb_u32 flags) {
	if (flags & SB_REGEX_ICASE) return sb_tolower_ascii(c);
	return c;
}

static int sb_re_class_matches(const char *cls, int len, sb_u8 ch, sb_u32 flags, int *out_invalid) {
	// cls points to '[', len includes trailing ']'.
	*out_invalid = 0;
	if (!cls || len < 2 || cls[0] != '[' || cls[len - 1] != ']') {
		*out_invalid = 1;
		return 0;
	}
	const char *p = cls + 1;
	const char *end = cls + len - 1;
	int neg = 0;
	if (p < end && *p == '^') {
		neg = 1;
		p++;
	}
	// Treat initial ']' as literal.
	int matched = 0;
	sb_u8 cch = sb_re_fold(ch, flags);
	if (p < end && *p == ']') {
		matched = (cch == sb_re_fold((sb_u8)']', flags));
		p++;
	}

	while (p < end) {
		sb_u8 a;
		if (*p == '\\' && (p + 1) < end) {
			a = (sb_u8)p[1];
			p += 2;
		} else {
			a = (sb_u8)(*p++);
		}
		a = sb_re_fold(a, flags);

		if (p < end && *p == '-' && (p + 1) < end && p[1] != ']') {
			p++; // consume '-'
			sb_u8 b;
			if (*p == '\\' && (p + 1) < end) {
				b = (sb_u8)p[1];
				p += 2;
			} else {
				b = (sb_u8)(*p++);
			}
			b = sb_re_fold(b, flags);
			sb_u8 lo = a < b ? a : b;
			sb_u8 hi = a < b ? b : a;
			if (cch >= lo && cch <= hi) matched = 1;
			continue;
		}

		if (cch == a) matched = 1;
	}

	return neg ? !matched : matched;
}

static int sb_re_atom_matches(const char *atom, int atom_len, enum sb_re_atom_kind kind, sb_u8 ch, sb_u32 flags, int *out_invalid) {
	*out_invalid = 0;
	if (kind == SB_RE_ATOM_DOT) {
		return ch != 0;
	}
	if (kind == SB_RE_ATOM_CLASS) {
		return sb_re_class_matches(atom, atom_len, ch, flags, out_invalid);
	}
	if (kind == SB_RE_ATOM_LITERAL) {
		sb_u8 a;
		if (atom_len == 2 && atom[0] == '\\') {
			a = (sb_u8)atom[1];
		} else {
			a = (sb_u8)atom[0];
		}
		return sb_re_fold(ch, flags) == sb_re_fold(a, flags);
	}
	*out_invalid = 1;
	return 0;
}

static int sb_re_match_here(const char *re, const char *text, sb_u32 flags, int stop_on_group_end,
	const char **out_re, const char **out_text, struct sb_regex_caps *caps, sb_u32 nextcap, sb_u32 *out_nextcap);

static int sb_re_match_star(const char *atom, int atom_len, enum sb_re_atom_kind kind, const char *re_rest, const char *text, sb_u32 flags,
	int stop_on_group_end, const char **out_re, const char **out_text, struct sb_regex_caps *caps, sb_u32 nextcap, sb_u32 *out_nextcap) {
	// Greedy then backtrack.
	const char *t = text;
	while (*t) {
		int inv = 0;
		if (!sb_re_atom_matches(atom, atom_len, kind, (sb_u8)*t, flags, &inv)) {
			if (inv) return -1;
			break;
		}
		t++;
	}
	for (;;) {
		struct sb_regex_caps caps_snap = *caps;
		sb_u32 nc_snap = nextcap;
		int r = sb_re_match_here(re_rest, t, flags, stop_on_group_end, out_re, out_text, &caps_snap, nc_snap, &nc_snap);
		if (r == 1) {
			*caps = caps_snap;
			*out_nextcap = nc_snap;
			return 1;
		}
		if (r < 0) return r;
		if (t == text) break;
		t--;
	}
	return 0;
}

static int sb_re_match_here(const char *re, const char *text, sb_u32 flags, int stop_on_group_end,
	const char **out_re, const char **out_text, struct sb_regex_caps *caps, sb_u32 nextcap, sb_u32 *out_nextcap) {
	if (!re || !text || !out_re || !out_text || !caps || !out_nextcap) return -1;

	if (stop_on_group_end) {
		if (*re == 0) return -1; // unterminated group
		if (sb_re_is_group_end(re)) {
			*out_re = re;
			*out_text = text;
			*out_nextcap = nextcap;
			return 1;
		}
	} else {
		if (sb_re_is_group_end(re)) return -1; // stray \)
	}

	if (*re == 0) {
		*out_re = re;
		*out_text = text;
		*out_nextcap = nextcap;
		return 1;
	}

	// $ at end
	if (!stop_on_group_end && re[0] == '$' && re[1] == 0) {
		if (*text == 0) {
			*out_re = re + 1;
			*out_text = text;
			*out_nextcap = nextcap;
			return 1;
		}
		return 0;
	}

	// Group
	if (sb_re_is_group_start(re)) {
		if (nextcap >= SB_REGEX_MAX_CAPS) return -1;
		sb_u32 cap_id = nextcap + 1;
		struct sb_regex_caps caps_snap = *caps;
		caps_snap.start[cap_id] = text;

		const char *inner_re = 0;
		const char *inner_text = 0;
		sb_u32 nc_after = cap_id;
		int r = sb_re_match_here(re + 2, text, flags, 1, &inner_re, &inner_text, &caps_snap, cap_id, &nc_after);
		if (r != 1) return r;
		if (!sb_re_is_group_end(inner_re)) return -1;
		caps_snap.end[cap_id] = inner_text;
		if (caps_snap.n < cap_id) caps_snap.n = cap_id;

		const char *after_re = 0;
		const char *after_text = 0;
		sb_u32 nc_final = nc_after;
		r = sb_re_match_here(inner_re + 2, inner_text, flags, stop_on_group_end, &after_re, &after_text, &caps_snap, nc_final, &nc_final);
		if (r == 1) {
			*caps = caps_snap;
			*out_re = after_re;
			*out_text = after_text;
			*out_nextcap = nc_final;
		}
		return r;
	}

	enum sb_re_atom_kind kind;
	int atom_len = sb_re_atom_len_and_kind(re, &kind);
	if (atom_len < 0) return -1;
	if (atom_len == 0) {
		*out_re = re;
		*out_text = text;
		*out_nextcap = nextcap;
		return 1;
	}
	if (kind == SB_RE_ATOM_GROUP_END) {
		return stop_on_group_end ? 1 : -1;
	}
	if (kind == SB_RE_ATOM_GROUP_START) return -1;

	if (re[atom_len] == '*') {
		return sb_re_match_star(re, atom_len, kind, re + atom_len + 1, text, flags, stop_on_group_end, out_re, out_text, caps, nextcap, out_nextcap);
	}

	if (*text) {
		int inv = 0;
		if (sb_re_atom_matches(re, atom_len, kind, (sb_u8)*text, flags, &inv)) {
			return sb_re_match_here(re + atom_len, text + 1, flags, stop_on_group_end, out_re, out_text, caps, nextcap, out_nextcap);
		}
		if (inv) return -1;
	}
	return 0;
}

int sb_regex_match_first(const char *re, const char *text, sb_u32 flags, const char **out_start, const char **out_end, struct sb_regex_caps *out_caps) {
	if (!re || !text || !out_start || !out_end) return -1;

	struct sb_regex_caps caps;
	for (sb_u32 i = 0; i <= SB_REGEX_MAX_CAPS; i++) {
		caps.start[i] = 0;
		caps.end[i] = 0;
	}
	caps.n = 0;

	const char *match_end = 0;
	const char *dummy_re = 0;
	if (re[0] == '^') {
		sb_u32 nc = 0;
		int r = sb_re_match_here(re + 1, text, flags, 0, &dummy_re, &match_end, &caps, 0, &nc);
		if (r < 0) return -1;
		if (r == 1) {
			*out_start = text;
			*out_end = match_end;
			if (out_caps) *out_caps = caps;
			return 1;
		}
		return 0;
	}

	for (const char *t = text;; t++) {
		struct sb_regex_caps caps_try = caps;
		sb_u32 nc = 0;
		int r = sb_re_match_here(re, t, flags, 0, &dummy_re, &match_end, &caps_try, 0, &nc);
		if (r < 0) return -1;
		if (r == 1) {
			*out_start = t;
			*out_end = match_end;
			if (out_caps) *out_caps = caps_try;
			return 1;
		}
		if (*t == 0) break;
	}
	return 0;
}
