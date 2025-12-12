#include "../src/sb.h"

static void wc_print_err(const char *argv0, const char *path, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, path);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static int wc_is_space(sb_u8 c) {
	return (c == (sb_u8)' ' || c == (sb_u8)'\n' || c == (sb_u8)'\t' || c == (sb_u8)'\r' || c == (sb_u8)'\v' || c == (sb_u8)'\f');
}

static sb_i32 wc_fd(sb_i32 fd, sb_u64 *out_lines, sb_u64 *out_words, sb_u64 *out_bytes) {
	sb_u8 buf[32768];
	sb_u64 lines = 0;
	sb_u64 words = 0;
	sb_u64 bytes = 0;
	int in_word = 0;
	for (;;) {
		sb_i64 r = sb_sys_read(fd, buf, (sb_usize)sizeof(buf));
		if (r < 0) {
			return (sb_i32)r; // negative errno
		}
		if (r == 0) {
			break;
		}
		bytes += (sb_u64)r;
		for (sb_i64 i = 0; i < r; i++) {
			sb_u8 c = buf[i];
			if (c == (sb_u8)'\n') {
				lines++;
			}
			if (wc_is_space(c)) {
				in_word = 0;
			} else {
				if (!in_word) {
					words++;
					in_word = 1;
				}
			}
		}
	}
	*out_lines = lines;
	*out_words = words;
	*out_bytes = bytes;
	return 0;
}

static int wc_path(const char *argv0, const char *path, sb_u64 *out_lines, sb_u64 *out_words, sb_u64 *out_bytes) {
	if (sb_streq(path, "-")) {
		sb_i32 r = wc_fd(0, out_lines, out_words, out_bytes);
		return (r < 0) ? 1 : 0;
	}

	sb_i64 fd = sb_sys_openat(SB_AT_FDCWD, path, SB_O_RDONLY | SB_O_CLOEXEC, 0);
	if (fd < 0) {
		wc_print_err(argv0, path, fd);
		return 1;
	}

	sb_i32 rr = wc_fd((sb_i32)fd, out_lines, out_words, out_bytes);
	(void)sb_sys_close((sb_i32)fd);
	if (rr < 0) {
		wc_print_err(argv0, path, (sb_i64)rr);
		return 1;
	}
	return 0;
}

static int wc_write_counts(const char *argv0, sb_u64 lines, sb_u64 words, sb_u64 bytes, int show_lines, int show_words, int show_bytes, const char *label, int have_label) {
	(void)argv0;
	int wrote_any = 0;
	if (show_lines) {
		if (sb_write_u64_dec(1, lines) < 0) return 1;
		wrote_any = 1;
	}
	if (show_words) {
		if (wrote_any) {
			if (sb_write_all(1, " ", 1) < 0) return 1;
		}
		if (sb_write_u64_dec(1, words) < 0) return 1;
		wrote_any = 1;
	}
	if (show_bytes) {
		if (wrote_any) {
			if (sb_write_all(1, " ", 1) < 0) return 1;
		}
		if (sb_write_u64_dec(1, bytes) < 0) return 1;
		wrote_any = 1;
	}
	if (!wrote_any) {
		if (sb_write_u64_dec(1, lines) < 0) return 1;
		if (sb_write_all(1, " ", 1) < 0) return 1;
		if (sb_write_u64_dec(1, words) < 0) return 1;
		if (sb_write_all(1, " ", 1) < 0) return 1;
		if (sb_write_u64_dec(1, bytes) < 0) return 1;
	}
	if (have_label) {
		if (sb_write_all(1, " ", 1) < 0) return 1;
		if (sb_write_all(1, label, sb_strlen(label)) < 0) return 1;
	}
	if (sb_write_all(1, "\n", 1) < 0) return 1;
	return 0;
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "wc";

	int show_lines = 0;
	int show_words = 0;
	int show_bytes = 0;

	int i = 1;
	for (; i < argc && argv[i]; i++) {
		const char *a = argv[i];
		if (sb_streq(a, "--")) {
			i++;
			break;
		}
		if (a[0] != '-' || a[1] == '\0') {
			break;
		}
		for (sb_usize j = 1; a[j] != '\0'; j++) {
			char c = a[j];
			if (c == 'l') {
				show_lines = 1;
				continue;
			}
			if (c == 'w') {
				show_words = 1;
				continue;
			}
			if (c == 'c') {
				show_bytes = 1;
				continue;
			}
			sb_die_usage(argv0, "wc [-l] [-w] [-c] [--] [FILE...]");
		}
	}

	if (!show_lines && !show_words && !show_bytes) {
		show_lines = 1;
		show_words = 1;
		show_bytes = 1;
	}

	int any_fail = 0;
	sb_u64 total_lines = 0;
	sb_u64 total_words = 0;
	sb_u64 total_bytes = 0;
	int success_count = 0;
	int operand_count = argc - i;

	if (i >= argc) {
		sb_u64 lines = 0, words = 0, bytes = 0;
		sb_i32 rr = wc_fd(0, &lines, &words, &bytes);
		if (rr < 0) {
			wc_print_err(argv0, "stdin", (sb_i64)rr);
			return 1;
		}
		if (wc_write_counts(argv0, lines, words, bytes, show_lines, show_words, show_bytes, "", 0) != 0) {
			return 1;
		}
		return 0;
	}

	for (; i < argc; i++) {
		const char *path = argv[i] ? argv[i] : "";
		sb_u64 lines = 0, words = 0, bytes = 0;
		int failed = wc_path(argv0, path, &lines, &words, &bytes);
		if (failed) {
			any_fail = 1;
			continue;
		}
		total_lines += lines;
		total_words += words;
		total_bytes += bytes;
		success_count++;
		if (wc_write_counts(argv0, lines, words, bytes, show_lines, show_words, show_bytes, path, 1) != 0) {
			return 1;
		}
	}

	if (operand_count > 1 && success_count > 0) {
		if (wc_write_counts(argv0, total_lines, total_words, total_bytes, show_lines, show_words, show_bytes, "total", 1) != 0) {
			return 1;
		}
	}

	return any_fail ? 1 : 0;
}
