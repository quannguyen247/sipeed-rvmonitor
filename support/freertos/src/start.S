/*
 * FreeRTOS V202111.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://www.github.com/FreeRTOS
 *
 * 1 tab == 4 spaces!
 */

/* SPDX-License-Identifier: MIT
 * Modified by Nguyen Dong Quan, 2026: minimal reserved-RAM startup; D-cache off.
 */
/* Small adaptation of the pinned SDK startup, MIT. No DDR/PLL/UART changes. */
#include "riscv-reg.h"
.section .vectors,"ax"
.global _start
_start:
    csrw mie,zero
    csrci mstatus,8
    .option push
    .option norelax
    la gp,__global_pointer$
    .option pop
    la sp,_stack_top
    la t0,early_trap
    csrw mtvec,t0
    li t0,0x30013
    csrs mcor,t0
    /* I-cache/prediction on; D-cache off so reserved shared RAM stays coherent. */
    li t0,0x7d
    csrw mhcr,t0
    li t0,0x2000
    csrs mstatus,t0
    la t0,_bss
    la t1,_ebss
1:  bgeu t0,t1,2f
    sd zero,0(t0)
    addi t0,t0,8
    j 1b
2:  la t0,freertos_risc_v_trap_handler
    csrw mtvec,t0
    call main
3:  wfi
    j 3b
