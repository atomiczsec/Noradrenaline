MODULE := launchd_enum
SRC := $(MODULE).c
BUILD_DIR ?= build

CC_MACOS ?= clang
MACOS_MIN ?= 11.0
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
	CC_LINUX_X64 ?= x86_64-linux-gnu-gcc
else
	CC_LINUX_X64 ?= gcc
endif
CC_LINUX_ARM64 ?= aarch64-linux-gnu-gcc

CFLAGS_COMMON := -Wall -Wextra -O2 -fvisibility=hidden
CFLAGS_MACOS := $(CFLAGS_COMMON) $(CFLAGS_EXTRA) -mmacosx-version-min=$(MACOS_MIN)
CFLAGS_LINUX := $(CFLAGS_COMMON) $(CFLAGS_EXTRA) -fPIC

.PHONY: all macos arm64 x86_64 universal linux linux-docker linux-x86_64 linux-arm64 linux-x86_64-docker linux-arm64-docker test clean

all:
	@if [ "$(UNAME_S)" = "Darwin" ]; then \
		$(MAKE) macos; \
	else \
		$(MAKE) linux; \
	fi

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

macos: universal

arm64: $(BUILD_DIR)
	$(CC_MACOS) $(CFLAGS_MACOS) -arch arm64 -dynamiclib $(SRC) -o $(BUILD_DIR)/$(MODULE)-macos-arm64.dylib

x86_64: $(BUILD_DIR)
	$(CC_MACOS) $(CFLAGS_MACOS) -arch x86_64 -dynamiclib $(SRC) -o $(BUILD_DIR)/$(MODULE)-macos-x86_64.dylib

universal: arm64 x86_64
	lipo -create $(BUILD_DIR)/$(MODULE)-macos-arm64.dylib $(BUILD_DIR)/$(MODULE)-macos-x86_64.dylib -output $(BUILD_DIR)/$(MODULE)-macos-universal.dylib

linux:
	@echo "Skipping $(MODULE): macOS-only module"

linux-docker: linux

linux-x86_64: linux

linux-arm64: linux

linux-x86_64-docker: linux

linux-arm64-docker: linux

test:
	@if [ "$(UNAME_S)" != "Darwin" ]; then \
		echo "Skipping $(MODULE) test: requires macOS"; \
	else \
		./test_native.sh; \
	fi

clean:
	rm -rf $(BUILD_DIR)
