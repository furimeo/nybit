CC ?= gcc
CFLAGS ?= -std=c23 -Wall -Wextra -Werror -g -Iinclude
BIN_DIR = bin

NYGEN_SRCS = $(filter-out src/main.c, $(filter-out src/nyjit/%.c, $(wildcard src/*.c src/*/*.c)))
NYJIT_SRCS = $(wildcard src/nyjit/*.c)
TEST_SRCS = $(wildcard tests/*.c)

all: $(BIN_DIR)/nybit $(BIN_DIR)/test_runner

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/nybit: src/main.c $(NYGEN_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ src/main.c $(NYGEN_SRCS)

$(BIN_DIR)/test_runner: $(TEST_SRCS) $(NYGEN_SRCS) $(NYJIT_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests -o $@ $(TEST_SRCS) $(NYGEN_SRCS) $(NYJIT_SRCS)

test: $(BIN_DIR)/test_runner
	./$(BIN_DIR)/test_runner

clean:
	rm -rf $(BIN_DIR)

.PHONY: all test clean
