# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Nguyen Dong Quan
CROSS_COMPILE ?= riscv64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip
BUILD_DIR ?= build
# Bound to the verified 5.10.271-rvnano #4 telemetry module. Override only after ABI review.
NPU_SHA256 ?= 99524ffa454fc2646b73320fddbfa0130bab9f250409eced727e9f670844a655
# Ignore unrelated host environment flags; explicit make CFLAGS=... still works.
CFLAGS = -march=rv64gc -mabi=lp64d -O2 -static -Wall -Wextra -Werror
CPPFLAGS += -Iinclude
.PHONY: all helpers module firmware test clean
all: $(BUILD_DIR)/rvmonitor
$(BUILD_DIR):
	mkdir -p "$@"
$(BUILD_DIR)/rvmonitor: src/rvmonitor.c include/shared.h include/pid.h Makefile | $(BUILD_DIR)
	@printf '%s\n' '$(NPU_SHA256)' | LC_ALL=C grep -Eq '^[0-9a-f]{64}$$'
	$(CC) $(CPPFLAGS) $(CFLAGS) '-DRVMON_NPU_SHA256="$(NPU_SHA256)"' "$<" -o "$@"
	$(STRIP) --strip-all "$@"
helpers: $(BUILD_DIR)/rtos_ctl $(BUILD_DIR)/linux_pid
$(BUILD_DIR)/rtos_ctl: support/freertos/src/rtos_ctl.c include/shared.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) "$<" -o "$@"
	$(STRIP) --strip-all "$@"
$(BUILD_DIR)/linux_pid: support/freertos/src/linux_pid.c include/pid.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) "$<" -o "$@"
	$(STRIP) --strip-all "$@"
module:
	python3 scripts/build_npu_module.py
firmware:
	python3 support/freertos/build.py
test:
	python3 -m unittest discover -s tests -v
clean:
	rm -rf -- "$(BUILD_DIR)"
