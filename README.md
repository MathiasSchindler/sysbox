# sysbox

A self-contained collection of Unix command-line tools for Linux x86_64.

## What is this?

sysbox is a reimplementation of common Unix utilities (`cat`, `ls`, `cp`, `rm`, `grep`, etc.) built without linking any C library. Every tool talks directly to the Linux kernel via syscalls.

Each tool is built as a static executable; runtime dependencies are limited to the Linux kernel.

## Why?

This project is mainly an exercise in building a usable Unix-like toolset with a restricted runtime environment.

- **Understanding**: Building tools from scratch exposes the syscall and file-descriptor model.
- **Constraints**: Freestanding + syscall-only changes how you structure code.
- **Experimentation**: Useful for testing what a shell and basic utilities need.

## Design principles

1. **Syscalls only** — No libc (no glibc), no stdio, no `malloc`. Tools call Linux syscalls via inline assembly.
2. **Static binaries** — Each tool is a standalone executable with no runtime dependencies.
3. **Size-oriented build** — Compiled with `-Os`, link-time optimization, and a custom linker script.
4. **Scope-limited** — Tools cover common use cases; many flags and edge cases are not implemented.
5. **Single platform** — Linux on x86_64 only. No portability layer, no abstractions.

## What's included?

68 binaries in `bin/` as of December 2025 (66 tool implementations, plus `realpath` and `[` as aliases):

| Category | Tools |
|----------|-------|
| Basics | `true`, `false`, `echo`, `yes`, `sleep`, `pwd` |
| Files | `cat`, `cp`, `mv`, `rm`, `ln`, `touch`, `mkdir`, `rmdir`, `readlink`, `realpath`, `chmod`, `chown` |
| Text | `head`, `tail`, `tee`, `sort`, `uniq`, `grep`, `tr`, `cut`, `wc`, `sed`, `awk`, `rev`, `strings`, `column`, `col`, `more`, `hexdump` |
| Info | `ls`, `stat`, `df`, `du`, `basename`, `dirname`, `uname`, `date`, `id`, `whoami`, `which`, `nproc`, `hostname` |
| Scripting | `sh`, `env`, `test`, `[`, `printf`, `seq`, `xargs`, `find` |
| System | `init`, `kill`, `ps`, `who`, `time`, `clear`, `cmp`, `diff`, `uptime`, `free`, `mount`, `watch` |

## Limitations

These tools implement a subset of GNU coreutils / BusyBox behavior:

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
make test     # Run the test suite (smoke + integration)
make clean    # Remove build artifacts
```

Requirements:
- GCC or Clang with x86_64 support
- GNU Make
- Linux (building and running)

Optional: Install `sstrip` from [ELFkickers](https://www.muppetlabs.com/~breadbox/software/elfkickers.html) to shrink binaries further.

## Binary sizes

Example sizes (with LTO, stripped):

```
bin/true       440 bytes
bin/false      440 bytes
bin/echo       760 bytes
bin/cat       1.1 KB
bin/ls        2.4 KB
bin/grep      2.8 KB
bin/cp        3.2 KB
```

To see the current sizes in your checkout, run `make size-report-short`.

## Project structure

```
sysbox/
├── bin/           # Built binaries
├── build/         # Build artifacts
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
│   ├── run.sh       # Test runner (host shell)
│   └── integration.sh # Runs tests using sysbox `sh` and sysbox tools
├── spec.md          # Detailed specifications
└── status.md        # Implementation status tracker
```

## How it works

Instead of linking against glibc, sysbox provides its own runtime support:

1. **`_start`** — A custom entrypoint (in `sb_start.c`) that parses the kernel-provided stack to extract `argc`, `argv`, and `envp`, then calls `main()`.

2. **Syscall wrappers** — Inline assembly functions (in `sb.h`) that invoke Linux syscalls directly using the `syscall` instruction.

3. **Helpers** — A small set of utility functions (`strlen`, `write_all`, integer formatting) that avoid any libc dependency.

The custom linker script (`scripts/minimal.ld`) reduces ELF overhead by merging segments and discarding unnecessary sections.

## License

**CC0 1.0 Universal (Public Domain)**

This project is released into the public domain. You can copy, modify, and distribute it without permission or attribution.

Allmost all of this code was generated with assistance from large language models (LLMs).

See [LICENSE](LICENSE) for the full CC0 text.

## Author

Created by Mathias with substantial help from Claude Opus 4.5 (Anthropic) and GPT-5.2 (Preview) using GitHub Copilot in VS Code.