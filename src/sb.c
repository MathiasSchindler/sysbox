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

SB_NORETURN void sb_die_errno(const char *argv0, const char *ctx, sb_i64 err_neg) {
	// Linux returns -errno. Print errno in hex to avoid 64-bit division helpers.
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, ctx);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
	sb_exit(1);
}
