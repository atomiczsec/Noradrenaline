MODULES := collection/ai_surface credential_access/cloud_metadata_check discovery/app_count discovery/mdm_policy_artifacts discovery/native_env discovery/netjoin_query discovery/os_version discovery/proxy_enum discovery/wallpaper_enum execution/firewall_rule
NATIVE_RUNNER_DIR := tests/native_runner
UNAME_S := $(shell uname -s)
PLATFORM ?= host

.PHONY: all build build-mac build-linux build-both artifacts macos linux linux-docker test clean $(MODULES)

build:
	@case "$(PLATFORM)" in \
		host) $(MAKE) all ;; \
		mac) \
			if [ "$(UNAME_S)" = "Darwin" ]; then \
				$(MAKE) macos; \
			else \
				echo "PLATFORM=mac requires a macOS host."; exit 1; \
			fi ;; \
		linux) \
			if [ "$(UNAME_S)" = "Darwin" ]; then \
				$(MAKE) linux-docker; \
			else \
				$(MAKE) linux; \
			fi ;; \
		both) \
			if [ "$(UNAME_S)" = "Darwin" ]; then \
				$(MAKE) artifacts; \
			else \
				$(MAKE) linux; \
				echo "Note: macOS artifacts require a macOS host."; \
			fi ;; \
		*) echo "Unknown PLATFORM=$(PLATFORM). Use host, mac, linux, or both."; exit 1 ;; \
	esac

build-mac:
	$(MAKE) build PLATFORM=mac

build-linux:
	$(MAKE) build PLATFORM=linux

build-both:
	$(MAKE) build PLATFORM=both

all:
	@for module in $(MODULES); do \
		$(MAKE) -C $$module all || exit $$?; \
	done

macos:
	@for module in $(MODULES); do \
		$(MAKE) -C $$module macos || exit $$?; \
	done

linux:
	@for module in $(MODULES); do \
		$(MAKE) -C $$module linux || exit $$?; \
	done

linux-docker:
	@for module in $(MODULES); do \
		$(MAKE) -C $$module linux-docker || exit $$?; \
	done

artifacts: macos linux-docker

test:
	$(MAKE) -C $(NATIVE_RUNNER_DIR) all

clean:
	@for module in $(MODULES); do \
		$(MAKE) -C $$module clean; \
	done
	$(MAKE) -C $(NATIVE_RUNNER_DIR) clean
