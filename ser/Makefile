CC=gcc
CFLAGS=-std=c2x -pedantic -g -Wall -fsanitize=address
LDFLAGS=-lm

BUILD_DIR=./objects
BIN=./ser
SOURCES=$(wildcard *.c)

OBJECTS:=$(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS:=$(patsubst %.c,$(BUILD_DIR)/%.d,$(SOURCES))

.PHONY: run build clean

run: $(BIN)
	@echo "[exec] $<"
	$(BIN)
build: $(BIN)

-include $(DEPS)

$(BIN): $(OBJECTS)
	@echo "[ld] $@"
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "[cc] $<"
	$(CC) -MMD $(CFLAGS) -c $< -o $@
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN)
