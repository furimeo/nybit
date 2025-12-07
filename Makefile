# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Le Hung Quang Minh (furimeo)

CC ?= gcc
CFLAGS ?= -std=c23 -Wall -Wextra -Werror -g -Iinclude -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
AR ?= ar
BIN_DIR = bin

NYGEN_SRCS = $(shell find src -type f -name '*.c' ! -name 'main.c' ! -path 'src/nyjit/*' ! -path 'src/nylink/*')
NYJIT_SRCS = $(shell find src/nyjit -type f -name '*.c')
NYLINK_SRCS = $(shell find src/nylink -type f -name '*.c')
TEST_SRCS = $(shell find tests -type f -name '*.c' ! -name 'bench_main.c')

all: $(BIN_DIR)/nybit $(BIN_DIR)/test_runner

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/nybit: src/main.c $(NYGEN_SRCS) $(NYLINK_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ src/main.c $(NYGEN_SRCS) $(NYLINK_SRCS)

$(BIN_DIR)/test_runner: $(TEST_SRCS) $(NYGEN_SRCS) $(NYJIT_SRCS) $(NYLINK_SRCS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests -o $@ $(TEST_SRCS) $(NYGEN_SRCS) $(NYJIT_SRCS) $(NYLINK_SRCS)

test: $(BIN_DIR)/test_runner
	./$(BIN_DIR)/test_runner

clean:
	rm -rf $(BIN_DIR)

.PHONY: all test clean
