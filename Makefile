CC ?= gcc
CPPFLAGS ?= -MMD -MP
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -g
LDFLAGS ?=

BIN := rivel
BUILD_DIR := .build
SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

.PHONY: all clean sanitize

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

sanitize: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: clean all

clean:
	rm -f $(BIN)
	rm -rf $(BUILD_DIR)

-include $(DEP)
