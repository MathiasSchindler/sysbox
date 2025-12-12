#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Create a private temp dir for each run
TMP=${TMPDIR:-/tmp}/sysbox-tests-$$
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT INT TERM
mkdir -p "$TMP"

BIN="$ROOT_DIR/bin"

[ -x "$BIN/rm" ] || fail "missing $BIN/rm (build first)"
[ -x "$BIN/cp" ] || fail "missing $BIN/cp (build first)"
[ -x "$BIN/mkdir" ] || fail "missing $BIN/mkdir (build first)"
[ -x "$BIN/rmdir" ] || fail "missing $BIN/rmdir (build first)"
[ -x "$BIN/echo" ] || fail "missing $BIN/echo (build first)"
[ -x "$BIN/head" ] || fail "missing $BIN/head (build first)"
[ -x "$BIN/tail" ] || fail "missing $BIN/tail (build first)"
[ -x "$BIN/sort" ] || fail "missing $BIN/sort (build first)"
[ -x "$BIN/uniq" ] || fail "missing $BIN/uniq (build first)"
[ -x "$BIN/tee" ] || fail "missing $BIN/tee (build first)"
[ -x "$BIN/tr" ] || fail "missing $BIN/tr (build first)"
[ -x "$BIN/cut" ] || fail "missing $BIN/cut (build first)"
[ -x "$BIN/date" ] || fail "missing $BIN/date (build first)"
[ -x "$BIN/sleep" ] || fail "missing $BIN/sleep (build first)"
[ -x "$BIN/ln" ] || fail "missing $BIN/ln (build first)"
[ -x "$BIN/readlink" ] || fail "missing $BIN/readlink (build first)"
[ -x "$BIN/basename" ] || fail "missing $BIN/basename (build first)"
[ -x "$BIN/dirname" ] || fail "missing $BIN/dirname (build first)"
[ -x "$BIN/touch" ] || fail "missing $BIN/touch (build first)"
[ -x "$BIN/chmod" ] || fail "missing $BIN/chmod (build first)"
[ -x "$BIN/chown" ] || fail "missing $BIN/chown (build first)"
[ -x "$BIN/printf" ] || fail "missing $BIN/printf (build first)"
[ -x "$BIN/yes" ] || fail "missing $BIN/yes (build first)"
[ -x "$BIN/seq" ] || fail "missing $BIN/seq (build first)"
[ -x "$BIN/uname" ] || fail "missing $BIN/uname (build first)"
[ -x "$BIN/stat" ] || fail "missing $BIN/stat (build first)"
[ -x "$BIN/df" ] || fail "missing $BIN/df (build first)"
[ -x "$BIN/ls" ] || fail "missing $BIN/ls (build first)"

# --- mkdir: -p creates parents, is idempotent, fails if a path component is a file ---
NEST="$TMP/mkdirp/a/b/c"
"$BIN/mkdir" -p "$NEST" || fail "mkdir -p failed to create nested dirs"
[ -d "$NEST" ] || fail "mkdir -p did not create expected directory"

"$BIN/mkdir" -p "$NEST" || fail "mkdir -p should succeed on existing path"

mkdir -p "$TMP/mkdirp2"
printf 'x' >"$TMP/mkdirp2/file"
set +e
"$BIN/mkdir" -p "$TMP/mkdirp2/file/sub" >"$TMP/out" 2>"$TMP/err"
RC=$?
set -e
[ $RC -ne 0 ] || fail "mkdir -p should fail when a path component is a file"
[ -s "$TMP/err" ] || fail "mkdir -p should print an error on failure"

# --- rmdir: -p removes empty ancestors, stops at non-empty parent ---
mkdir -p "$TMP/rmdir_p/empty/a/b"
"$BIN/rmdir" -p "$TMP/rmdir_p/empty/a/b" || fail "rmdir -p failed"
[ ! -e "$TMP/rmdir_p/empty/a/b" ] || fail "rmdir -p did not remove leaf"
[ ! -e "$TMP/rmdir_p/empty/a" ] || fail "rmdir -p did not remove empty parent"

mkdir -p "$TMP/rmdir_p/nonempty/a/b"
printf x >"$TMP/rmdir_p/nonempty/a/keep"
"$BIN/rmdir" -p "$TMP/rmdir_p/nonempty/a/b" || fail "rmdir -p should succeed even if parent is non-empty"
[ ! -e "$TMP/rmdir_p/nonempty/a/b" ] || fail "rmdir -p did not remove leaf under non-empty parent"
[ -d "$TMP/rmdir_p/nonempty/a" ] || fail "rmdir -p should not remove non-empty parent"
[ -f "$TMP/rmdir_p/nonempty/a/keep" ] || fail "rmdir -p should not delete files in parent"

# --- rm: continue on error, still remove what it can ---
mkdir -p "$TMP/rm1"
("$BIN/echo" hi > "$TMP/rm1/good")

set +e
"$BIN/rm" "$TMP/rm1/good" "$TMP/rm1/missing" >"$TMP/out" 2>"$TMP/err"
RC=$?
set -e

[ $RC -eq 1 ] || fail "rm should exit 1 when any operand fails (got $RC)"
[ ! -e "$TMP/rm1/good" ] || fail "rm did not remove existing operand"
[ -s "$TMP/err" ] || fail "rm should print an error for missing operand"

# --- rm: -f ignores missing operands and exits 0 ---
mkdir -p "$TMP/rm_force"
("$BIN/echo" hi > "$TMP/rm_force/good")
"$BIN/rm" -f "$TMP/rm_force/good" "$TMP/rm_force/missing" >"$TMP/out" 2>"$TMP/err" || fail "rm -f should exit 0 when only failures are ENOENT"
[ ! -e "$TMP/rm_force/good" ] || fail "rm -f did not remove existing operand"
[ ! -s "$TMP/err" ] || fail "rm -f should not print an error for missing operand"

# --- rm: -- should allow removing a file named '--' ---
mkdir -p "$TMP/rm2"
("$BIN/echo" x > "$TMP/rm2/--")
"$BIN/rm" -- "$TMP/rm2/--" || fail "rm -- failed to remove file named '--'"
[ ! -e "$TMP/rm2/--" ] || fail "rm -- did not remove file named '--'"

# --- rm: -r removes directories recursively and does not follow symlinks ---
mkdir -p "$TMP/rm_r/dir/sub"
printf 'x' >"$TMP/rm_r/dir/sub/file"
"$BIN/rm" -r "$TMP/rm_r/dir" || fail "rm -r failed to remove directory"
[ ! -e "$TMP/rm_r/dir" ] || fail "rm -r did not remove directory"

mkdir -p "$TMP/rm_r2/target/sub"
printf 'y' >"$TMP/rm_r2/target/sub/file"
ln -s "$TMP/rm_r2/target" "$TMP/rm_r2/link"
"$BIN/rm" -r "$TMP/rm_r2/link" || fail "rm -r failed on symlink operand"
[ ! -e "$TMP/rm_r2/link" ] || fail "rm -r should remove symlink itself"
[ -e "$TMP/rm_r2/target/sub/file" ] || fail "rm -r unexpectedly followed symlink into target"

# --- cp: must reject directories (regular files only) ---
mkdir -p "$TMP/cp1/srcdir"
set +e
"$BIN/cp" "$TMP/cp1/srcdir" "$TMP/cp1/dst" >"$TMP/out" 2>"$TMP/err"
RC=$?
set -e
[ $RC -eq 1 ] || fail "cp dir should exit 1 (got $RC)"

# --- cp: -r recursively copies directories ---
mkdir -p "$TMP/cp_r/srcdir/sub"
printf 'hello\n' >"$TMP/cp_r/srcdir/sub/file"
"$BIN/cp" -r "$TMP/cp_r/srcdir" "$TMP/cp_r/dstdir" || fail "cp -r failed"
cmp -s "$TMP/cp_r/srcdir/sub/file" "$TMP/cp_r/dstdir/sub/file" || fail "cp -r did not copy file contents"

# --- cp: -p preserves mode (best-effort) ---
CPPSRC="$TMP/cp_p_src"
CPPDST="$TMP/cp_p_dst"
printf 'x' >"$CPPSRC"
"$BIN/chmod" 640 "$CPPSRC" || fail "chmod for cp -p test failed"
"$BIN/cp" -p "$CPPSRC" "$CPPDST" || fail "cp -p failed"
MODE_SRC=$(stat -c %a "$CPPSRC")
MODE_DST=$(stat -c %a "$CPPDST")
[ "$MODE_SRC" = "$MODE_DST" ] || fail "cp -p did not preserve mode (src $MODE_SRC, dst $MODE_DST)"

# --- cat: -n numbers lines (including blank lines) ---
CAT_IN="$TMP/cat_in"
printf 'a\n\nb\n' >"$CAT_IN"
OUT=$($BIN/cat -n "$CAT_IN")
EXP=$(printf '1\ta\n2\t\n3\tb\n')
[ "$OUT" = "$EXP" ] || fail "cat -n unexpected output: '$OUT'"

OUT=$(printf 'x\n' | $BIN/cat -n)
EXP=$(printf '1\tx\n')
[ "$OUT" = "$EXP" ] || fail "cat -n (stdin) unexpected output: '$OUT'"

# --- head/tail: basic -n behavior on stdin ---
printf '1\n2\n3\n4\n5\n' >"$TMP/lines"
OUT=$("$BIN/head" -n 2 <"$TMP/lines")
[ "$OUT" = "1
2" ] || fail "head -n 2 unexpected output"

OUT=$("$BIN/tail" -n 2 <"$TMP/lines")
[ "$OUT" = "4
5" ] || fail "tail -n 2 unexpected output"

# --- wc: default and -l/-w/-c flags ---
WC_F="$TMP/wc-file"
printf 'a b\nc\td\n\n' >"$WC_F"
WC_LINES=3
WC_WORDS=4
WC_BYTES=$(stat -c %s "$WC_F")

OUT=$($BIN/wc "$WC_F")
[ "$OUT" = "$WC_LINES $WC_WORDS $WC_BYTES $WC_F" ] || fail "wc default unexpected output: '$OUT'"

OUT=$($BIN/wc -l "$WC_F")
[ "$OUT" = "$WC_LINES $WC_F" ] || fail "wc -l unexpected output: '$OUT'"

OUT=$($BIN/wc -w "$WC_F")
[ "$OUT" = "$WC_WORDS $WC_F" ] || fail "wc -w unexpected output: '$OUT'"

OUT=$($BIN/wc -c "$WC_F")
[ "$OUT" = "$WC_BYTES $WC_F" ] || fail "wc -c unexpected output: '$OUT'"

OUT=$(printf 'x y\n' | $BIN/wc)
[ "$OUT" = "1 2 4" ] || fail "wc (stdin) unexpected output: '$OUT'"

OUT=$(printf 'x y\n' | $BIN/wc -lw)
[ "$OUT" = "1 2" ] || fail "wc -lw (stdin) unexpected output: '$OUT'"

# --- head/tail: -c N bytes ---
printf 'abcdef' >"$TMP/bytes"
OUT=$("$BIN/head" -c 3 <"$TMP/bytes")
[ "$OUT" = "abc" ] || fail "head -c 3 unexpected output"

OUT=$("$BIN/tail" -c 3 <"$TMP/bytes")
[ "$OUT" = "def" ] || fail "tail -c 3 unexpected output"

OUT=$(printf 'abcdef' | "$BIN/tail" -c 3)
[ "$OUT" = "def" ] || fail "tail -c 3 (pipe) unexpected output"

# --- sort: in-memory, bytewise line sorting ---
printf 'b\na\nc\n' >"$TMP/sort1"
OUT=$("$BIN/sort" <"$TMP/sort1")
[ "$OUT" = "a
b
c" ] || fail "sort unexpected output"

OUT=$("$BIN/sort" -r <"$TMP/sort1")
[ "$OUT" = "c
b
a" ] || fail "sort -r unexpected output"

printf 'b\na\na\n' >"$TMP/sort2"
OUT=$("$BIN/sort" -u <"$TMP/sort2")
[ "$OUT" = "a
b" ] || fail "sort -u unexpected output"

printf 'b\n' >"$TMP/sort3a"
printf 'a\n' >"$TMP/sort3b"
OUT=$("$BIN/sort" "$TMP/sort3a" "$TMP/sort3b")
[ "$OUT" = "a
b" ] || fail "sort FILE FILE unexpected output"

# --- sort: -n numeric sort (leading integer) ---
printf '2\n10\n1\n' >"$TMP/sortn1"
OUT=$("$BIN/sort" -n <"$TMP/sortn1")
[ "$OUT" = "1
2
10" ] || fail "sort -n unexpected output"

printf '  -2\n1\n 10\n0\n' >"$TMP/sortn2"
OUT=$("$BIN/sort" -n <"$TMP/sortn2")
[ "$OUT" = "  -2
0
1
 10" ] || fail "sort -n (spaces/neg) unexpected output"

# --- uniq: streaming adjacent duplicate suppression ---
printf 'a\na\nb\nb\nc\n' >"$TMP/uniq1"
OUT=$("$BIN/uniq" <"$TMP/uniq1")
[ "$OUT" = "a
b
c" ] || fail "uniq unexpected output"

OUT=$("$BIN/uniq" -c <"$TMP/uniq1")
[ "$OUT" = "2 a
2 b
1 c" ] || fail "uniq -c unexpected output"

OUT=$("$BIN/uniq" -d <"$TMP/uniq1")
[ "$OUT" = "a
b" ] || fail "uniq -d unexpected output"

OUT=$("$BIN/uniq" -u <"$TMP/uniq1")
[ "$OUT" = "c" ] || fail "uniq -u unexpected output"

# --- tee: fanout stdin to stdout + files ---
printf 'hi\n' >"$TMP/tee_in"
printf 'hi\n' >"$TMP/tee_expected"
printf 'hi\n' | "$BIN/tee" "$TMP/tee_a" "$TMP/tee_b" >"$TMP/tee_out" || fail "tee failed"
cmp -s "$TMP/tee_expected" "$TMP/tee_out" || fail "tee stdout mismatch"
cmp -s "$TMP/tee_expected" "$TMP/tee_a" || fail "tee file A mismatch"
cmp -s "$TMP/tee_expected" "$TMP/tee_b" || fail "tee file B mismatch"

# --- tr: 1:1 bytewise translation ---
printf 'abc\n' >"$TMP/tr_in"
printf 'xyc\n' >"$TMP/tr_expected"
"$BIN/tr" ab xy <"$TMP/tr_in" >"$TMP/tr_out" || fail "tr failed"
cmp -s "$TMP/tr_expected" "$TMP/tr_out" || fail "tr output mismatch"

# --- cut: tab-delimited fields (-f) ---
printf 'a\tb\tc\n1\t2\n' >"$TMP/cut_in"
printf 'b\n2\n' >"$TMP/cut_expected1"
"$BIN/cut" -f 2 <"$TMP/cut_in" >"$TMP/cut_out1" || fail "cut -f 2 failed"
cmp -s "$TMP/cut_expected1" "$TMP/cut_out1" || fail "cut -f 2 mismatch"

printf 'a\tc\n1\n' >"$TMP/cut_expected2"
"$BIN/cut" -f 1,3 <"$TMP/cut_in" >"$TMP/cut_out2" || fail "cut -f 1,3 failed"
cmp -s "$TMP/cut_expected2" "$TMP/cut_out2" || fail "cut -f 1,3 mismatch"

# --- date: epoch seconds (numeric) ---
OUT=$($BIN/date)
case "$OUT" in
  (""|*[!0-9]*) fail "date output not numeric: '$OUT'" ;;
esac

# --- sleep: integer seconds ---
$BIN/sleep 0 || fail "sleep 0 failed"

# --- ln: hard link ---
printf 'hello' >"$TMP/ln_src"
"$BIN/ln" "$TMP/ln_src" "$TMP/ln_dst" || fail "ln failed"
[ "$(stat -c %i "$TMP/ln_src")" = "$(stat -c %i "$TMP/ln_dst")" ] || fail "ln did not create a hard link"

# --- ln: -s creates symlink ---
"$BIN/ln" -s "targetfile" "$TMP/ln_sym" || fail "ln -s failed"
OUT=$($BIN/readlink "$TMP/ln_sym")
[ "$OUT" = "targetfile" ] || fail "ln -s produced wrong target: '$OUT'"

# --- ln: -f replaces existing dst ---
printf 'A' >"$TMP/ln_src2"
printf 'B' >"$TMP/ln_dst2"
"$BIN/ln" -f "$TMP/ln_src2" "$TMP/ln_dst2" || fail "ln -f failed"
[ "$(stat -c %i "$TMP/ln_src2")" = "$(stat -c %i "$TMP/ln_dst2")" ] || fail "ln -f did not replace dst with hard link"

# --- ln: -sf replaces existing symlink ---
"$BIN/ln" -s "old" "$TMP/ln_sym2" || fail "ln -s (old) failed"
"$BIN/ln" -sf "new" "$TMP/ln_sym2" || fail "ln -sf failed"
OUT=$($BIN/readlink "$TMP/ln_sym2")
[ "$OUT" = "new" ] || fail "ln -sf produced wrong target: '$OUT'"

# --- readlink: print symlink target ---
ln -s "targetfile" "$TMP/rl_symlink"
OUT=$($BIN/readlink "$TMP/rl_symlink")
[ "$OUT" = "targetfile" ] || fail "readlink unexpected output"

# --- readlink: -f canonicalize (absolute path, resolve symlinks, normalize . and ..) ---
mkdir -p "$TMP/rlf/base/sub"
printf 'z' >"$TMP/rlf/base/sub/file"

OUT=$($BIN/readlink -f "$TMP/rlf/base/sub/file")
[ "$OUT" = "$TMP/rlf/base/sub/file" ] || fail "readlink -f (absolute file) unexpected output: '$OUT'"

OUT=$(cd "$TMP/rlf" && $BIN/readlink -f base/sub/file)
[ "$OUT" = "$TMP/rlf/base/sub/file" ] || fail "readlink -f (relative file) unexpected output: '$OUT'"

ln -s "base/sub/file" "$TMP/rlf/link1"
ln -s "link1" "$TMP/rlf/link2"
OUT=$($BIN/readlink -f "$TMP/rlf/link2")
[ "$OUT" = "$TMP/rlf/base/sub/file" ] || fail "readlink -f (symlink chain) unexpected output: '$OUT'"

OUT=$($BIN/readlink -f "$TMP/rlf/base/./sub/../sub/file")
[ "$OUT" = "$TMP/rlf/base/sub/file" ] || fail "readlink -f (dotdot) unexpected output: '$OUT'"

ln -s "loop2" "$TMP/rlf/loop1"
ln -s "loop1" "$TMP/rlf/loop2"
set +e
$BIN/readlink -f "$TMP/rlf/loop1" >"$TMP/out" 2>"$TMP/err"
RC=$?
set -e
[ $RC -ne 0 ] || fail "readlink -f should fail on symlink loop"
[ -s "$TMP/err" ] || fail "readlink -f loop should print an error"

# --- basename/dirname: pure string logic ---
OUT=$($BIN/basename /a/b/c)
[ "$OUT" = "c" ] || fail "basename /a/b/c unexpected"

OUT=$($BIN/basename /a/b/c/)
[ "$OUT" = "c" ] || fail "basename /a/b/c/ unexpected"

OUT=$($BIN/basename /a/b/c .c)
[ "$OUT" = "c" ] || fail "basename suffix strip unexpected"

OUT=$($BIN/dirname /a/b/c)
[ "$OUT" = "/a/b" ] || fail "dirname /a/b/c unexpected"

OUT=$($BIN/dirname c)
[ "$OUT" = "." ] || fail "dirname c unexpected"

OUT=$($BIN/dirname /)
[ "$OUT" = "/" ] || fail "dirname / unexpected"

# --- touch: create if missing; do not truncate if existing ---
MISSING="$TMP/touch-missing"
"$BIN/touch" "$MISSING" || fail "touch failed to create missing file"
[ -f "$MISSING" ] || fail "touch did not create missing file"

EXISTING="$TMP/touch-existing"
printf 'abc' >"$EXISTING"
"$BIN/touch" "$EXISTING" || fail "touch failed on existing file"
[ "$(wc -c <"$EXISTING")" -eq 3 ] || fail "touch unexpectedly changed file size"

# --- touch: -t sets mtime (UTC epoch) ---
STAMP="202512120102.03"
EXP=$(date -u -d '2025-12-12 01:02:03' +%s)
TFILE="$TMP/touch-stamp"
printf 'x' >"$TFILE"
"$BIN/touch" -t "$STAMP" "$TFILE" || fail "touch -t failed"
GOT=$(stat -c %Y "$TFILE")
[ "$GOT" = "$EXP" ] || fail "touch -t did not set expected mtime (got $GOT, exp $EXP)"

# --- touch: without -t updates mtime to now ---
TFILE2="$TMP/touch-now"
printf 'x' >"$TFILE2"
M1=$(stat -c %Y "$TFILE2")
sleep 1
"$BIN/touch" "$TFILE2" || fail "touch (update) failed"
M2=$(stat -c %Y "$TFILE2")
[ "$M2" -ge $((M1 + 1)) ] || fail "touch did not update mtime (m1 $M1, m2 $M2)"

# --- chmod: numeric (octal) modes ---
CHMOD_F="$TMP/chmod-file"
printf 'x' >"$CHMOD_F"
"$BIN/chmod" 600 "$CHMOD_F" || fail "chmod 600 failed"
MODE=$(stat -c %a "$CHMOD_F")
[ "$MODE" = "600" ] || fail "chmod did not set expected mode (got $MODE)"

# --- chmod: symbolic modes ---
"$BIN/chmod" u+x "$CHMOD_F" || fail "chmod u+x failed"
MODE=$(stat -c %a "$CHMOD_F")
[ "$MODE" = "700" ] || fail "chmod u+x expected 700 (got $MODE)"

"$BIN/chmod" go+r "$CHMOD_F" || fail "chmod go+r failed"
MODE=$(stat -c %a "$CHMOD_F")
[ "$MODE" = "744" ] || fail "chmod go+r expected 744 (got $MODE)"

"$BIN/chmod" a=rx "$CHMOD_F" || fail "chmod a=rx failed"
MODE=$(stat -c %a "$CHMOD_F")
[ "$MODE" = "555" ] || fail "chmod a=rx expected 555 (got $MODE)"

"$BIN/chmod" u=rw,go=r "$CHMOD_F" || fail "chmod u=rw,go=r failed"
MODE=$(stat -c %a "$CHMOD_F")
[ "$MODE" = "644" ] || fail "chmod u=rw,go=r expected 644 (got $MODE)"

# --- chown: numeric uid[:gid] (non-root expected to fail) ---
CHOWN_F="$TMP/chown-file"
printf 'y' >"$CHOWN_F"
set +e
"$BIN/chown" 0 "$CHOWN_F" >"$TMP/out" 2>"$TMP/err"
RC=$?
set -e
[ $RC -ne 0 ] || fail "chown unexpectedly succeeded (are tests running as root?)"
[ -s "$TMP/err" ] || fail "chown should print an error when it fails"

# --- printf: minimal format substitutions ---
OUT=$($BIN/printf '%s %d\n' hi 3)
[ "$OUT" = "hi 3" ] || fail "printf %s %d unexpected output: '$OUT'"

OUT=$($BIN/printf '%%\n')
[ "$OUT" = "%" ] || fail "printf %% unexpected output: '$OUT'"

# --- yes: repeat lines (bounded by head) ---
OUT=$($BIN/yes x | $BIN/head -n 3)
[ "$OUT" = "x
x
x" ] || fail "yes unexpected output"

# --- seq: integer sequences ---
OUT=$($BIN/seq 3)
[ "$OUT" = "1
2
3" ] || fail "seq 3 unexpected"

OUT=$($BIN/seq 2 4)
[ "$OUT" = "2
3
4" ] || fail "seq 2 4 unexpected"

OUT=$($BIN/seq 5 -2 1)
[ "$OUT" = "5
3
1" ] || fail "seq 5 -2 1 unexpected"

# --- uname: sysname ---
OUT=$($BIN/uname)
[ "$OUT" = "Linux" ] || fail "uname unexpected output: '$OUT'"

# --- stat: size/perm output ---
STAT_F="$TMP/stat-file"
printf 'abc' >"$STAT_F"
$BIN/chmod 600 "$STAT_F" || fail "chmod for stat test failed"
OUT=$($BIN/stat "$STAT_F")
case "$OUT" in
  (*"perm=600"*"size=3"*) : ;;
  (*) fail "stat output missing perm/size: '$OUT'" ;;
esac

# --- df: basic output exists and includes path ---
OUT=$($BIN/df "$TMP")
set -- $OUT
[ "$1" = "$TMP" ] || fail "df did not echo path as first field: '$OUT'"
case "$2" in (""|*[!0-9]*) fail "df total not numeric: '$OUT'" ;; esac
case "$3" in (""|*[!0-9]*) fail "df used not numeric: '$OUT'" ;; esac
case "$4" in (""|*[!0-9]*) fail "df avail not numeric: '$OUT'" ;; esac

# --- ls: -l long format and -h human size ---
LSDIR="$TMP/ls_long"
mkdir -p "$LSDIR"
printf 'x' > "$LSDIR/f"
mkdir -p "$LSDIR/sub"

$BIN/ls -l "$LSDIR" | grep -q " f$" || fail "ls -l missing file name"
$BIN/ls -l "$LSDIR" | grep -q "^-" || fail "ls -l did not print long format"

dd if=/dev/zero of="$LSDIR/big" bs=1024 count=2 >/dev/null 2>&1
$BIN/ls -lh "$LSDIR" | grep -q " big$" || fail "ls -lh missing big"
$BIN/ls -lh "$LSDIR" | grep -Eq " [0-9]+[KMGT] " || fail "ls -lh missing human size suffix"

echo "OK"