# Rivel build.
#
#   make            builds bin/rivelc, bin/qbe, and lib/rivel_rt.o
#   make test       runs the language test suite (tests/run.sh)
#   make test-examples checks standalone examples and the tic-tac-toe game
#   make lsp        builds the portable Node.js language server (Node 22+)
#   make test-lsp   tests compiler analysis and the LSP over stdio
#   make unit       builds and runs C unit tests (runtime and compiler)
#   make sanitize   rebuilds everything under ASan+UBSan into build/san, bin/san, lib/san
#   make format     runs clang-format over the sources
#   make clean

CC       ?= cc
CFLAGS   ?= -O2 -g
WARN     := -std=c11 -Wall -Wextra -Werror -pedantic -Wno-unused-parameter
DEFS     := -D_XOPEN_SOURCE=700 -D_DARWIN_C_SOURCE
BUILD    ?= build
BIN      ?= bin
LIB      ?= lib
RIVEL_HOME ?= $(abspath .)

SRC_DIRS     := base lex ast parse sema ir lower backend driver ide
COMPILER_SRC := src/main.c $(shell find $(addprefix src/,$(SRC_DIRS)) -name '*.c' 2>/dev/null | sort)
COMPILER_OBJ := $(COMPILER_SRC:%.c=$(BUILD)/%.o)
RUNTIME_OBJ  := $(LIB)/rivel_rt.o
DEPS         := $(COMPILER_OBJ:.o=.d) $(BUILD)/runtime/rivel_rt.d

.PHONY: all test test-examples test-lsp lsp unit sanitize format clean qbe

all: $(BIN)/rivelc $(BIN)/qbe $(RUNTIME_OBJ)

$(BIN)/rivelc: $(COMPILER_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(COMPILER_OBJ) -o $@ $(LDFLAGS) -lm

$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP $(WARN) $(DEFS) $(CFLAGS) -Isrc -DRIVEL_HOME='"$(RIVEL_HOME)"' -DRIVEL_BIN='"$(BIN)"' -DRIVEL_LIB='"$(LIB)"' -c $< -o $@

$(RUNTIME_OBJ): runtime/rivel_rt.c runtime/rivel_rt.h
	@mkdir -p $(dir $@) $(BUILD)/runtime
	$(CC) -MMD -MP -MF $(BUILD)/runtime/rivel_rt.d $(WARN) $(DEFS) $(CFLAGS) -c $< -o $@

# QBE's own Makefile produces third_party/qbe/qbe; local patches are documented
# in third_party/README.md.
# Keep its C99 dialect and define wrapping arithmetic for its integer constants.
qbe: $(BIN)/qbe
$(BIN)/qbe: $(wildcard third_party/qbe/*.c third_party/qbe/*/*.c third_party/qbe/*.h)
	@mkdir -p $(dir $@)
	$(MAKE) -C third_party/qbe CC="$(CC)" \
		CFLAGS="$(CFLAGS) -std=c99 -fwrapv -Wall -Wextra -Wpedantic" qbe
	cp third_party/qbe/qbe $@

lsp: $(BIN)/rivelc
	npm ci --prefix tools/lsp --ignore-scripts
	npm run build --prefix tools/lsp

test-lsp: lsp
	npm test --prefix tools/lsp

test: all
	@RIVEL_HOME="$(RIVEL_HOME)" RIVEL_BIN="$(BIN)" RIVEL_LIB="$(LIB)" tests/run.sh

test-examples: all
	@RIVEL_HOME="$(RIVEL_HOME)" RIVEL_BIN="$(BIN)" RIVEL_LIB="$(LIB)" bash examples/test.sh

UNIT_SRC := $(wildcard tests/unit/*.c)
UNIT_BIN := $(UNIT_SRC:tests/unit/%.c=$(BUILD)/tests/unit/%)
COMPILER_OBJ_NOMAIN := $(filter-out $(BUILD)/src/main.o,$(COMPILER_OBJ))

$(BUILD)/tests/unit/%: tests/unit/%.c $(COMPILER_OBJ_NOMAIN) $(RUNTIME_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(WARN) $(DEFS) $(CFLAGS) -Isrc -Iruntime $< $(COMPILER_OBJ_NOMAIN) $(RUNTIME_OBJ) -o $@ -lm

$(BUILD)/runtime/rt_test: runtime/rt_test.c $(RUNTIME_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(WARN) $(DEFS) $(CFLAGS) -Iruntime $< $(RUNTIME_OBJ) -o $@ -lm

unit: $(UNIT_BIN) $(BUILD)/runtime/rt_test
	@for t in $(BUILD)/runtime/rt_test $(UNIT_BIN); do echo "  RUN  $$t"; $$t || exit 1; done
	@echo "unit tests passed"

# The sanitized compiler and runtime are built into build/san, bin/san, lib/san.
# The language tests then run the sanitized compiler but link programs against
# the normal runtime, since a sanitized runtime cannot link without the
# sanitizer runtime and the collector's conservative stack scan does not mix
# with ASan's fake stacks. The runtime itself is tested under the sanitizers
# by rt_test.
sanitize:
	$(MAKE) BUILD=build/san BIN=bin/san LIB=lib/san \
		CFLAGS="-O1 -g -Wno-format-truncation -fsanitize=address,undefined -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address,undefined" all unit
	$(MAKE) $(RUNTIME_OBJ)
	$(MAKE) BUILD=build/san BIN=bin/san LIB=lib test

format:
	clang-format -i $(COMPILER_SRC) $(shell find $(addprefix src/,$(SRC_DIRS)) -name '*.h') runtime/rivel_rt.c runtime/rivel_rt.h runtime/rt_test.c tests/unit/*.c

clean:
	rm -rf $(BUILD) $(BIN) $(LIB) build bin lib
	-$(MAKE) -C third_party/qbe clean >/dev/null 2>&1

-include $(DEPS)
