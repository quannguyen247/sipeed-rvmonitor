/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { FW (rwx) : ORIGIN = 0x8fe00000, LENGTH = 0x100000 }
SECTIONS {
 .vectors : { KEEP(*(.vectors)) } > FW
 .text : { *(.text .text.*) } > FW
 .resource_table : ALIGN(8) { KEEP(*(.resource_table)) } > FW
 .rodata : { *(.rodata .rodata.*) } > FW
 .data : ALIGN(8) { __global_pointer$ = . + 0x800; *(.sdata .sdata.* .data .data.*) } > FW
 .bss (NOLOAD) : ALIGN(8) { _bss = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(8); _ebss = .; } > FW
 .stack (NOLOAD) : ALIGN(16) { . += 0x4000; _stack_top = .; } > FW
 /DISCARD/ : { *(.comment .eh_frame .note.*) }
 ASSERT(_stack_top < 0x8ff00000,"firmware overlaps shared RAM")
}
