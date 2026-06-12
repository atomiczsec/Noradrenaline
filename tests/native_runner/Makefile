CC ?= clang
BUILD_DIR ?= build
CFLAGS := -Wall -Wextra -O2
LDFLAGS :=
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
	LDFLAGS += -ldl
endif

.PHONY: all clean

all: $(BUILD_DIR)/native_runner

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/native_runner: native_runner.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) native_runner.c -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
