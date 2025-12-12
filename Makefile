# sysbox (Tier 2): syscall-only, freestanding, static (preferred)

CC ?= cc
LTO ?= 1

BUILD_DIR := build
BIN_DIR := bin
SRC_DIR := src
TOOLS_DIR := tools

# Linker script (override with: make LD_SCRIPT=scripts/minimal_wx.ld)
LD_SCRIPT ?= scripts/minimal.ld

# Optional post-link section-header stripping (ELFkickers `sstrip`).
# If present, it is used automatically to produce the smallest binaries.
# If absent, build still succeeds (prints at most one note).
SSTRIP ?= $(shell command -v sstrip 2>/dev/null)

# Determine whether we are building anything (vs only `make clean`).
BUILD_GOALS := $(strip $(if $(MAKECMDGOALS),$(filter-out clean,$(MAKECMDGOALS)),all))
ifeq ($(strip $(SSTRIP)),)
ifneq ($(BUILD_GOALS),)
$(info note: sstrip not found; skipping section-header stripping (install elfkickers for smaller binaries))
endif
endif

CFLAGS_COMMON := -std=c11 -Os \
	-ffreestanding -fno-builtin \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-stack-protector \
	-fcf-protection=none \
	-fno-ident \
	-fno-pic -fno-pie \
	-ffunction-sections -fdata-sections \
	-Wall -Wextra

# Some toolchains default to PIE; attempt to force non-PIE for simplest static build.
# -z,noseparate-code: merge segments to avoid 4KB page alignment padding (~7KB savings)
# --build-id=none: omit .note.gnu.build-id section
LDFLAGS_COMMON := -nostdlib -static -Wl,--gc-sections -Wl,-s \
	-Wl,-z,noseparate-code -Wl,--build-id=none -no-pie \
	-T $(LD_SCRIPT)

ifeq ($(LTO),1)
	CFLAGS_COMMON += -flto
	LDFLAGS_COMMON += -flto
endif

RUNTIME_OBJ := $(BUILD_DIR)/sb_start.o
LIB_OBJ := $(BUILD_DIR)/sb.o

TOOLS := true false echo cat pwd ls wc mkdir rmdir rm mv cp head tail sort uniq tee tr cut date sleep ln readlink basename dirname touch chmod chown printf yes seq uname stat df

.PHONY: all clean test tiny size size-short size-report size-report-short

SIZE_LINES ?= 200

.PHONY: test

all: $(TOOLS:%=$(BIN_DIR)/%)

$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@

$(RUNTIME_OBJ): $(SRC_DIR)/sb_start.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(LIB_OBJ): $(SRC_DIR)/sb.c $(SRC_DIR)/sb.h | $(BUILD_DIR)
	$(CC) $(CFLAGS_COMMON) -c $(SRC_DIR)/sb.c -o $@

$(BIN_DIR)/%: $(TOOLS_DIR)/%.c $(RUNTIME_OBJ) $(LIB_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS_COMMON) $< $(RUNTIME_OBJ) $(LIB_OBJ) $(LDFLAGS_COMMON) -o $@
	@if [ -n "$(SSTRIP)" ]; then $(SSTRIP) $@ >/dev/null 2>&1 || echo "note: sstrip failed on $@ (continuing)"; fi

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

test: all
	sh tests/run.sh

# Optional post-link shrinking: removes section headers (impacts tooling like readelf -S, size -A).
.PHONY: tiny
tiny: all
	@command -v sstrip >/dev/null 2>&1 \
		&& { echo "Running sstrip..."; sstrip $(BIN_DIR)/*; } \
		|| echo "sstrip not found; skipping"

size: size-report

size-short: size-report-short

size-report: all
	@echo "== file sizes (bytes) =="
	@wc -c $(BIN_DIR)/* | sort -n
	@echo
	@echo "== section sizes (if 'size' exists) =="
	@command -v size >/dev/null 2>&1 && size -A -x $(BIN_DIR)/* | cat || echo "(no 'size' in PATH)"
	@echo
	@echo "== text/data/bss totals (if 'size' exists) =="
	@command -v size >/dev/null 2>&1 && size $(BIN_DIR)/* | cat || echo "(no 'size' in PATH)"

size-report-short: all
	@$(MAKE) --no-print-directory size-report | { head -n $(SIZE_LINES); cat >/dev/null; }
