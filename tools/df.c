#include "../src/sb.h"

static void warn_errno(const char *argv0, const char *path, sb_i64 err_neg) {
	sb_u64 e = (err_neg < 0) ? (sb_u64)(-err_neg) : (sb_u64)err_neg;
	(void)sb_write_str(2, argv0);
	(void)sb_write_str(2, ": ");
	(void)sb_write_str(2, path);
	(void)sb_write_str(2, ": errno=");
	sb_write_hex_u64(2, e);
	(void)sb_write_str(2, "\n");
}

static void write_u64_tab(sb_u64 v) {
	(void)sb_write_u64_dec(1, v);
	(void)sb_write_all(1, "\t", 1);
}

static void write_u64_human_tab(sb_u64 v) {
	char suf = 0;
	while (v >= 1024u) {
		if (!suf) suf = 'K';
		else if (suf == 'K') suf = 'M';
		else if (suf == 'M') suf = 'G';
		else if (suf == 'G') suf = 'T';
		else break;
		v /= 1024u;
	}
	(void)sb_write_u64_dec(1, v);
	if (suf) (void)sb_write_all(1, &suf, 1);
	(void)sb_write_all(1, "\t", 1);
}

__attribute__((used)) int main(int argc, char **argv, char **envp) {
	(void)envp;
	const char *argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : "df";

	int opt_h = 0;
	int opt_T = 0;
	int opt_H = 0;

	int start = 1;
	for (; start < argc; start++) {
		const char *a = argv[start];
		if (!a) break;
		if (sb_streq(a, "--")) {
			start++;
			break;
		}
		if (a[0] != '-' || sb_streq(a, "-")) {
			break;
		}
		if (sb_streq(a, "-h")) {
			opt_h = 1;
			continue;
		}
		if (sb_streq(a, "-H")) {
			opt_H = 1;
			continue;
		}
		if (sb_streq(a, "-T")) {
			opt_T = 1;
			continue;
		}
		// Allow combined short flags: -hT / -Th
		if (a[1] != 0 && a[2] != 0 && a[1] != '-') {
			for (int j = 1; a[j]; j++) {
				if (a[j] == 'h') opt_h = 1;
				else if (a[j] == 'H') opt_H = 1;
				else if (a[j] == 'T') opt_T = 1;
				else sb_die_usage(argv0, "df [-h] [-H] [-T] [PATH...]");
			}
			continue;
		}
		sb_die_usage(argv0, "df [-h] [-H] [-T] [PATH...]");
	}

	int rc = 0;
	int any = 0;
	for (int i = start; i < argc; i++) {
		if (argv[i]) {
			any = 1;
			break;
		}
	}

	if (opt_H) {
		(void)sb_write_str(1, "path\ttotal\tused\tavail");
		if (opt_T) {
			(void)sb_write_str(1, "\ttype");
		}
		(void)sb_write_all(1, "\n", 1);
	}

	int count = (any ? (argc - start) : 1);
	for (int j = 0; j < count; j++) {
		const char *path = any ? argv[start + j] : ".";
		if (!path) path = ".";

		struct sb_statfs fs;
		sb_i64 r = sb_sys_statfs(path, &fs);
		if (r < 0) {
			warn_errno(argv0, path, r);
			rc = 1;
			continue;
		}

		sb_u64 bsize = (fs.f_frsize > 0) ? (sb_u64)fs.f_frsize : (sb_u64)fs.f_bsize;
		if (bsize == 0) bsize = 1024;

		sb_u64 total = (fs.f_blocks * bsize) >> 10;
		sb_u64 used = ((fs.f_blocks - fs.f_bfree) * bsize) >> 10;
		sb_u64 avail = (fs.f_bavail * bsize) >> 10;

		// Output:
		// default: <path>\t<total_k>\t<used_k>\t<avail_k>\n
		// -h:      <path>\t<total>\t<used>\t<avail>\n (suffix K/M/G/T)
		// -T:      append \t<type_hex>
		(void)sb_write_str(1, path);
		(void)sb_write_all(1, "\t", 1);
		if (opt_h) {
			write_u64_human_tab(total);
			write_u64_human_tab(used);
			// last numeric column: no trailing tab here
			{
				sb_u64 v = avail;
				char suf = 0;
				while (v >= 1024u) {
					if (!suf) suf = 'K';
					else if (suf == 'K') suf = 'M';
					else if (suf == 'M') suf = 'G';
					else if (suf == 'G') suf = 'T';
					else break;
					v /= 1024u;
				}
				(void)sb_write_u64_dec(1, v);
				if (suf) (void)sb_write_all(1, &suf, 1);
			}
		} else {
			write_u64_tab(total);
			write_u64_tab(used);
			(void)sb_write_u64_dec(1, avail);
		}
		if (opt_T) {
			(void)sb_write_all(1, "\t", 1);
			sb_write_hex_u64(1, (sb_u64)fs.f_type);
		}
		(void)sb_write_all(1, "\n", 1);
	}

	return rc;
}
