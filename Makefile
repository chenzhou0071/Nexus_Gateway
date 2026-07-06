CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Werror -O2 -g -D_GNU_SOURCE -Iinclude
LDFLAGS = -lpthread

SRC_DIR   = src
OBJ_DIR   = obj
BIN_DIR   = bin
TEST_DIR  = tests
TEST_BIN  = $(BIN_DIR)/unit_tests

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
BIN  = $(BIN_DIR)/nexus_gateway

TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/%,$(TEST_SRCS))

.PHONY: all clean test bench

all: $(BIN)

$(BIN): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_%: $(TEST_DIR)/test_%.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	for t in $(TEST_BINS); do $$t || exit 1; done

bench:
	bash bench/bench.sh

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
