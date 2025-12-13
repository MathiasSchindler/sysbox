#!/bin/sh
set -eu

BIN=${1:?usage: recipes.sh /path/to/sysbox/bin /path/to/tmpdir /path/to/tests/data}
TMP=${2:?usage: recipes.sh /path/to/sysbox/bin /path/to/tmpdir /path/to/tests/data}
DATA=${3:?usage: recipes.sh /path/to/sysbox/bin /path/to/tmpdir /path/to/tests/data}

VERBOSE=${SB_TEST_VERBOSE:-0}
COUNT_ONLY=${SB_TEST_COUNT:-0}
LAST_CMD=""

CHECKS=0

vlog() {
  [ "$VERBOSE" = "1" ] && echo "$*" >&2
  return 0
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

assert_eq() {
  # $1 desc, $2 expected, $3 actual
  [ "$2" = "$3" ] || fail_cmp "$1" "$2" "$3"
  CHECKS=$((CHECKS + 1))
  vlog "PASS: $1"
}

assert_nonempty() {
  # $1 desc, $2 actual
  [ -n "$2" ] || fail_cmp "$1" "<non-empty>" "$2"
  CHECKS=$((CHECKS + 1))
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
  CHECKS=$((CHECKS + 1))
  vlog "PASS: $1"
}

run_box() {
  # $1: command string for sysbox sh -c
  LAST_CMD=$1
  vlog "RUN: $1"
  "$BIN/env" -i PATH="$BIN" "$BIN/sh" -c "$1"
}

run_box_argv() {
  # $1: command string for sysbox sh -c, then extra argv
  CMD=$1
  shift
  LAST_CMD="$BIN/sh -c $CMD $*"
  vlog "RUN: $BIN/sh -c $CMD $*"
  "$BIN/env" -i PATH="$BIN" "$BIN/sh" -c "$CMD" "$@"
}

run_script() {
  # Usage: run_script SCRIPT [args...]
  LAST_CMD="$BIN/sh $*"
  vlog "RUN: $BIN/sh $*"
  "$BIN/env" -i PATH="$BIN" "$BIN/sh" "$@"
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

if [ "$VERBOSE" = "1" ]; then
  echo "=== sysbox recipes ===" >&2
  echo "BIN=$BIN" >&2
  echo "TMP=$TMP" >&2
  echo "DATA=$DATA" >&2
fi

# Create per-suite workspace using sysbox tools (not host mkdir/rm).
RROOT="$TMP/recipes"
"$BIN/rm" -r -f "$RROOT" >/dev/null 2>&1 || true
"$BIN/mkdir" -p "$RROOT"

# 1) Word frequency (common: sort | uniq -c | sort -n)
OUT=$(run_box "cat '$DATA/words.txt' | sort | uniq -c | sort -n -r | head -n 2")
EXP=$(printf '4 apple\n3 banana')
assert_eq "word frequency top2" "$EXP" "$OUT"

# 2) Align a whitespace table (column)
OUT=$(run_box "column '$DATA/table.txt'")
EXP=$(printf 'a   bb\nccc d')
assert_eq "column alignment" "$EXP" "$OUT"

# 3) Filter+transform log lines (sed -n + s///p)
OUT=$(run_box "sed -n -e 's/ERROR/WARN/p' '$DATA/log.txt'")
EXP=$(printf 'WARN disk full\nWARN network down')
assert_eq "sed substitution+print" "$EXP" "$OUT"

# 3b) sed: multiple -e, line addressing, and delete
OUT=$(run_box "cat '$DATA/log.txt' | sed -e '2d' -e 's/INFO/DBG/'")
EXP=$(printf 'DBG start\nDBG retry\nERROR network down\nDBG end')
assert_eq "sed -e* + addressing + d" "$EXP" "$OUT"

# 4) Find a tree, sort results, count them (find | sort | wc)
OUT=$(run_box "rm -r -f '$RROOT/tree'; mkdir -p '$RROOT/tree/a'; mkdir -p '$RROOT/tree/b'; echo hi > '$RROOT/tree/a/f1'; echo hi > '$RROOT/tree/a/f2'; echo yo > '$RROOT/tree/b/f3'; find '$RROOT/tree' -type f | sort | wc -l")
assert_eq "find|sort|wc" "3" "$OUT"

# 5) Pipeline with a deliberate failure (ensure rc propagation from the last command)
run_box_rc "echo ok | false"
assert_rc "pipeline exit code propagation" 1 "$RC"

# 5b) sh: && and || short-circuit
OUT=$(run_box "false && echo NO; true || echo NO; false || echo yes")
assert_eq "sh &&/||" "yes" "$OUT"

# 6) Diff as a config change detector (exit code only)
run_box_rc "cp '$DATA/config_a.txt' '$RROOT/a'; cp '$DATA/config_b.txt' '$RROOT/b'; diff '$RROOT/a' '$RROOT/b' >/dev/null"
assert_rc "diff detects changes" 1 "$RC"

# 7) Round-trip transformation (rev twice)
OUT=$(run_box "cat '$DATA/sentences.txt' | rev | rev")
EXP=$($BIN/cat "$DATA/sentences.txt")
assert_eq "rev round-trip" "$EXP" "$OUT"

# 8) Basic hexdump sanity (extract stable fields with sed)
OUT=$(run_box "echo -n abc > '$RROOT/abc'; hexdump '$RROOT/abc' | sed -e 's/ .*//'")
assert_eq "hexdump offset" "0000000000000000" "$OUT"

OUT=$(run_box "echo -n abc > '$RROOT/abc'; hexdump '$RROOT/abc' | grep '|abc'")
assert_nonempty "hexdump ascii gutter contains abc" "$OUT"

# 9) grep: -q and -v
run_box_rc "grep -q ERROR '$DATA/log.txt'"
assert_rc "grep -q found" 0 "$RC"
run_box_rc "grep -q DOES_NOT_EXIST '$DATA/log.txt'"
assert_rc "grep -q not found" 1 "$RC"

OUT=$(run_box "grep -v INFO '$DATA/log.txt' | wc -l")
assert_eq "grep -v" "2" "$OUT"

# 10) rm: combined flags -rf
OUT=$(run_box "mkdir -p '$RROOT/rmrf/sub'; echo x > '$RROOT/rmrf/sub/f'; rm -rf '$RROOT/rmrf'; test -e '$RROOT/rmrf' && echo BAD || echo ok")
assert_eq "rm -rf" "ok" "$OUT"

# 11) sh script mode + if/while/for
OUT=$(run_script "$DATA/scripts/control.sh")
EXP=$(printf 'IF_OK\nLOOP\nAFTER\na\nbb\nc')
assert_eq "sh script + if/while/for" "$EXP" "$OUT"

# 12) sh script argv + positional parameters ($0..$N)
OUT=$(run_script "$DATA/scripts/argv.sh" one two)
EXP=$(printf 'argv0=%s\nargv1=one\nargv2=two' "$DATA/scripts/argv.sh")
assert_eq "sh script argv + positional params" "$EXP" "$OUT"

# 12b) sh script argv: $# and $@/$*
OUT=$(run_script "$DATA/scripts/argv_params.sh" one two)
EXP=$(printf 'argv0=%s\nargv1=one\nargv2=two\nargc=2\nat=one two\nstar=one two' "$DATA/scripts/argv_params.sh")
assert_eq "sh script argv: argc + at/star" "$EXP" "$OUT"

# 13) sh -c argv + positional parameters
CMD='echo argv0=$0; echo argv1=$1; echo argv2=$2'
OUT=$(run_box_argv "$CMD" zero one two)
EXP=$(printf 'argv0=zero\nargv1=one\nargv2=two')
assert_eq "sh -c argv + positional params" "$EXP" "$OUT"

# 13b) sh -c argv: $# and $@/$*
CMD='echo argc=$#; echo at=$@; echo star=$*'
OUT=$(run_box_argv "$CMD" zero one two)
EXP=$(printf 'argc=2\nat=one two\nstar=one two')
assert_eq "sh -c argv: argc + at/star" "$EXP" "$OUT"

if [ "$COUNT_ONLY" = "1" ]; then
  echo "$CHECKS"
fi

exit 0
