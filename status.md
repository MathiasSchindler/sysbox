# sysbox — tool status

This file tracks which tools exist in sysbox, what they currently support, and what features are still missing.

Notes:
- Target is Linux x86_64, freestanding C, syscall-only (Tier 2), static.
- “Current features” and “ToDo” are intentionally not GNU/POSIX complete; they describe sysbox behavior.

## Tools

| Tool | Current Features | ToDo (Future Features) | Notes |
|---|---|---|---|
| `[` / `test` | Minimal condition evaluation | More POSIX operators | Useful for shell scripting |
| `awk` | `print`, fields `$n`, `/regex/` + numeric patterns, `-F` | Arithmetic, variables, more actions | Designed for pipelines |
| `basename` | Strip path prefix; suffix stripping | — | Pure string logic |
| `cat` | Stream files/stdin, `-` means stdin; `-n`, `-b`, `-s` | — | — |
| `chmod` | Numeric + symbolic modes | — | Uses `fchmodat` |
| `chown` | Numeric `uid:gid` only | Name lookup via `/etc/passwd` parsing | Syscall-only implies manual parsing |
| `clear` | Writes `\033[H\033[2J` | Terminfo-aware clearing | Tiny ANSI output |
| `cmp` | Bytewise compare; exit 0/1 | More flags (e.g. `-l`) | Handy in scripts/tests |
| `col` | Handles `\b` and `\r` overstrikes | Full traditional behavior | Fixed max columns |
| `column` | Align whitespace-delimited columns | More modes/delimiters | Fixed input/rows/cols caps |
| `cp` | Regular files + symlinks; `-r`, `-p` | — | Symlinks copied as symlinks; recursion depth capped |
| `cut` | `-f` tab-delimited fields | `-d`, byte/char modes | Char mode implies Unicode decisions |
| `date` | Epoch seconds | Formatting | Uses `clock_gettime` |
| `df` | `statfs`-based totals; `-h` (K/M/G/T), `-T` (type hex), `-H` (header) | Headers formatting/padding; filesystem name decoding | Output values are derived from `statfs` |
| `diff` | Minimal line-based diff (first mismatch) | Full diffs, unified format | Useful for debugging |
| `dirname` | Path parent | Edge-case fidelity | Pure string logic |
| `du` | Summed sizes (bytes) directory traversal; `-s` | Block sizes; apparent-size toggle | Directory walking |
| `echo` | `-n`, join args; `-e/-E` escapes | — | — |
| `env` | Print env; `-i`, `-u`, `-0`; exec with modified env | — | Uses `execve` |
| `false` | Exit 1 | — | — |
| `find` | `-name` (glob `*` `?`), `-type`, `-mindepth/-maxdepth` | Full expression language, `-exec`, `-prune` | Does not follow symlink dirs |
| `free` | Reads `/proc/meminfo`; prints mem/swap totals | Headers; human readable; more fields | Values are KiB from meminfo |
| `grep` | Fixed-string matching (`-i/-v/-c/-n/-q`) | Regex | Kept intentionally small |
| `head` | `-n N`, `-c N` | — | — |
| `hexdump` | Canonical hex+ASCII dump | Flags/offset controls | Fixed 16-byte rows |
| `hostname` | Prints `uname` nodename | Set hostname | Syscall-only print |
| `id` | `id` prints numeric `uid/gid/groups`; `-u`, `-g` | `-n` names | Uses `getuid/getgid/getgroups` |
| `kill` | `kill PID...` default SIGTERM; `kill -N PID...`; `kill -l` | — | Uses `kill` syscall |
| `ln` | Hard link; `-s`, `-f` | — | — |
| `ls` | One-per-line; hides dotfiles by default; `-a`, `-l`, `-h`, `-R` | Sorting | `-R` warns if recursion is truncated; depth capped |
| `mkdir` | `mkdir DIR`, `-p`, `-m MODE` | — | — |
| `more` | Minimal pager (24 lines/page) | Termios raw mode; sizing | Prompts via `/dev/tty` when available |
| `mount` | Lists mounts via `/proc/self/mountinfo` | Mutating mounts/flags | Prints: `mountpoint\tfstype\tsource` |
| `mv` | `renameat` fast-path; EXDEV fallback copy+unlink (regular files, symlinks, dirs) | Broader filetype support; dst-exists dir semantics; preserve more metadata | Cross-FS dir move requires DST to not exist |
| `nproc` | Prints online CPUs via affinity | More flags (`--all`, etc.) | Uses `sched_getaffinity` |
| `printf` | Minimal `%s/%d/%u/%x/%c/%%` | Width/precision; more escapes | Scripting workhorse |
| `ps` | Lists PID + comm from `/proc` | More columns/filtering | Pairs with `kill` |
| `pwd` | Prints cwd | `-L/-P` semantics | — |
| `readlink` | Prints symlink target; `-f` canonicalize | — | — |
| `realpath` | Same as `readlink -f` | Flags | Symlink to `readlink` |
| `rev` | Reverse bytes per line | UTF-8-aware reverse | Fixed max line length |
| `rm` | Unlink files; `-f`, `-r`, `-d` | — | Recursion depth capped |
| `rmdir` | Remove empty dir; `-p` | — | — |
| `sed` | Substitution-only `s/REGEX/REPL/[gp]`, `-n`, `-e` | More commands; capture groups | Regex subset: `.^$.*` |
| `seq` | Integer sequences | Floats/formatting | `seq LAST|FIRST LAST|FIRST INCR LAST` |
| `sh` | `-c`, `;`, `|`, `<`, `>`, `>>`, basic quotes; `cd`, `exit` | Variables; globbing; job control | Minimal shell |
| `sleep` | Seconds | Fractions | Uses `nanosleep` |
| `sort` | In-memory line sort; `-r`, `-u`, `-n` | External sort; keys; locale | — |
| `stat` | Basic file info; `-l` lstat; default follows symlinks | Formatting options | Prints type/perm/uid/gid/size |
| `strings` | Printable ASCII runs; `-n N` | Encodings; offsets | Streaming; fixed prefix buffer |
| `tail` | `-n N` (seek on regular files, streaming fallback), `-c N` | Follow mode (`-f`) | — |
| `tee` | Write stdin to stdout + files; `-a` append | — | Multi-file fanout |
| `time` | Run cmd; print elapsed time | Formatting/options | Uses `clock_gettime` |
| `touch` | Create if missing; set times (`-t`) | — | Uses `utimensat` |
| `tr` | Basic 1:1 translate | Sets/ranges; delete; squeeze | — |
| `true` | Exit 0 | — | — |
| `uname` | `uname`, `-m`, `-a` | More flags | Uses `uname` syscall |
| `uniq` | Adjacent de-dup; `-c`, `-d`, `-u` | Fields; ignore-case; skip-chars | — |
| `uptime` | Reads `/proc/uptime` + `/proc/loadavg` | Human time formatting | Prints uptime seconds + load avgs |
| `watch` | Reruns command every N seconds (`-n`) | Interactive quit; resize | Ctrl-C to stop; clears screen |
| `wc` | `-l/-w/-c`; default prints all three | — | — |
| `which` | Searches `$PATH`; `-a` prints all matches | — | Uses `faccessat` |
| `who` | Prints `USER TTY` from utmp | More fields | May be empty in non-login env |
| `whoami` | Prints numeric uid | Name lookup via `/etc/passwd` parsing | Equivalent to `id -u` (numeric) |
| `xargs` | `-n N`, `-I REPL` templates | — | Uses `vfork/execve/wait4` |
| `yes` | Repeats a line to stdout | Custom separator | Ignores SIGPIPE best-effort |
