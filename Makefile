Q=@
CC=gcc
CFLAGS=-g -Wall
LDFLAGS=
BUILD_DIR=./build
BIN=jsfw

RUNARGS=client /dev/input/js0 localhost 7776

SOURCES=main.c hid.c

OBJECTS:=$(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))

.PHONY: run
run: $(BIN)
	@echo "RUN   $(BIN) $(RUNARGS)"
	$(Q) chmod +x $(BIN)
	$(Q) ./$(BIN) $(RUNARGS)

$(BIN): $(OBJECTS)
	@echo "LD    $@"
	$(Q) $(CC) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "CC    $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(BUILD_DIR)
$(BUILD_DIR):
	@echo "MKDIR"
	$(Q) mkdir -p $(BUILD_DIR)

.PHONY: clean
clean:
	@echo "CLEAN"
	$(Q) rm -fr $(BUILD_DIR)
	$(Q) rm -fr $(BIN)

