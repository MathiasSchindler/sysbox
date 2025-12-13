# sysbox — syscall-only coreutils-ish tools (Linux x86_64)

Date: 2025-12-12

## 0. Purpose
Build a small, fast, minimal set of “coreutils-like” command-line tools for a single target:

- OS: Linux
- Arch: x86_64 (Ryzen desktop)
- Language: C
- Dependencies: *syscalls only* (no third-party libraries; avoid libc helpers in tool logic)

This project is explicitly **not** trying to be POSIX-complete or portable. It’s a deliberately minimalist toolbox for a single machine.

## 1. Non-goals
- Cross-platform support (BSD/macOS/Windows)
- Cross-architecture support (ARM, 32-bit)
- Full GNU coreutils compatibility
- Locale/i18n, wide characters, multibyte correctness beyond UTF-8-as-bytes
- Fancy UX (colors, paging, interactive prompts by default)
- Filesystem feature completeness (ACLs, xattrs, SELinux labels)

## 2. Design principles
1. **Syscall-first**: implement behavior using Linux syscalls directly.
2. **Minimal bytes**: build with size-focused flags; keep code small.
3. **Predictable performance**: avoid allocations; stream data; minimize copies.
4. **Explicit limitations**: document what’s intentionally unsupported.
5. **Simple parsing**: only a small subset of flags; stable behavior.
6. **Consistent exit codes**: 0 success, 1 operational failure, 2 usage error.
7. **No global state**: no hidden caches; no background daemons.

## 3. Project shape
Two viable packaging approaches; pick one and stick to it.

### Option A — Multicall binary (BusyBox-style)
- One executable `sysbox`
- Determine applet by `argv[0]` (symlink name) or first argument `sysbox <cmd> ...`
- Advantages: smallest total disk footprint; shared code (arg parsing, IO)
- Disadvantages: single binary; changes affect all tools

### Option B — Many tiny binaries
- One `.c` per tool => `cat`, `ls`, `rm`, …
- Advantages: simple mental model; easy incremental work
- Disadvantages: duplicated code unless you still share a tiny internal library

**Decision (for now)**: Option B (many tiny binaries) to validate feasibility.

Future iteration:
- Introduce a tiny shared internal library (`libsysbox.a`) for syscall wrappers + helpers, *or*
- Move to Option A (multicall) once the applets stabilize.

## 4. Build & linking strategy
The phrase “only syscalls” can be interpreted in two tiers:

### Tier 1 — “No third-party libs, but libc is allowed” (pragmatic)
- Use normal C runtime and libc for process startup
- Still avoid libc convenience functions in the tool logic; prefer syscalls

### Tier 2 — “Truly syscall-only” (hardcore)
- No libc usage (no `printf`, `malloc`, `strerror`, etc.)
- Provide minimal `_start`, syscall wrappers, and tiny string/memory helpers
- Link with `-nostdlib -static` (or very small dynamic)

**Decision (for now)**: Tier 2, *truly syscall-only*, with **static linking preferred**.

Feasibility note: this is possible on Linux x86_64, but you must provide your own runtime entrypoint (`_start`) and avoid pulling in libc/crt objects. You also need to ensure the compiler doesn’t emit implicit helper calls you haven’t implemented (e.g. `memcpy`, `__stack_chk_fail`).

#### Suggested compiler/linker flags (Tier 2)
Goal: small + fast while staying freestanding.

Recommended baseline (per-tool; tune once it works):
- Compile:
  - `-O2` (speed) or `-Os` (size); start with `-Os` then compare.
  - `-ffreestanding -fno-builtin`
  - `-fno-asynchronous-unwind-tables -fno-unwind-tables`
  - `-fno-stack-protector`
  - `-fcf-protection=none` (disable Intel CET: no `endbr64`, no `.note.gnu.property`)
  - `-fno-plt -fno-pic -fno-pie` (only if your toolchain allows fully static ET_EXEC)
  - `-ffunction-sections -fdata-sections`
- Link:
  - `-nostdlib -static`
  - `-Wl,--gc-sections -Wl,-s`
  - `-Wl,-z,noseparate-code` (merge segments into fewer pages; saves ~7KB per binary)
  - `-Wl,--build-id=none` (omit `.note.gnu.build-id` section)
  - `-T scripts/minimal.ld` (custom linker script to reduce ELF/segment overhead)

Practical notes:
- `-fno-builtin` prevents the compiler from assuming libc exists, but it can also block useful optimizations. If you want both “no libc” and good codegen, you can selectively re-enable builtins and provide the few symbols the compiler might emit (e.g. `memcpy`, `memset`, `memmove`).
- Many distros build GCC with PIE-by-default; using `-no-pie` or `-fno-pie` may be required. If you end up with a PIE binary, static PIE is possible but can be trickier; treat it as an implementation detail.
- Avoid 128-bit division or floating point to keep helper routines from being pulled in.
- `-z,noseparate-code` disables the default behavior of placing `.text` and `.rodata` on separate 4KB-aligned pages (a W^X security hardening). This is acceptable here since the tools are not setuid and run in a trusted single-user environment. The size savings (~7KB per binary from eliminated padding) are significant.

Minimal runtime requirements:
- Provide `_start` and parse the initial stack to build `argc/argv/envp` (or accept only `argc/argv` and ignore envp at first).
- Provide your own `exit()` wrapper that calls `exit_group`.

### Project build knobs (current Makefile)
This repo’s canonical build configuration lives in `Makefile` and intentionally optimizes for small static ELF binaries.

- **Default LTO**: Link-time optimization is enabled by default.
  - Use: `make clean all` (LTO on)
  - Disable: `make clean all LTO=0`
  - Rationale: with many small objects, LTO tends to shrink total `.text` by enabling better inlining and dead-code elimination across translation units.

- **LTO gotcha with freestanding `_start`**: `_start` calls `main` via inline asm, which some LTO pipelines treat as “no visible reference to main”.
  - Fix used here: mark each applet’s `main` as `__attribute__((used))` so it isn’t garbage-collected.
  - Alternative fix (not used): pass a linker “undefined symbol” hint such as `-Wl,-u,main`.

- **Drop compiler ID strings**: `-fno-ident` is enabled to avoid emitting a `.comment` section, saving a small amount per binary.

- **CET disabled**: `-fcf-protection=none` is enabled to avoid `.note.gnu.property` and `endbr64` entry pads.

- **Custom linker script**: the build uses a minimal linker script to reduce program headers/notes and keep the on-disk ELF smaller.
  - Default: `LD_SCRIPT=scripts/minimal.ld` (smallest; produces a single RWX PT_LOAD to minimize overhead; linker warns about RWX).
  - Alternative: `LD_SCRIPT=scripts/minimal_wx.ld` (W^X split RX/RW PT_LOAD; slightly larger; avoids RWX warning).

- **Optional post-link shrinking**: `make tiny` runs `sstrip` (ELFkickers) if it is available in `PATH`.
  - This can remove section headers entirely and save a few hundred bytes per binary.
  - If `sstrip` is missing (common on Ubuntu), the target prints a skip message and leaves binaries untouched.

- **Size introspection**: `make size-report` prints byte sizes and (when available) section/segment summaries via the host `size` tool.

- **Size introspection (short)**: `make size-short` (or `make size-report-short`) prints a truncated report without broken-pipe noise.

## 5. Kernel ABI & syscalls
Target Linux x86_64 syscall ABI.

### Primary syscalls to use
- Process: `exit`, `exit_group`, `execve`, `execveat` (rare), `fork`/`clone` (avoid)
- IO: `read`, `write`, `close`, `openat`, `lseek`, `pread64`, `pwrite64`
- FS metadata: `newfstatat` (aka `fstatat`), `statx` (optional), `fchmodat`, `fchownat`, `utimensat`
- Directories: `getdents64`
- Links: `linkat`, `symlinkat`, `readlinkat`, `unlinkat`, `renameat` / `renameat2`
- Paths: `getcwd`
- System: `sched_getaffinity` (CPU count)
- Memory: avoid `mmap` unless needed; avoid `brk` / malloc entirely
- Efficient copy (optional): `copy_file_range`, `sendfile` (if allowed)

### Syscall wrapper layer
Implement minimal wrappers (inline asm or `syscall` instruction) in:
- `src/syscall.h` and `src/syscall.S` (or pure C inline asm)

Keep wrappers simple:
- Return negative `-errno` on error (Linux convention)
- Provide helpers `is_error(r)`, `errno_from(r)`

## 6. Runtime & utility code (tiny internal “lib”)
Even in Tier 2 you need small helpers. Keep them minimal.

### Required utilities
- `strlen`, `memcmp`, `memcpy`, `streq` (or `strcmp`-like)
- `u64toa` / `i64toa` (integer formatting)
- `write_all(fd, buf, len)` loop
- `read_full(fd, buf, len)` only if needed
- `parse_u64` for numeric flags
- Error printing that does not require strerror tables:
  - Print: `<applet>: <context>: errno=<n>\n`

### IO policy
- Prefer fixed-size stack buffers (e.g. 32 KiB for streaming file IO)
- Never allocate proportional to file size

## 7. CLI parsing rules
- Support only short options initially (e.g. `-n`, `-r`), optionally `--` to end options.
- No combined long GNU options unless explicitly added.
- No locale-aware parsing.
- Output should be stable; error messages should be short.

### Standard behaviors
- If a tool reads input and no files are given, read from stdin.
- Use `--` consistently where it makes sense.

## 8. Error handling & exit codes
- `0`: all operations succeeded
- `1`: operational error (file not found, permission denied, partial failure)
- `2`: usage error (invalid flags, missing required args)

If multiple operands are processed:
- Continue where safe (e.g. `rm`), but return `1` if any failure occurred.

## 9. Security model
- Not intended to be setuid/setgid.
- Avoid following symlinks in destructive operations unless required.
- Prefer *at()-family syscalls (`openat`, `unlinkat`, `renameat2`) to reduce TOCTOU.
- Default to non-recursive operations unless explicitly requested.

## 10. Tool roadmap (minimalist)
Start with tools that are easiest with pure syscalls and establish shared infra.

### Phase 0 — infrastructure
- Per-tool runtime entrypoint (`_start`) and argument bootstrap
- Syscall wrappers
- Tiny string/number helpers
- Common usage/help output (small, consistent)

Note: with Option B (many tiny binaries), “infrastructure” can be either duplicated in each tool initially (simplest) or factored into a tiny static object/library later.

### Phase 1 — “tiny essentials”
1. `true` / `false`
   - No flags.
2. `echo`
   - Subset: `-n` only.
   - No escape processing.
3. `cat`
   - Subset: `-n` (number lines) optional later.
   - Default: plain streaming.
4. `pwd`
   - Subset: no flags.
   - Use `getcwd` (syscall) and print.
5. `mkdir`
   - Subset: `-p` optional later; start with single dir only.
6. `rmdir`
   - Subset: single dir only, no `-p` initially.
7. `rm`
   - Subset: `-f` and `-r` later; start with plain file unlink.
8. `mv`
   - Subset: only rename within same filesystem initially (use `renameat`).
9. `cp`
   - Subset: copy regular files only; no dirs; preserve mode optional later.

### Phase 2 — “filesystem visibility”
10. `ls`
    - Minimal columns: one name per line.
    - Subset: `-a` (show dotfiles), `-l` later.
    - Implement via `openat` + `getdents64`.
11. `ln`
    - Minimal: hardlink only; add `-s` later.
12. `readlink`
    - Minimal: print link target.

### Phase 3 — “text utilities (minimal)”
13. `head`
  - Minimal: `-n <N>`, `-c <N>`.
14. `tail`
  - Minimal: `-n <N>`; start with line-based streaming for pipes; later add seek for regular files.
  - Subset: `-c <N>` bytes.
15. `wc`
    - Minimal: default prints lines/words/bytes like `wc`? Or pick bytes+lines only.
16. `sort`
  - Minimal: in-memory bytewise sort (UTF-8 treated as bytes) with a fixed buffer limit.
  - Subset: `-r` reverse, `-u` unique.
  - No locale, no key syntax, no external sort.
17. `uniq`
  - Minimal: streaming adjacent duplicate suppression.
  - Subset: `-c` counts, `-d` duplicates only, `-u` unique only.
  - Assumes input is already grouped (typically via `sort`).
18. `tee`
  - Minimal: fan out stdin to stdout and files.
19. `tr`
  - Minimal: basic 1:1 byte translation.
20. `cut`
  - Minimal: `-f` tab-delimited field selection.

### Explicitly avoid early
- `dd`, `find`, `tar`

Note: some items that were originally in this list (e.g. `chmod`, `chown`) are now implemented in minimal form.

Note: a minimal `sort` is now implemented, but it intentionally keeps the “avoid early” constraints: no locale, no key syntax, and it refuses inputs that exceed its fixed in-memory buffer.

## 11. Per-tool specifications (initial)
This section defines minimal behavior for Phase 1 and 2.

### `sort`
- Usage: `sort [-r] [-u] [-n] [FILE...]`
- Input:
  - If no FILE operands are given: read stdin.
  - Otherwise: read each FILE in order (and `-` means stdin), concatenated as one stream.
- Sorting:
  - Split on `\n` and sort lines **bytewise** (UTF-8 treated as bytes; no locale).
  - `-n`: numeric sort by leading integer (skips leading ASCII whitespace); ties are broken bytewise.
  - `-r`: reverse order.
  - `-u`: unique (after sorting, suppress adjacent duplicates).
- Output:
  - Writes sorted lines to stdout, each terminated by `\n`.
- Limits:
  - This implementation is strictly in-memory with a fixed buffer cap (currently 4 MiB total for data+metadata).
  - If the input cannot fit within the buffer (including per-line metadata), `sort` fails with exit code 1.

### `uniq`
  - Usage: `uniq [-c] [-d|-u] [FILE...]`
  - Input:
    - If no FILE operands are given: read stdin.
    - Otherwise: read each FILE in order (and `-` means stdin), concatenated as one stream.
  - Behavior:
    - Collapses **adjacent** identical lines only (bytewise comparison).
    - `-c`: prefix output lines with `<count><space>`.
    - `-d`: only print lines that occurred more than once in a group.
    - `-u`: only print lines that occurred exactly once in a group.
  - Limitations:
    - No locale/case folding.
    - Fixed maximum line length (currently 65536 bytes); longer lines cause exit code 1.

  ### `tee`
    - Usage: `tee [-a] [FILE...]`
    - Behavior:
      - Reads stdin and writes to stdout and each FILE.
      - FILE `-` is treated as stdout.
      - `-a`: append to FILE outputs instead of truncating.
    - Limitations:
      - Only supports `-a` (no other GNU flags).
      - Opens FILE outputs as create+truncate by default (mode 0666; umask applies).
      - If a file cannot be opened or written, `tee` prints an error and continues with the remaining outputs; exit code is 1 if any such error occurred.

  ### `tr`
    - Usage: `tr SET1 SET2`
    - Behavior:
      - Translates stdin by mapping each byte in SET1 to the corresponding byte in SET2.
      - Comparison/translation is bytewise; no escape processing, no ranges, no classes.
    - Limits:
      - Requires `strlen(SET1) == strlen(SET2)` and both non-empty.

  ### `cut`
    - Usage: `cut -f LIST [FILE...]`
    - Input:
      - If no FILE operands are given: read stdin.
      - Otherwise: read each FILE in order (and `-` means stdin).
    - Behavior:
      - Splits each line on tab (`\t`) and outputs the selected fields separated by tabs.
      - LIST supports comma-separated numbers and ranges (e.g. `1`, `2,4`, `1-3,7`).
    - Limits:
      - Only `-f` mode is supported (tab delimiter only).
      - Field numbers above 1024 are ignored.

    ### `date`
      - Usage: `date`
      - Behavior:
        - Prints epoch seconds (UTC) followed by `\n`.
      - Limitations:
        - No formatting flags.

    ### `sleep`
      - Usage: `sleep SECONDS`
      - Behavior:
        - Sleeps for an integer number of seconds.
        - Retries automatically if interrupted by a signal.
      - Limitations:
        - No fractional seconds.

      ### `ln`
        - Usage: `ln [-s] [-f] [--] SRC DST`
        - Behavior:
          - Default: creates a hard link at DST pointing to SRC (`linkat`).
          - `-s`: create a symlink at DST with target SRC (`symlinkat`).
          - `-f`: unlink existing DST before creating link (ignore ENOENT).

      ### `readlink`
        - Usage: `readlink [-f] PATH`
        - Behavior:
          - Default: prints the raw symlink target bytes followed by `\n`.
          - `-f`: resolve symlinks and print an absolute, normalized path.
            - Prepends the current working directory if PATH is relative.
            - Normalizes `.` and `..` components.
            - Resolves symlinks per-component (bounded to a maximum depth to avoid loops).
        - Limitations:
          - Fixed buffers (currently 4096 bytes); longer paths/targets may fail.
          - `-f` fails if any path component does not exist.

      ### `realpath`
        - Usage: `realpath PATH`
        - Behavior:
          - Prints an absolute, normalized, symlink-resolved path (equivalent to `readlink -f PATH`).
        - Notes:
          - Implemented by invoking the `readlink` binary under the name `realpath`.

### `basename`
          - Usage: `basename PATH [SUFFIX]`
          - Behavior:
            - Prints the last path component of PATH followed by `\n`.
            - Trailing slashes are ignored.
            - If SUFFIX is provided and matches the end of the basename (and is not the entire basename), it is removed.
          - Notes:
            - PATH consisting only of slashes prints `/`.

### `dirname`
          - Usage: `dirname PATH`
          - Behavior:
            - Prints the directory portion of PATH followed by `\n`.
            - Trailing slashes are ignored.
          - Notes:
            - PATH with no `/` prints `.`.
            - PATH consisting only of slashes prints `/`.

### `printf`
          - Usage: `printf FORMAT [ARG...]`
          - Behavior:
            - Supports only: `%s`, `%d`, `%u`, `%x`, `%c`, `%%`.
            - `%d` parses signed decimal; `%u` parses unsigned decimal; `%x` parses unsigned decimal and prints hex (no `0x` prefix).
            - Format string supports minimal escapes: `\\n`, `\\t`, `\\\\`.
            - Missing arguments are treated as empty/zero.
          - Limitations:
            - No width/precision, no repeated-format behavior.

### `chmod`
- Usage: `chmod MODE FILE...`
- `MODE` formats:
  - Octal: `chmod 755 FILE`
  - Symbolic: `[ugoa]*[+-=][rwx]+` with optional comma-separated clauses (e.g. `u+x`, `go-w`, `a=rx`, `u=rw,go=r`).
- Notes/limitations:
  - Symbolic mode only supports `r`, `w`, `x` (no `X`, no `s`/`t`, no permission-copy like `g=u`).
  - A missing class set defaults to `a` (all of `u`, `g`, `o`).

### `yes`
          - Usage: `yes [STRING...]`
          - Behavior:
            - Writes `STRING...` joined by single spaces (or `y` if no args), followed by `\n`, repeatedly.
            - Exits 0 on broken pipe.
          - Limitations:
            - Fixed maximum line buffer (4096 bytes).

### `seq`
          - Usage: `seq LAST | seq FIRST LAST | seq FIRST INCR LAST`
          - Behavior:
            - Generates an integer sequence, one value per line.
            - Handles negative ranges and negative increments.
          - Limitations:
            - Integers only; no formatting, no floats.

### `uname`
          - Usage: `uname [-s|-m|-a]`
          - Behavior:
            - Default prints kernel name (sysname).
            - `-m` prints machine.
            - `-a` prints: sysname nodename release version machine.
          - Limitations:
            - No other GNU flags.

### `stat`
          - Usage: `stat [-l|-L] FILE...`
          - Output:
            - One line per operand:
              - `<path>: type=<reg|dir|lnk|other> perm=<NNN> uid=<U> gid=<G> size=<S>`
          - Notes:
            - Default follows symlinks (uses `newfstatat(..., flags=0)`).
            - `-l` uses `lstat` semantics (`newfstatat(..., AT_SYMLINK_NOFOLLOW)`).
            - `-L` forces follow-symlinks behavior (default).
            - Uses `newfstatat` to avoid blocking on special files (e.g. FIFOs).

### `df`
          - Usage: `df [-h] [-H] [-T] [PATH...]`
          - Output:
            - One line per operand (or `.` if none):
              - `<path>\t<total_k>\t<used_k>\t<avail_k>`
            - Values are in 1KiB blocks derived from `statfs`.
            - With `-h`: numbers are scaled by 1024 and suffixed with `K/M/G/T`.
            - With `-T`: an extra final column is appended: `<type_hex>` (the `statfs.f_type` value).
            - With `-H`: print a header line first.
          - Limitations:
            - Minimal formatting; no headers.

### `du`
          - Usage: `du [-s] [PATH...]`
          - Output: one line per reported directory or file:
            - `<bytes>\t<path>`
          - Behavior:
            - For file operands: prints the file size in bytes (`st_size`).
            - For directory operands (default): recursively sums sizes of contained non-directory entries (does not follow symlinks) and prints totals for each directory encountered (post-order).
            - `-s`: summary only; prints only the total for each operand (or `.` if none).
          - Exit codes:
            - `0`: all operands processed successfully
            - `1`: at least one operand failed
            - `2`: usage error
          - Limitations:
            - Fixed recursion depth (64) and fixed per-directory subdir tracking.
            - Uses apparent size (`st_size`), not allocated blocks.

### `clear`
          - Usage: `clear`
          - Behavior:
            - Writes the ANSI escape sequence `\033[H\033[2J` to stdout.
          - Limitations:
            - No terminfo handling; expects an ANSI-compatible terminal.

### `hostname`
          - Usage: `hostname`
          - Behavior:
            - Prints the system hostname (the `nodename` field from `uname`) followed by `\n`.
          - Limitations:
            - Printing only; no flags to set hostname.

### `nproc`
          - Usage: `nproc`
          - Behavior:
            - Prints the number of CPUs available to the current process, derived from `sched_getaffinity(0, ...)`, followed by `\n`.
          - Limitations:
            - Fixed CPU mask buffer; extremely large CPU counts may be truncated.

### `env`
          - Usage: `env [-i] [-0] [-u NAME]... [NAME=VALUE...] [CMD [ARGS...]]`
          - Behavior:
            - Without CMD: prints the environment, one `NAME=VALUE` per line.
            - With CMD: executes CMD with a modified environment (`execve`).
            - `-i`: start with an empty environment before applying assignments.
            - `-u NAME`: remove `NAME` from the environment (can be repeated).
            - `-0`: when printing, separate entries with NUL (`\0`) instead of newline.
            - If CMD contains `/`, it is executed directly; otherwise it is searched via `PATH` from the constructed environment.
              - Note: empty `PATH` segments (e.g. `::` or leading/trailing `:`) are treated as the current directory.
          - Exit codes:
            - `0`: success
            - `1`: operational error (e.g. command not found / exec failure)
            - `2`: usage error
          - Limitations:
            - Fixed maximum number of environment entries and argv entries.

### `sed`
          - Usage: `sed [-n] [-e SCRIPT] [SCRIPT] [FILE...]`
          - Script:
            - Supported commands:
              - Substitution: `s/REGEX/REPL/[g][p]`
              - Delete: `d`
            - Minimal addressing:
              - `Ncmd` applies `cmd` only on line `N` (e.g. `2d`, `3s/a/b/`).
              - `$cmd` applies `cmd` only on the last input line (e.g. `$d`, `$s/a/b/`).
            - Multiple commands:
              - `-e SCRIPT` is repeatable; commands are applied in order.
              - If no `-e` is used, the first non-option argument is the SCRIPT.
            - Delimiter can be any single byte after `s` (e.g. `s#foo#bar#`).
          - Regex subset:
            - Literals, `.`, `*`, `^`, `$`.
            - `\\x` escapes `x` to a literal byte.
          - Replacement subset:
            - `&` expands to the whole match.
            - `\\x` inserts literal `x`; `\\n` inserts a newline byte.
          - Printing:
            - Default prints every input line after applying the substitution.
            - With `-n`, prints only when a substitution succeeds and the `p` flag is present.
          - Exit codes:
            - `0`: success
            - `1`: operational error (I/O, file open error, line/output too long)
            - `2`: usage error

### `awk`
          - Usage: `awk [-F FS] [--] PROGRAM [FILE...]`
          - Supported PROGRAM forms:
            - `{print}`
            - `{print ITEM[,ITEM...]}`
            - `/REGEX/ {print ...}`
            - `$N OP INT {print ...}` (numeric pattern)
            - `NR OP INT {print ...}` (numeric pattern)
            - `NF OP INT {print ...}` (numeric pattern)
            - `/REGEX/` (default action: print the full line)
            - `print ITEM[,ITEM...]` (action without braces)
          - Numeric pattern operators:
            - `==`, `!=`, `<`, `<=`, `>`, `>=`
            - Integers are parsed as signed decimal; non-numeric fields are treated as “no match”.
          - Supported `print` items:
            - `$0`, `$1..$N` fields
            - `NR` (current line number, 1-based across all inputs)
            - `NF` (number of fields on the current line)
            - string literals: `"..."` with escapes `\\n`, `\\t`, `\\\\`, `\\\"`
          - Field splitting:
            - Default: whitespace FS (space/tab; runs collapse; no empty fields)
            - `-F FS`: only single-byte FS is supported; empty fields are preserved
          - Regex subset:
            - Literals, `.`, `*`, `^`, `$`, and `\\x` escapes.
          - Exit codes:
            - `0`: success
            - `1`: operational error (I/O, file open error, line too long)
            - `2`: usage error

### `find`
          - Usage: `find [PATH...] [EXPR]`
          - PATH:
            - If no PATH operands are provided, defaults to `.`.
            - Each PATH is visited in pre-order (directory printed before its contents).
          - Supported EXPR subset (AND-only):
            - `-name PAT`: match PAT against the basename using glob subset `*` and `?`.
            - `-type f|d|l`: match regular files, directories, or symlinks.
            - `-mindepth N`: only match/print entries at depth $\ge N$ (root is depth 0).
            - `-maxdepth N`: do not descend below depth N (root is depth 0).
            - `-print`: accepted but is the default action.
          - Symlinks:
            - Uses `newfstatat(..., AT_SYMLINK_NOFOLLOW)`.
            - Does not follow symlinks when recursing into directories.
          - Exit codes:
            - `0`: success
            - `1`: operational error (I/O, stat/open errors)
            - `2`: usage error

### `sh`
          - Usage:
            - `sh -c CMD [arg0 [arg...]]`
            - `sh FILE [arg...]`
          - Behavior:
            - If `-c CMD` is provided, executes CMD and exits with the status of the last pipeline element.
            - If FILE is provided, reads commands from FILE.
            - Otherwise reads from stdin and prints a minimal `$ ` prompt.
          - Supported syntax subset:
            - Command separators: `;` and newline
            - Pipelines: `cmd1 | cmd2 | ...`
            - Conditionals: `cmd1 && cmd2`, `cmd1 || cmd2` (short-circuit based on exit status)
            - Control flow (minimal):
              - `if LIST; then LIST; [else LIST;] fi`
              - `while LIST; do LIST; done`
              - `for NAME in WORD...; do LIST; done`
            - Redirections: `< file`, `> file`, `>> file`
            - Quoting: single quotes `'...'`, double quotes `"..."` (supports `\\n`, `\\t`, `\\"`, `\\\\` escapes)
            - Backslash escapes outside quotes: `\\x` => literal `x`
            - Comments: `#` starts a comment at token boundary
            - Minimal variable expansion:
              - `$NAME` is expanded for variables set by `for`.
            - Minimal positional parameter expansion:
              - `$0..$N` expands from the argv passed to `sh`.
                - Script mode (`sh FILE ...`): `$0` is the script path, `$1` is the first script arg, etc.
                - `-c` mode (`sh -c CMD ...`): if args are provided, `$0` is the first arg after CMD.
              - `$#` expands to the count of positional parameters ($1..$N).
              - `$@` and `$*` expand to `$1..$N` joined by single spaces (no word-splitting; quoting does not change behavior).
          - Builtins:
            - `cd [DIR]` (uses `$HOME` when DIR is omitted)
            - `exit [N]`
          - Limitations:
            - No general variables/assignments, no globbing, no job control.
              - Only `$NAME` expansion for variables set by `for` is supported.
            - Positional parameters are limited (fixed maximum count; out-of-range expands to empty).
            - Fixed maximum line length, token count, argv count, and pipeline length.
          - Exit codes:
            - `0`: success
            - `1`: operational error
            - `2`: usage error

### `test` / `[` 
- Usage: `test EXPRESSION` or `[ EXPRESSION ]`
- Behavior:
  - Evaluates a minimal expression and exits `0` if true, `1` if false.
  - Supported forms:
    - Unary file tests: `-e PATH`, `-f PATH`, `-d PATH`, `-r PATH`, `-w PATH`, `-x PATH`
    - Unary string tests: `-z STRING`, `-n STRING`
    - Binary string tests: `STR1 = STR2`, `STR1 != STR2`
    - Binary integer tests: `N1 -eq|-ne|-lt|-le|-gt|-ge N2`
  - With a single operand: true iff the operand is a non-empty string.
- Exit codes:
  - `0`: expression true
  - `1`: expression false
  - `2`: usage error (invalid operator/arity, or `[` missing closing `]`)

### `grep`
- Usage: `grep [-i] [-v] [-c] [-n] [-q] PATTERN [FILE...]`
- Behavior:
  - Fixed-string substring matching (no regex).
  - If no FILE operands are given, reads stdin.
  - `-i`: ASCII-only case-insensitive match.
  - `-v`: invert match.
  - `-c`: print the number of matching lines.
  - `-n`: prefix matching lines with `LINE:`.
  - `-q`: quiet; exit 0 on first match, 1 otherwise.
- Exit codes:
  - `0`: at least one match
  - `1`: no matches (or operational errors)
  - `2`: usage error

### `kill`
- Usage: `kill [-l] [-N] PID...`
- Behavior:
  - `kill PID...`: sends SIGTERM (15) to each PID.
  - `kill -N PID...`: sends signal N to each PID.
  - `kill -l`: prints signal numbers (minimal: one per line).
- Exit codes:
  - `0`: all signals sent successfully
  - `1`: at least one PID failed
  - `2`: usage error

### `id`
- Usage: `id [-u|-g]`
- Behavior:
  - Default: prints `uid=N gid=N groups=N,...`.
  - `-u`: prints uid only.
  - `-g`: prints gid only.
- Exit codes:
  - `0`: success
  - `1`: operational error
  - `2`: usage error

### `whoami`
- Usage: `whoami`
- Behavior:
  - Prints the numeric uid followed by `\n`.
  - Equivalent to: `id -u`.

### `which`
- Usage: `which [-a] CMD...`
- Behavior:
  - For each `CMD`, search `$PATH` (split on `:`) for executable matches.
    - Note: empty `PATH` segments (e.g. `::` or leading/trailing `:`) are treated as the current directory.
  - Default: prints the first match per `CMD`, one per line.
  - `-a`: prints all matches per `CMD`, one per line, in `$PATH` order.
  - If `CMD` contains `/`, it is checked directly.
- Exit codes:
  - `0`: all commands found
  - `1`: at least one command not found
  - `2`: usage error

### `echo`
- Usage: `echo [-n] [-e|-E] [ARG...]`
- Output: join args with single spaces; write trailing `\n` unless `-n`.
- Escapes:
  - Default: `-E` (no escape processing)
  - `-e`: interpret a small subset: `\\n`, `\\t`, `\\r`, `\\b`, `\\a`, `\\\\`, `\\0`
- Exit: always 0 unless write fails.

### `xargs`
- Usage: `xargs [-n N] [-I REPL] [--] CMD [ARGS...]`
- Behavior:
  - Reads whitespace-delimited tokens from stdin.
  - Default: appends tokens as arguments to CMD in batches; `-n N` limits batch size.
  - `-I REPL`: runs CMD once per input token; replaces occurrences of REPL in ARGS with the token.
    - If REPL is not used in any ARGS, the token is appended as an additional argument.
    - Replacement is literal (no escaping). If you pass substituted data to `sh -c`, shell injection is possible (same semantics as typical `xargs`).
- Exit codes:
  - `0`: all invocations succeeded (or no input)
  - `1`: at least one invocation failed
  - `2`: usage error

### `cmp`
- Usage: `cmp [-s] [--] FILE1 FILE2`
- Compares FILE1 and FILE2 bytewise.
- Exit codes:
  - `0`: identical
  - `1`: different
  - `2`: usage error
- `-s`: silent (no diagnostic on difference).

### `diff`
- Usage: `diff [--] FILE1 FILE2`
- Minimal line-based diff: prints the first differing line only.
- Exit codes:
  - `0`: identical
  - `1`: different
  - `2`: usage error

### `time`
- Usage: `time [--] CMD [ARGS...]`
- Runs CMD and prints elapsed time (monotonic) to stderr as `real S.NNNNNNNNN`.
- Exit code: same as CMD (or `127` if exec fails).

### `ps`
- Usage: `ps`
- Lists processes by scanning `/proc`.
- Output: header `PID CMD`, then one process per line.

### `who`
- Usage: `who`
- Prints logged-in users by reading `utmp` (`/run/utmp` or `/var/run/utmp`).
- Output: `USER TTY` per line (if available).

### `cat`
- Usage: `cat [-n] [-b] [-s] [--] [FILE...]`
- If no FILE: copy stdin to stdout.
- For each FILE:
  - Special case `-` means stdin.
- `-n`: prefix each output line with `<line>\t` (includes blank lines; line numbering continues across files).
- `-b`: number nonblank lines only.
- `-s`: squeeze repeated blank lines.
- Implementation notes:
  - `openat(AT_FDCWD, path, O_RDONLY|O_CLOEXEC)`
  - loop `read` into buffer then `write_all(1, ...)`

### `head`
- Usage: `head [-n N] [-c N] [FILE...]`
- Default: `-n 10`.
- `-n N`: write the first N lines.
- `-c N`: write the first N bytes.

### `tail`
- Usage: `tail [-n N] [-c N] [FILE...]`
- Default: `-n 10`.
- `-n N`: write the last N lines.
  - For regular files: uses `lseek` + a backward scan to find the start offset, then streams forward.
  - For non-seekable input (pipes): streaming ring-buffer implementation.
- `-c N`: write the last N bytes.
  - For regular files: use `lseek` to seek from end, then read to EOF.
  - For non-seekable input (pipes): keep a ring buffer of the last N bytes.

### `wc`
- Usage: `wc [-l] [-w] [-c] [--] [FILE...]`
- Default (no flags): print lines, words, bytes.
- Flags:
  - `-l`: lines only
  - `-w`: words only (words are sequences of non-whitespace bytes)
  - `-c`: bytes only
- Output:
  - For each operand: selected counts separated by single spaces, then ` FILE`.
  - If multiple operands: includes a `total` line.

### `touch`
- Usage: `touch [-t STAMP] [--] FILE...`
- Behavior:
  - For each FILE, create it if missing.
  - Without `-t`, set both atime and mtime to the current time.
  - With `-t`, set both atime and mtime to STAMP.
- `-t` format: `[[CC]YY]MMDDhhmm[.ss]`
  - If year is omitted (STAMP is `MMDDhhmm[.ss]`), the current UTC year is used.
  - Timestamp interpretation is **UTC** (no local-time / timezone handling).
- Implementation notes:
  - Uses `utimensat(AT_FDCWD, path, times, 0)`.
  - Time source for “now” is `clock_gettime(CLOCK_REALTIME)`.

### `pwd`
- Usage: `pwd`
- Implement via `getcwd` into fixed buffer (start at 4 KiB; if ERANGE, either fail or retry with a larger fixed maximum like 64 KiB).

### `mkdir`
- Usage: `mkdir [-p] [-m MODE] DIR`
- Mode: default 0777 masked by umask (kernel behavior)
- `-p`: create intermediate directories as needed; succeed if components already exist as directories.
- `-m MODE`: set directory mode (octal, e.g. `755`, masked by umask).

### `rmdir`
- Usage: `rmdir [-p] DIR`
- Only empty directories.
- `-p`: after removing DIR, attempt to remove its parent directories as well, stopping when a parent cannot be removed.

### `rm`
- Usage: `rm [-f] [-r] [-d] [--] FILE...`
- Uses `unlinkat(AT_FDCWD, path, 0)`.
- `-f`: ignore missing operands (ENOENT) and do not print an error for those.
- `-d`: allow removing empty directories (non-recursive).
- `-r`: recursively remove directories (depth-first). Symlinks are removed as symlinks (not followed).
- Recursion is bounded to a maximum depth of 64.
- If any operand fails (other than ignored ENOENT under `-f`): exit 1.

### `mv`
- Usage: `mv SRC DST`
- Minimal: only two args.
- Use `renameat(AT_FDCWD, SRC, AT_FDCWD, DST)`.
- If `renameat` fails with EXDEV (cross-filesystem), `mv` falls back to copy+unlink:
  - Regular files: read/write loop to `DST` then `unlink SRC`.
  - Symlinks: recreate symlink at `DST` then `unlink SRC`.
  - Directories: recursively move the directory contents.
    - Limitation: only supported when `DST` does not exist.
    - Limitation: only supports regular files, symlinks, and subdirectories inside the tree.
  - Other file types (devices, FIFOs, etc.): usage error.

### `cp`
- Usage: `cp [-r] [-p] [--] SRC DST`
- Copy method:
  - read/write loop (optionally later add `copy_file_range` fast path).
- Default behavior (no `-r`):
  - Regular files and symlinks (symlinks are copied as symlinks; never dereferenced).
  - Fails on directories (use `-r`) and other non-regular file types (devices, FIFOs, etc.).
- `-r`:
  - If SRC is a directory, recursively copy its contents into DST (DST is created as a directory if needed).
  - Directory traversal is via `openat(O_DIRECTORY)` + `getdents64`.
  - Symlinks are copied as symlinks (never dereferenced).
  - Recursion is bounded to a maximum depth of 64.
- `-p`:
  - Preserve mode and timestamps; attempt to preserve uid/gid (best-effort; may fail for non-root).

### `ls`
- Usage: `ls [-a] [-l] [-h] [-R] [DIR]`
- Minimal: list directory entries one per line (kernel order is fine).
- `-a`: include dotfiles.
- `-l`: long listing: print `TYPE+PERMS`, nlink, uid, gid, size, mtime, name.
  - `mtime` is printed as epoch seconds (no human date formatting).
  - Use `fstatat(dirfd, name, AT_SYMLINK_NOFOLLOW)` per entry (do not follow symlinks).
- `-h`: with `-l`, print size with K/M/G/T suffix (base 1024, integer only).
- `-R`: recursive directory listing.
  - Prints a `<dir>:` header before each directory listing, with a blank line between directory blocks.
  - Does not recurse into symlinks; never recurses into `.` or `..`.
  - Recursion is bounded to a maximum depth of 64.
  - If there are too many subdirectories in a single directory to track (fixed pool), `ls` prints a warning and skips recursing into the overflow.

### `uptime`
- Usage: `uptime`
- Behavior:
  - Reads `/proc/uptime` and `/proc/loadavg`.
  - Prints one line: `up <seconds.fraction> load <l1> <l5> <l15>`.
- Limitations:
  - No “pretty” human duration formatting.
  - No user/session counting.

### `free`
- Usage: `free`
- Behavior:
  - Reads `/proc/meminfo` and prints:
    - `mem\tTOTAL\tFREE\tAVAIL\tBUFFERS\tCACHED`
    - `swap\tTOTAL\tFREE`
  - All values are KiB (as reported by `/proc/meminfo`).
- Limitations:
  - No headers, no human-readable output.
  - Only a small subset of meminfo keys are reported.

### `mount`
- Usage: `mount`
- Behavior:
  - Lists current mounts by reading `/proc/self/mountinfo`.
  - Prints one line per mount: `<mountpoint>\t<fstype>\t<source>`.
- Limitations:
  - Listing-only (no `-t`, `-o`, bind/remount, or mutating mounts).
  - Escaped spaces in mountinfo (e.g. `\040`) are printed as-is.

### `strings`
- Usage: `strings [-n N] [FILE...]`
- Behavior:
  - Scans byte streams and prints runs of printable ASCII bytes (plus tab) of length at least `N`.
  - Default `N` is 4.
  - If no FILE operands are given: read stdin.
- Limitations:
  - ASCII-only; no encoding detection.
  - No offsets/addresses.

### `rev`
- Usage: `rev [FILE...]`
- Behavior:
  - Reverses bytes within each line (bytewise; UTF-8 treated as bytes).
  - If no FILE operands are given: read stdin.
- Limitations:
  - Fixed maximum line length (currently 65536 bytes); longer lines are an operational failure.

### `column`
- Usage: `column [FILE...]`
- Behavior:
  - Reads input and splits each line into whitespace-delimited fields.
  - Aligns columns by padding with spaces and prints a table.
  - If no FILE operands are given: read stdin.
- Limits:
  - Fixed maximum total input size (currently 512 KiB).
  - Fixed maximum rows/columns (currently 2048 rows, 32 columns).
- Limitations:
  - No delimiter selection and no quoting/escaping rules.

### `col`
- Usage: `col [FILE...]`
- Behavior:
  - Minimal “overstrike cleanup” filter:
    - `\b` moves the cursor left by 1.
    - `\r` resets cursor to column 0 (overstrike).
    - `\f` is treated as a newline.
  - Other control bytes are ignored.
- Limits:
  - Fixed maximum line width (currently 4096 columns).
- Limitations:
  - Not a full traditional `col` implementation.

### `more`
- Usage: `more [FILE...]`
- Behavior:
  - Prints input and prompts after a fixed number of lines (currently 24).
  - Attempts to read prompt keystrokes from `/dev/tty`; if unavailable, it behaves like `cat`.
  - `q` quits; any other key continues.
- Limitations:
  - No terminal size detection, no raw-mode input, no search.

### `watch`
- Usage: `watch [-n SECS] [--] CMD [ARGS...]`
- Behavior:
  - Clears the screen, runs CMD, sleeps, repeats.
  - Default interval is 2 seconds.
- Limitations:
  - No interactive quit key; use Ctrl-C.
  - No terminal size detection.

### `hexdump`
- Usage: `hexdump [FILE...]`
- Behavior:
  - Prints a canonical hex + ASCII view in fixed 16-byte rows.
  - If no FILE operands are given: read stdin.
- Limitations:
  - No flags (offset controls, widths, formats).

## 12. Testing strategy (lightweight)
- Shell-based golden tests under `tests/` using `sh`.
- Compare outputs with expected where deterministic.
- For non-deterministic ordering (`ls` without sorting): test set membership rather than exact order.

## 13. Benchmarking
- Microbenchmarks:
  - `cat` on large files (cold cache vs warm)
  - `cp` vs `cp` from coreutils
- Measure:
  - `time` (user/sys)
  - `perf stat` (cycles, instructions, page faults)

## 14. Style conventions
- C dialect: C11 or GNU11 (decide early)
- No dynamic allocation in applets
- All outputs via `write(1,...)` / `write(2,...)`
- No `stdio.h` in Tier 2 build

## 15. Known limitations (initial)
- No long options.
- No locale-aware behavior.
- Limited handling of extremely long paths.
- No advanced metadata preservation.
- Minimal error strings (errno number only).

## 16. Open questions (to resolve early)
1. Multicall vs many binaries — confirm final choice.
2. Tier 1 vs Tier 2 syscall purity — confirm.
3. Should `ls` default hide dotfiles (classic) and require `-a` to show?
4. Should tools support `--help` universally?
5. Maximum fixed buffer sizes (IO, getcwd).

