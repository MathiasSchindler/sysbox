# sysbox — syscall-only “coreutils-ish” tools (Linux x86_64)

Date: 2025-12-13

## 0. Purpose
Build a small, fast, minimal set of command-line tools for a single target:

- OS: Linux
- Arch: x86_64
- Language: C
- Dependencies: syscalls only (no third-party libs; avoid libc helpers in tool logic)

This project is explicitly **not** trying to be POSIX-complete or portable. It is a deliberately minimalist toolbox for a single machine.

## 1. Documentation contract
This repo uses multiple documents with different roles:

- This file (`spec.md`) describes goals, constraints, and architectural decisions.
- `status.md` is the canonical description of what each tool currently supports and what remains missing.
- `README.md` is a high-level overview.

## 2. Non-goals
- Cross-platform support (BSD/macOS/Windows)
- Cross-architecture support (ARM, 32-bit)
- Full GNU coreutils compatibility
- Locale/i18n, wide characters, multibyte correctness beyond “treat UTF-8 as bytes”
- Fancy UX (colors, paging, interactive prompts by default)
- Filesystem feature completeness (ACLs, xattrs, SELinux labels)

## 3. Design principles
1. **Syscall-first**: implement behavior using Linux syscalls directly.
2. **Minimal bytes**: optimize for small static binaries; keep code small.
3. **Predictable performance**: avoid allocations; stream data; minimize copies.
4. **Explicit limitations**: document what’s intentionally unsupported.
5. **Simple parsing**: small flag subsets; keep behavior consistent once settled.
6. **Consistent exit codes**: 0 success, 1 operational failure, 2 usage error.
7. **No global state**: no hidden caches; no background daemons.

Stability note:
- This repo is under active development and currently has **no production users**.
- Until behavior is considered “settled”, flags and edge-case behavior may change.

## 4. Project shape (current)
Packaging decision: **many tiny binaries** (one `.c` per tool) with a shared internal layer linked into every binary.

- Tool implementations live in `tools/*.c`.
- The tiny shared layer lives in `src/sb.[ch]` and provides:
  - syscall wrappers
  - tiny string/memory helpers
  - numeric parsing helpers
  - error printing without strerror tables
  - a small `getdents64` directory iteration primitive
  - other shared primitives where duplication is clearly harmful

The goal of the shared layer is *deduplication and correctness*, not building a large framework.

## 5. Build & linking strategy
This repo targets a “Tier 2” stance: **truly syscall-only**, freestanding, and (preferably) statically linked.

- No libc usage in tool logic (no `printf`, `malloc`, `strerror`, ...).
- Minimal runtime entrypoint (`_start`) is provided by the project.
- Link with `-nostdlib` and a minimal linker script to reduce ELF overhead.

### Build knobs (canonical: Makefile)
The authoritative configuration is the `Makefile`. Notable knobs and constraints:

- Default LTO is enabled (can be disabled with `LTO=0`).
- `main` is marked `__attribute__((used))` to avoid LTO dead-stripping.
- CET is disabled (`-fcf-protection=none`) to avoid extra note sections and pads.
- Minimal linker scripts:
  - `scripts/minimal.ld` (smallest; single RWX PT_LOAD; linker warns)
  - `scripts/minimal_wx.ld` (W^X split segments; slightly larger)
- Optional `make tiny` runs `sstrip` if available.

## 6. Kernel ABI & syscalls
Target: Linux x86_64 syscall ABI.

Common syscall families used across tools:
- IO: `read`, `write`, `close`, `openat`, `lseek`
- FS metadata: `newfstatat`, `fchmodat`, `fchownat`, `utimensat`, `statfs`
- Directories: `getdents64`
- Links/paths: `linkat`, `symlinkat`, `readlinkat`, `unlinkat`, `renameat`
- Process/system: `execve`, `vfork`, `wait4`, `clock_gettime`, `nanosleep`

Syscall conventions:
- Errors are returned as negative `-errno`.
- Tools print errors without libc strerror lookups.

## 7. CLI & behavior conventions
This section defines repo-wide conventions; tool-specific behavior lives in `status.md`.

- Prefer short options; support `--` to end option parsing where relevant.
- If a tool reads input and no files are given: read stdin.
- `-` as an operand means stdin/stdout where it makes sense.
- Error messages should be short and stable.

### Exit codes
- `0`: success
- `1`: operational error (I/O error, missing file, partial failure)
- `2`: usage error (invalid flags, missing required args)

If multiple operands are processed, continue where safe but return `1` if any failure occurred.

## 8. Security model
- Not intended to be setuid/setgid.
- Prefer `*at()` syscalls to reduce TOCTOU.
- Avoid following symlinks in destructive operations unless explicitly required.

## 9. Testing strategy (lightweight)
- Shell-based integration tests under `tests/`.
- Keep tests deterministic and fast (avoid long sleeps).

## 10. Style conventions
- C dialect: C11
- No dynamic allocation in applets.
- All outputs via direct `write(1,...)` / `write(2,...)` wrappers.
- Avoid `stdio.h` entirely.

## 11. Known limitations
- No locale-aware behavior.
- Fixed buffer sizes (documented in code and bounded in behavior).
- Error strings are minimal (errno numbers rather than strerror text).

