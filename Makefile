CC ?= gcc
CFLAGS ?= -std=c23 -Wall -Wextra -Werror -g -Iinclude
BIN_DIR = bin

CORE_SRCS = $(filter-out src/main.c, $(wildcard src/*.c src/*/*.c))
TEST_SRCS = $(wildcard tests/*.c)

all: $(BIN_DIR)/nybit $(BIN_DIR)/test_runner

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/nybit: src/main.c $(CORE_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ src/main.c $(CORE_SRCS)

$(BIN_DIR)/test_runner: $(TEST_SRCS) $(CORE_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests -o $@ $(TEST_SRCS) $(CORE_SRCS)

test: $(BIN_DIR)/test_runner
	./$(BIN_DIR)/test_runner

clean:
	rm -rf $(BIN_DIR)

.PHONY: all test clean
