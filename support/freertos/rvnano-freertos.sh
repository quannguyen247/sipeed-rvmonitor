#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Nguyen Dong Quan
set -eu
R=/sys/class/remoteproc/remoteproc0
CTL=/opt/rvnano/freertos/rtos_ctl
test "$(uname -r)" = 5.10.271-rvnano
test "$(cat "$R/name")" = cv181x-c906_1
case "${1:-}" in
 start)
  if grep -q '^soph_rtos_cmdqu ' /proc/modules; then
   echo 'C906L is reserved for the ISP/legacy RTOS mailbox driver' >&2
   exit 1
  fi
  test -f /lib/firmware/rvnano-freertos.elf
  test "$(cat "$R/state")" = offline
  printf '%s\n' rvnano-freertos.elf > "$R/firmware"
  printf '%s\n' start > "$R/state"
  sleep 1
  if ! "$CTL" ready; then
   printf '%s\n' stop > "$R/state"
   exit 1
  fi
  ;;
 stop)
  if test "$(cat "$R/state")" = running; then printf '%s\n' stop > "$R/state"; fi
  ;;
 status) "$CTL" status ;;
 *) echo 'usage: rvnano-freertos start|stop|status' >&2;exit 2 ;;
esac
