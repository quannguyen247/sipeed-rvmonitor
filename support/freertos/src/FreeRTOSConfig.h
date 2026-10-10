/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdint.h>
#include <stddef.h>
#define configSYS_CLOCK_HZ 25000000UL
/* Vendor THead port reads rdtime; undefined MTIME selects its 32-bit IO path. */
#define configMTIMECMP_BASE_ADDRESS 0x74004000UL
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 4
#define configMINIMAL_STACK_SIZE 256
#define configMAX_TASK_NAME_LEN 12
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_IDLE_HOOK 1
#define configUSE_TICK_HOOK 1
#define configUSE_TICKLESS_IDLE 2
void rv_suppress_ticks(uint64_t expected);
#define portSUPPRESS_TICKS_AND_SLEEP(x) rv_suppress_ticks(x)
#define configUSE_TRACE_FACILITY 0
#define configUSE_MUTEXES 0
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TIMERS 0
#define configQUEUE_REGISTRY_SIZE 0
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configISR_STACK_SIZE_WORDS 512
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelete 0
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
void rv_assert(unsigned line) __attribute__((noreturn));
#define configASSERT(x) do { if (!(x)) rv_assert(__LINE__); } while (0)
#endif
