# sysbox

A tiny, self-contained collection of Unix command-line tools for Linux x86_64.

## What is this?

sysbox is a minimalist reimplementation of common Unix utilities (`cat`, `ls`, `cp`, `rm`, `grep`, etc.) with a twist: **it uses no C library at all**. Every tool talks directly to the Linux kernel via syscalls.

The result: extremely small static binaries (typically 1–4 KB each) with zero external dependencies.

## Why?

Mostly as an exercise in minimalism. But also:

- **Understanding**: Building tools from scratch reveals how Unix actually works.
- **Size**: The entire toolset is smaller than a single coreutils binary.
- **Simplicity**: No glibc, no dynamic linking, no hidden complexity.

## Design principles

1. **Syscalls only** — No libc, no `printf`, no `malloc`. Just raw Linux syscalls via inline assembly.
2. **Static binaries** — Each tool is a standalone executable with no runtime dependencies.
3. **Small by default** — Compiled with `-Os`, link-time optimization, and a custom linker script.
4. **Good enough** — These tools cover common use cases, not every edge case. They're intentionally incomplete.
5. **Single platform** — Linux on x86_64 only. No portability layer, no abstractions.

## What's included?

55 tools as of December 2025:

| Category | Tools |
|----------|-------|
| Basics | `true`, `false`, `echo`, `yes`, `sleep`, `pwd` |
| Files | `cat`, `head`, `tail`, `tee`, `cp`, `mv`, `rm`, `ln`, `touch`, `mkdir`, `rmdir`, `readlink`, `realpath` |
| Text | `sort`, `uniq`, `grep`, `tr`, `cut`, `wc`, `sed`, `awk` |
| Info | `ls`, `stat`, `df`, `basename`, `dirname`, `uname`, `date`, `id`, `whoami`, `which`, `nproc`, `hostname` |
| Scripting | `sh`, `env`, `test`/`[`, `printf`, `seq`, `xargs`, `find` |
| Permissions | `chmod`, `chown` |
| System | `kill`, `ps`, `who`, `time`, `du`, `clear`, `cmp`, `diff` |

## Limitations

These tools are **not** drop-in replacements for GNU coreutils or BusyBox. They have intentional limitations:

- **No long options** — Only short flags like `-r`, not `--recursive`.
- **No locales** — Everything is bytewise; no Unicode collation.
- **Fixed buffers** — Some tools have size limits (e.g., `sort` caps at 4 MB of input).
- **Minimal error messages** — Errors print errno numbers, not human-readable strings.
- **Missing features** — Many common flags aren't implemented.
- **Conservative recursion & symlinks** — Recursive operations have fixed depth limits, and tools avoid following symlinks (e.g. `cp` copies symlinks as symlinks).

For details, see [spec.md](spec.md) and [status.md](status.md).

## Building

```sh
make          # Build all tools to bin/
make test     # Run the test suite
make clean    # Remove build artifacts
```

Requirements:
- GCC or Clang with x86_64 support
- GNU Make
- Linux (building and running)

Optional: Install `sstrip` from [ELFkickers](https://www.muppetlabs.com/~breadbox/software/elfkickers.html) to shrink binaries further.

## Binary sizes

Typical sizes (with LTO, stripped):

```
bin/true       440 bytes
bin/false      440 bytes
bin/echo       760 bytes
bin/cat       1.1 KB
bin/ls        2.4 KB
bin/grep      2.8 KB
bin/cp        3.2 KB
```

## Project structure

```
sysbox/
├── src/
│   ├── sb.h         # Syscall wrappers and types
│   ├── sb.c         # Helper functions (strlen, write_all, etc.)
│   └── sb_start.c   # Freestanding _start entrypoint
├── tools/
│   ├── cat.c        # One file per tool
│   ├── ls.c
│   └── ...
├── scripts/
│   └── minimal.ld   # Custom linker script for small ELFs
├── tests/
│   └── run.sh       # Shell-based test suite
├── spec.md          # Detailed specifications
└── status.md        # Implementation status tracker
```

## How it works

Instead of linking against glibc, sysbox provides its own minimal runtime:

1. **`_start`** — A tiny entrypoint (in `sb_start.c`) that parses the kernel-provided stack to extract `argc`, `argv`, and `envp`, then calls `main()`.

2. **Syscall wrappers** — Inline assembly functions (in `sb.h`) that invoke Linux syscalls directly using the `syscall` instruction.

3. **Helpers** — A small set of utility functions (`strlen`, `write_all`, integer formatting) that avoid any libc dependency.

The custom linker script (`scripts/minimal.ld`) reduces ELF overhead by merging segments and discarding unnecessary sections.

## License

**CC0 1.0 Universal (Public Domain)**

This project is released into the public domain. You can copy, modify, and distribute it without permission or attribution.

The majority of this code was generated with assistance from large language models (LLMs). Given the nature of LLM-generated code, copyright protection is questionable anyway. Consider it a gift to the commons.

See [LICENSE](LICENSE) for the full CC0 text.

## Author

Created by Mathias with substantial help from Claude (Anthropic) and GPT-5.2 (Preview) using GitHub Copilot in VS Code.