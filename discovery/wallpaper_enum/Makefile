MODULE := wallpaper_enum
SRC := $(MODULE).c
MACOS_SRCS := $(MODULE).c $(MODULE)_darwin.m
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
MACOS_LDFLAGS := -framework Foundation -framework AppKit

.PHONY: all macos arm64 x86_64 universal linux linux-docker linux-x86_64 linux-arm64 linux-x86_64-docker linux-arm64-docker clean

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
	$(CC_MACOS) $(CFLAGS_MACOS) -arch arm64 -dynamiclib $(MACOS_SRCS) -o $(BUILD_DIR)/$(MODULE)-macos-arm64.dylib $(MACOS_LDFLAGS)

x86_64: $(BUILD_DIR)
	$(CC_MACOS) $(CFLAGS_MACOS) -arch x86_64 -dynamiclib $(MACOS_SRCS) -o $(BUILD_DIR)/$(MODULE)-macos-x86_64.dylib $(MACOS_LDFLAGS)

universal: arm64 x86_64
	lipo -create $(BUILD_DIR)/$(MODULE)-macos-arm64.dylib $(BUILD_DIR)/$(MODULE)-macos-x86_64.dylib -output $(BUILD_DIR)/$(MODULE)-macos-universal.dylib

linux: linux-x86_64 linux-arm64

linux-docker: linux-x86_64-docker linux-arm64-docker

linux-x86_64: $(BUILD_DIR)
	@if command -v $(CC_LINUX_X64) >/dev/null 2>&1; then \
		$(CC_LINUX_X64) $(CFLAGS_LINUX) -shared $(SRC) -o $(BUILD_DIR)/$(MODULE)-linux-x86_64.so; \
	else \
		echo "Skipping linux-x86_64: $(CC_LINUX_X64) not found"; \
	fi

linux-arm64: $(BUILD_DIR)
	@if command -v $(CC_LINUX_ARM64) >/dev/null 2>&1; then \
		$(CC_LINUX_ARM64) $(CFLAGS_LINUX) -shared $(SRC) -o $(BUILD_DIR)/$(MODULE)-linux-arm64.so; \
	else \
		echo "Skipping linux-arm64: $(CC_LINUX_ARM64) not found"; \
	fi

linux-x86_64-docker: $(BUILD_DIR)
	docker run --rm -v "$$(pwd)":/src -w /src --platform linux/amd64 -e DEBIAN_FRONTEND=noninteractive debian:stable-slim sh -c "apt-get update >/dev/null && apt-get install -y --no-install-recommends gcc libc6-dev >/dev/null && gcc $(CFLAGS_LINUX) -shared $(SRC) -o $(BUILD_DIR)/$(MODULE)-linux-x86_64.so"

linux-arm64-docker: $(BUILD_DIR)
	docker run --rm -v "$$(pwd)":/src -w /src --platform linux/arm64 -e DEBIAN_FRONTEND=noninteractive debian:stable-slim sh -c "apt-get update >/dev/null && apt-get install -y --no-install-recommends gcc libc6-dev >/dev/null && gcc $(CFLAGS_LINUX) -shared $(SRC) -o $(BUILD_DIR)/$(MODULE)-linux-arm64.so"

clean:
	rm -rf $(BUILD_DIR)
