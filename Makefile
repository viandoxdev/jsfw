Q=@
CC=gcc

GCCCFLAGS=-Wno-format-truncation 
CFLAGS=-std=gnu11 -pedantic -g -Wall -pthread -D_GNU_SOURCE -fsanitize=address
LDFLAGS=-lm

# The root for /sys and /dev needs to be moved in docker, this should stay empty in most cases
FSROOT=""

# enable VERBOSE if VERBOSE=1
ifeq ($(VERBOSE),1)
	CFLAGS+=-DVERBOSE
endif

ifdef FSROOT
	CFLAGS+=-D_FSROOT=$(FSROOT)
endif

BUILD_DIR=./objects
BIN=jsfw

RUNARGS=server 7776 ./server_config.json

SOURCES=$(wildcard *.c)

OBJECTS:=$(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))

ifeq ($(CC),gcc)
	CFLAGS:=$(CFLAGS) $(GCCCFLAGS)
endif

.PHONY: run
run: $(BIN)
	@echo "RUN   $(BIN) $(RUNARGS)"
	$(Q) chmod +x $(BIN)
	$(Q) ./$(BIN) $(RUNARGS)

$(BIN): $(OBJECTS)
	@echo "LD    $@"
	$(Q) $(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

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

