# Build and integration

## Monitor

Use a RISC-V Linux compiler and static libc. The measured build used Xuantie
V2.6.1 / GCC 10.2.0, musl, rv64gc/lp64d and -O2. No curses, Python runtime,
GUI, inference framework or resident daemon is required by the monitor.

```sh
make CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl-
make helpers CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl-
make test
```

The default NPU digest binds the monitor to the validated telemetry module.
Do not change a digest to bypass an ABI check. Builds with a different compiler,
libc, kernel config or SDK may have different bytes and need fresh validation.
These commands build on the PC; they do not install anything onto the board.

## NPU telemetry module

Supply a matching prepared kernel source/build tree and the external osdrv BSP.
The checked source pin is scpcom/sophgo-osdrv commit
3885436de4bcd2976652c762f4273f210d3c2887. The telemetry patch applies inside
interdrv/tpu. The source remains GPL-2.0-or-later; it is not bundled as Apache.

```sh
OSDRV_DIR=/path/to/osdrv KERNEL_SRC=/path/to/linux \
KERNEL_BUILD=/path/to/kernel-build \
CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl- make module
make NPU_SHA256=THE_REVIEWED_MODULE_SHA256 CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl-
```

The helper copies only into build/, applies the patch there and builds an
out-of-tree module. Its output is build/soph_tpu-monitor.ko, whose module name
is soph_tpu. It never modifies the supplied BSP/kernel or loads the result.
It requires the original source; clean or choose a fresh build directory before
rebuilding the patch stage. No force-load or force-unload is supported.

## CPU1 firmware and kernel integration

See [FreeRTOS support](../support/freertos/README.md). Apply prepare.patch then
wake.patch from kernel/remoteproc to the matching kernel source (git apply -p1).
Build the module against the same kernel configuration and Module.symvers.
The GPL-2.0-only patches keep the auxiliary reset asserted during ELF load and
add the firmware-guarded wake mailbox attribute. The repository does not replace
the device tree, FIP, bootloader, kernel image or vendor package set.

## Prepared-board file layout

| Build artifact or dependency | Board path |
|---|---|
| build/rvmonitor | /usr/local/bin/rvmonitor |
| build/soph_tpu-monitor.ko | /opt/rvnano/rvmonitor/soph_tpu-monitor.ko |
| build/rtos_ctl | /opt/rvnano/freertos/rtos_ctl |
| build/linux_pid | /opt/rvnano/freertos/linux_pid |
| build/freertos/rvnano-freertos.elf | /lib/firmware/rvnano-freertos.elf |
| External validated INT8 engine | /opt/rvnano-npu/tensor-mul-selftest |

The two Linux helpers can be rebuilt from included source. The INT8 engine is
an external dependency from sophgo/cviruntime, pin
ef8044988c2b4a5d491125d13e6f048b5f8a1389, test/181x/test_181x_tensor_mul.c
and test/181x/test_cvikernel_util.*. It is not embedded or redistributed here.
The validated musl static build used the matching Cvitek 4.1.0 SDK and linked
runtime/cvikernel/ionutils. Its expected SHA is
9237777676b52fb07a60e31e11ce268b946e6bf106b2b42ca54d0c49d17e43cd.
The NPU test refuses an absent or mismatched engine. Rebuilding SDK components
is separate from compiling this monitor, and requires their own licenses/ABI checks.
