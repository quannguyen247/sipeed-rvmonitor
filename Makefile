# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Nguyen Dong Quan
# Native build on the board; set CROSS_COMPILE for a PC cross toolchain.
CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip
BUILD_DIR ?= build
DESTDIR ?=
# Verified 5.10.271-rvnano #4 telemetry module.
NPU_SHA256 ?= 99524ffa454fc2646b73320fddbfa0130bab9f250409eced727e9f670844a655
CFLAGS = -march=rv64gc -mabi=lp64d -O2 -static -Wall -Wextra -Werror
CPPFLAGS += -Isrc
.PHONY: all install module firmware test clean FORCE
all: $(BUILD_DIR)/rvmonitor $(BUILD_DIR)/rtos_ctl $(BUILD_DIR)/linux_pid
$(BUILD_DIR):
	mkdir -p "$@"
FORCE:
$(BUILD_DIR)/npu.sha256: FORCE | $(BUILD_DIR)
	@printf '%s\n' '$(NPU_SHA256)' | LC_ALL=C grep -Eq '^[0-9a-f]{64}$$'
	@printf '%s\n' '$(NPU_SHA256)' > "$@.tmp"
	@if cmp -s "$@.tmp" "$@"; then rm -f -- "$@.tmp"; else mv -f -- "$@.tmp" "$@"; fi
$(BUILD_DIR)/rvmonitor: src/rvmonitor.c src/shared.h src/pid.h Makefile $(BUILD_DIR)/npu.sha256 | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) '-DRVMON_NPU_SHA256="$(NPU_SHA256)"' "$<" -o "$@"
	$(STRIP) --strip-all "$@"
$(BUILD_DIR)/rtos_ctl: src/rtos_ctl.c src/shared.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) "$<" -o "$@"
	$(STRIP) --strip-all "$@"
$(BUILD_DIR)/linux_pid: src/linux_pid.c src/pid.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) "$<" -o "$@"
	$(STRIP) --strip-all "$@"
install: all
	install -D -m 0755 "$(BUILD_DIR)/rvmonitor" "$(DESTDIR)/usr/local/bin/rvmonitor"
	install -D -m 0755 "$(BUILD_DIR)/rtos_ctl" "$(DESTDIR)/opt/rvnano/freertos/rtos_ctl"
	install -D -m 0755 "$(BUILD_DIR)/linux_pid" "$(DESTDIR)/opt/rvnano/freertos/linux_pid"
module:
	python3 board/build_npu.py
firmware:
	python3 board/build_firmware.py
test:
	python3 -m unittest discover -s tests -v
clean:
	rm -rf -- "$(BUILD_DIR)"
