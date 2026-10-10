#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Nguyen Dong Quan
"""Reuse a supplied GPL BSP and prepared kernel; never load a module or access a board."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
root=Path(__file__).resolve().parent.parent
osdrv=Path(os.environ['OSDRV_DIR']).resolve()
kernel=Path(os.environ['KERNEL_SRC']).resolve()
kbuild=Path(os.environ['KERNEL_BUILD']).resolve()
prefix=os.environ.get('CROSS_COMPILE','riscv64-linux-gnu-')
out=root/'build/npu-module';out.mkdir(parents=True,exist_ok=True)
upstream=osdrv/'interdrv/tpu'
if not (upstream/'common/cvi_tpu_interface.c').is_file():raise SystemExit('OSDRV_DIR lacks interdrv/tpu')
shutil.copytree(upstream,out,dirs_exist_ok=True)
subprocess.run(['patch','--batch','--forward','-p1','-i',str(root/'kernel/npu/telemetry.patch')],cwd=out,check=True)
shutil.copy2(root/'kernel/npu/Makefile',out/'Makefile')
subprocess.run(['make','-C',str(kernel),'O='+str(kbuild),'M='+str(out),'ARCH=riscv',
                'CROSS_COMPILE='+prefix,'OSDRV_DIR='+str(osdrv),'-j2','modules'],check=True)
module=root/'build/soph_tpu-monitor.ko';shutil.copy2(out/'soph_tpu.ko',module)
digest=hashlib.sha256(module.read_bytes()).hexdigest()
(root/'build/module-manifest.json').write_text(json.dumps({'module':module.name,'sha256':digest,'bytes':module.stat().st_size},indent=2)+'\n')
print('NPU_SHA256='+digest)
print('Rebuild the monitor with this digest after reviewing kernel ABI and runtime compatibility.')
