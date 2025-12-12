#include "../src/sb.h"

#define CHMOD_CLASS_U 0x1
#define CHMOD_CLASS_G 0x2
#define CHMOD_CLASS_O 0x4

static int chmod_is_end(char c) {
	return (c == '\0' || c == ',');
}

static int chmod_parse_classes(const char **ps, sb_u32 *out_classes) {
	const char *s = *ps;
	sb_u32 classes = 0;
	for (;;) {
		char c = *s;
		if (c == 'u') {
			classes |= CHMOD_CLASS_U;
			s++;
			continue;
		}
		if (c == 'g') {
			classes |= CHMOD_CLASS_G;
			s++;
			continue;
		}
		if (c == 'o') {
			classes |= CHMOD_CLASS_O;
			s++;
			continue;
		}
		if (c == 'a') {
			classes |= (CHMOD_CLASS_U | CHMOD_CLASS_G | CHMOD_CLASS_O);
			s++;
			continue;
		}
		break;
	}
	*ps = s;
	*out_classes = classes;
	return 0;
}

static int chmod_parse_perms(const char **ps, sb_u32 *out_perm_bits, int *out_had_any) {
	const char *s = *ps;
	sb_u32 bits = 0;
	int had_any = 0;
	for (;;) {
		char c = *s;
		if (chmod_is_end(c) || c == '\0') {
			break;
		}
		if (c == 'r') {
			bits |= 4;
			had_any = 1;
			s++;
			continue;
		}
		if (c == 'w') {
			bits |= 2;
			had_any = 1;
			s++;
			continue;
		}
		if (c == 'x') {
			bits |= 1;
			had_any = 1;
			s++;
			continue;
		}
		return -1;
	}
	*ps = s;
	*out_perm_bits = bits;
	*out_had_any = had_any;
	return 0;
}

static sb_u32 chmod_shift_for_class(sb_u32 cls) {
	if (cls == CHMOD_CLASS_U) {
		return 6;
	}
	if (cls == CHMOD_CLASS_G) {
		return 3;
	}
	return 0;
}

static int chmod_apply_symbolic(const char *expr, sb_u32 in_mode, sb_u32 *out_mode) {
	if (!expr || !expr[0]) {
		return -1;
	}

	sb_u32 mode = in_mode;
	const char *s = expr;
	for (;;) {
		sb_u32 classes = 0;
		if (chmod_parse_classes(&s, &classes) != 0) {
			return -1;
		}
		if (classes == 0) {
			classes = (CHMOD_CLASS_U | CHMOD_CLASS_G | CHMOD_CLASS_O);
		}

		char op = *s;
		if (!(op == '+' || op == '-' || op == '=')) {
			return -1;
		}
		s++;

		sb_u32 perm_bits = 0;
		int had_any = 0;
		if (chmod_parse_perms(&s, &perm_bits, &had_any) != 0) {
			return -1;
		}
		if (!had_any && op != '=') {
			return -1;
		}

		sb_u32 cls;
		for (cls = CHMOD_CLASS_U; cls <= CHMOD_CLASS_O; cls <<= 1) {
			if ((classes & cls) == 0) {
				continue;
			}
			sb_u32 sh = chmod_shift_for_class(cls);
			sb_u32 class_mask = (sb_u32)(7u << sh);
			sb_u32 val = (sb_u32)(perm_bits << sh);
			if (op == '+') {
				mode |= val;
			} else if (op == '-') {
				mode &= ~val;
			} else {
				mode = (mode & ~class_mask) | val;
			}
		}

		if (*s == ',') {
			s++;
			if (*s == '\0') {
				return -1;
			}
			continue;
		}
		if (*s == '\0') {
			break;
		}
		return -1;
	}

	*out_mode = mode;
	return 0;
}

static void chmod_print_err(const char *argv0, const char *path, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, path);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "chmod";

	int i = 1;
	if (i < argc && argv[i] && sb_streq(argv[i], "--")) {
		i++;
	}

	if (argc - i < 2) {
		sb_die_usage(argv0, "chmod MODE FILE...");
	}

	const char *mode_s = argv[i];
	sb_u32 octal_mode = 0;
	int is_octal = (sb_parse_u32_octal(mode_s, &octal_mode) == 0);
	if (!is_octal) {
		sb_u32 dummy = 0;
		if (chmod_apply_symbolic(mode_s, 0, &dummy) != 0) {
			sb_die_usage(argv0, "chmod MODE(octal|symbolic) FILE...");
		}
	}
	i++;

	int any_fail = 0;
	for (; i < argc; i++) {
		const char *path = argv[i] ? argv[i] : "";
		sb_u32 out_mode = octal_mode;
		if (!is_octal) {
			struct sb_stat st;
			sb_i64 sr = sb_sys_newfstatat(SB_AT_FDCWD, path, &st, 0);
			if (sr < 0) {
				chmod_print_err(argv0, path, sr);
				any_fail = 1;
				continue;
			}
			sb_u32 base = st.st_mode & 07777u;
			if (chmod_apply_symbolic(mode_s, base, &out_mode) != 0) {
				sb_die_usage(argv0, "chmod MODE(octal|symbolic) FILE...");
			}
		}
		sb_i64 r = sb_sys_fchmodat(SB_AT_FDCWD, path, out_mode, 0);
		if (r < 0) {
			chmod_print_err(argv0, path, r);
			any_fail = 1;
		}
	}

	return any_fail ? 1 : 0;
}
