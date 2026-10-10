#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Nguyen Dong Quan
"""Build pinned vendor FreeRTOS with existing Xuantie tools; no board access."""
import hashlib
import os
import json
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parent
PROJECT=ROOT.parent
SRC=ROOT
V=Path(os.environ['FREERTOS_DIR']).resolve()
B=PROJECT/'build/freertos'; B.mkdir(parents=True,exist_ok=True)
PREFIX=os.environ.get('CROSS_COMPILE','riscv64-linux-gnu-')
def tool(name): return str(PREFIX)+name
def execute(cmd,name):
    r=subprocess.run(cmd,capture_output=True)
    (B/name).write_bytes((' '.join(cmd)+'\n').encode()+r.stdout+r.stderr)
    r.check_returncode()
manifest=json.loads((ROOT/'vendor-manifest.json').read_text())
for f in manifest['files']:
    assert hashlib.sha256((V/f['path']).read_bytes()).hexdigest()==f['sha256'],f['path']
PORT=V/'Source/portable/GCC/RISC-V'
# Preserve vendor original. Add wrap-safe compare writes and observable fatal traps.
asm=(PORT/'portASM.S').read_text()
old='''\t\t\tsw t2, 0(t0)\t\t\t\t/* Store ullNextTime into compare register L. */
\t\t\tsrli t3, t2, 32
\t\t\tsw t3, 4(t0)\t\t\t\t/* Store ullNextTime into compare register H. */'''
new='''\t\t\tli t4, -1
\t\t\tsw t4, 0(t0) /* Prevent a transient expired compare across low-word wrap. */
\t\t\tsrli t3, t2, 32
\t\t\tsw t3, 4(t0)
\t\t\tsw t2, 0(t0)'''
assert asm.count(old)==1
asm=asm.replace(old,new)
asm=asm.replace('\tj is_exception\t\t\t\t\t\t/* No other exceptions handled yet. */','\tload_x sp, xISRStackTop\n\tj early_trap')
asm=asm.replace('\tj as_yet_unhandled','\tload_x sp, xISRStackTop\n\tj early_trap')
(B/'portASM.S').write_text(asm)
inc=['-I'+str(p) for p in [PROJECT/'src',SRC,V/'Source/include',PORT,PORT/'chip_specific_extensions/thead_c906_rv64imafdc',V/'cvitek/arch/riscv64/include']]
flags=['-march=rv64imafdc','-mabi=lp64d','-mcmodel=medany','-msmall-data-limit=0','-O2','-g','-ffreestanding','-fno-builtin','-fno-common','-fno-stack-protector','-ffunction-sections','-fdata-sections','-DTHEAD_C906','-DRISCV_FPU','-Wall','-Wextra']
files=[SRC/'start.S',SRC/'firmware.c',V/'Source/tasks.c',V/'Source/list.c',V/'Source/queue.c',PORT/'port.c',B/'portASM.S']
objs=[]
for f in files:
    o=B/(f.stem+'.o');execute([tool('gcc'),*flags,*inc,'-c',str(f),'-o',str(o)],f.stem+'-compile.log');objs.append(str(o))
execute([tool('gcc'),*flags,'-nostdlib','-nostartfiles','-Wl,--gc-sections','-Wl,-Map,'+str(B/'firmware.map'),'-T',str(SRC/'link.ld'),*objs,'-lgcc','-o',str(B/'rvnano-freertos-debug.elf')],'link.log')
execute([tool('objcopy'),'--strip-debug',str(B/'rvnano-freertos-debug.elf'),str(B/'rvnano-freertos.elf')],'strip.log')
execute([tool('readelf'),'-h','-l','-S',str(B/'rvnano-freertos.elf')],'elf-layout.txt')
execute([tool('size'),str(B/'rvnano-freertos.elf')],'size.txt')
p=B/'rvnano-freertos.elf'
print(p.name,p.stat().st_size,hashlib.sha256(p.read_bytes()).hexdigest())
