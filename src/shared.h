/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
#ifndef RVNANO_SHARED_H
#define RVNANO_SHARED_H
#include <stdint.h>
#define RV_SHARED_ADDR 0x8ff00000UL
#define RV_SHARED_BYTES 0x100000UL
#define RV_MAGIC 0x52565453u
#define RV_VERSION 3u
#define RV_MAX_SAMPLES 10000u
#define RV_TIMER_HZ 25000000UL
#define RV_PERIOD_TICKS 25000UL
enum { RV_BOOT=1, RV_READY=2, RV_RUN=3, RV_DONE=4, RV_FAULT=5 };
enum { RV_CMD_START=1, RV_CMD_IDLE=2 };
struct rv_sample { uint64_t release, wake, finish; };
struct rv_shared {
    uint32_t magic, version, state, hart;
    uint32_t tick_hz, timer_hz, command, request;
    uint32_t acknowledge, count, completed, checksum;
    uint32_t ping_request, ping_reply, stack_free_words, assert_line;
    uint64_t ticks, first_compare, last_deadline;
    uint64_t fault_cause, fault_pc, fault_value;
    uint64_t ping_receive, ping_send;
    struct rv_sample samples[RV_MAX_SAMPLES];
    /* Append only: retain the v1 sample offsets for archived benchmarks. */
    uint32_t telemetry_seq, telemetry_reserved;
    uint64_t elapsed_ticks, idle_ticks;
    /* v3: an open WFI interval can be observed without waking this core. */
    uint64_t boot_time, idle_since, timer_irqs, wake_irqs;
    uint32_t sleep_mode, sleep_reserved;
};
_Static_assert(sizeof(struct rv_shared)<RV_SHARED_BYTES,"shared area overflow");
static inline void rv_fence(void) { __asm__ volatile("fence rw,rw" ::: "memory"); }
#endif
