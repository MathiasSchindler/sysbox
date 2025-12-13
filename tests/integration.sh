#!/bin/sh
set -eu

BIN=${1:?usage: integration.sh /path/to/sysbox/bin /path/to/tmpdir}
TMP=${2:?usage: integration.sh /path/to/sysbox/bin /path/to/tmpdir}

VERBOSE=${SB_TEST_VERBOSE:-0}

LAST_CMD=""

vlog() {
  [ "$VERBOSE" = "1" ] && echo "$*" >&2
  return 0
}

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

fail_cmp() {
  # $1 desc, $2 expected, $3 actual
  echo "FAIL: $1" >&2
  if [ "$VERBOSE" = "1" ]; then
    [ -n "$LAST_CMD" ] && echo "  cmd:      $LAST_CMD" >&2
    echo "  expected: '$2'" >&2
    echo "  actual:   '$3'" >&2
  fi
  exit 1
}

mark() {
  : "${SB_TEST_DEBUG:=0}"
  [ "$SB_TEST_DEBUG" = "1" ] && echo "MARK: $*" >&2
  return 0
}

# Run a command using sysbox-only tooling:
# - sysbox env provides a clean environment
# - sysbox sh executes the pipeline/redirect logic
# - PATH points only at sysbox bin/
run_box() {
  # $1: command string for sysbox sh -c
  LAST_CMD=$1
  vlog "RUN: $1"
  "$BIN/env" -i PATH="$BIN" "$BIN/sh" -c "$1"
}

run_box_rc() {
  # echoes stdout to caller via command substitution; sets RC via global
  # shellcheck disable=SC2034
  RC=0
  set +e
  OUT=$(run_box "$1")
  RC=$?
  set -e
  printf '%s' "$OUT"
  return 0
}

assert_eq() {
  # $1 desc, $2 expected, $3 actual
  [ "$2" = "$3" ] || fail_cmp "$1" "$2" "$3"
  vlog "PASS: $1"
}

assert_rc() {
  # $1 desc, $2 expected_rc, $3 actual_rc
  if [ "$2" -ne "$3" ]; then
    if [ "$VERBOSE" = "1" ]; then
      echo "FAIL: $1" >&2
      [ -n "$LAST_CMD" ] && echo "  cmd:        $LAST_CMD" >&2
      echo "  expected rc: $2" >&2
      echo "  actual rc:   $3" >&2
    else
      echo "FAIL: $1 (expected rc $2, got $3)" >&2
    fi
    exit 1
  fi
  vlog "PASS: $1"
}

mark "sysbox integration"

if [ "$VERBOSE" = "1" ]; then
  echo "=== sysbox integration ===" >&2
  echo "BIN=$BIN" >&2
  echo "TMP=$TMP" >&2
fi

# 1) PATH lookup + basic stdout
mark "echo"
OUT=$(run_box "echo hi")
assert_eq "echo via sysbox sh" "hi" "$OUT"

# 2) Pipelines
mark "pipe wc"
OUT=$(run_box "seq 2 | wc -l")
assert_eq "pipeline wc -l" "2" "$OUT"

# 3) Redirects: > and <
mark "redir"
F1="$TMP/integration_f1"
OUT=$(run_box "echo hello > '$F1'; cat < '$F1'")
assert_eq "redirect cat" "hello" "$OUT"

# 4) Append redirect >>
mark "append"
F2="$TMP/integration_f2"
OUT=$(run_box "echo a > '$F2'; echo b >> '$F2'; wc -l < '$F2'")
assert_eq "append + wc" "2" "$OUT"

# 5) Sequencing with ';'
mark "semicolon"
OUT=$(run_box "echo a; echo b")
EXP=$(printf 'a\nb')
assert_eq "semicolon sequencing" "$EXP" "$OUT"

# 6) grep + wc
mark "grep"
OUT=$(run_box "seq 3 | tr 123 aba | grep a | wc -l")
assert_eq "grep|wc" "2" "$OUT"

# 7) sort | uniq
mark "sort uniq"
OUT=$(run_box "seq 3 | tr 123 bab | sort | uniq")
EXP=$(printf 'a\nb')
assert_eq "sort|uniq" "$EXP" "$OUT"

# 8) tr in pipeline
mark "tr"
OUT=$(run_box "echo hi | tr i o")
assert_eq "tr pipeline" "ho" "$OUT"

# 9) xargs
mark "xargs"
OUT=$(run_box "echo one two | xargs echo prefix")
assert_eq "xargs" "prefix one two" "$OUT"

# 10) find + basic filesystem operations (only sysbox tools)
mark "find"
FROOT="$TMP/integration_find"
OUT=$(run_box "rm -r -f '$FROOT'; mkdir -p '$FROOT/sub'; echo x > '$FROOT/a'; echo y > '$FROOT/sub/b'; find '$FROOT' -name b -type f")
assert_eq "find" "$FROOT/sub/b" "$OUT"

# 11) cd builtin + pwd tool
mark "cd"
CDDIR="$TMP/integration_cd"
OUT=$(run_box "mkdir -p '$CDDIR'; cd '$CDDIR'; pwd")
assert_eq "cd/pwd" "$CDDIR" "$OUT"

# 12) Exit codes (ensure failure propagates)
mark "exit codes"
run_box_rc "false"
assert_rc "false exit code" 1 "$RC"

run_box_rc "false; true"
assert_rc "compound exit code" 0 "$RC"

exit 0
