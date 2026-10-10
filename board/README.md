# Board integration

These files support the prepared Ubuntu 22.04.5 system with Linux
`5.10.271-rvnano`, version `#4 Fri Oct 9 02:40:26 +07 2026`.
CPU1 uses FreeRTOS 10.4.6, shared ABI v3 and a 2 MiB carveout at `0x8fe00000`.
Stock images require matching kernel/device-tree integration before use.

## Required files

| File on board | Source |
| --- | --- |
| `/usr/local/bin/rvmonitor` | `make install` |
| `/opt/rvnano/freertos/{rtos_ctl,linux_pid}` | `make install` |
| `/opt/rvnano/rvmonitor/soph_tpu-monitor.ko` | `make module`, same kernel ABI |
| `/lib/firmware/rvnano-freertos.elf` | `make firmware` |
| `/usr/local/sbin/rvnano-freertos` | `board/rvnano-freertos.sh` |
| `/etc/systemd/system/rvnano-freertos.service` | Supplied service |
| `/opt/rvnano-npu/tensor-mul-selftest` | External Cvitek 4.1.0 SDK INT8 engine |

The NPU test engine is not bundled. Its source is
[sophgo/cviruntime](https://github.com/sophgo/cviruntime/tree/ef8044988c2b4a5d491125d13e6f048b5f8a1389/test/181x),
`test_181x_tensor_mul.c` and `test_cvikernel_util.*`, linked with matching
`runtime`, `cvikernel` and `ionutils`. The accepted engine SHA-256 is
`9237777676b52fb07a60e31e11ce268b946e6bf106b2b42ca54d0c49d17e43cd`.

## NPU module

Use [scpcom/sophgo-osdrv](https://github.com/scpcom/sophgo-osdrv/tree/3885436de4bcd2976652c762f4273f210d3c2887)
with an already configured kernel source/build tree. From the repository root:

```sh
OSDRV_DIR=/path/to/osdrv KERNEL_SRC=/path/to/linux \
KERNEL_BUILD=/path/to/kernel-build \
CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl- make module
make -B CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl- NPU_SHA256=MODULE_SHA256
```

`make module` prints the hash and writes `build/soph_tpu-monitor.ko`.
Use that hash only after checking the kernel/driver ABI. Copy the module to
the path above, then install the rebuilt monitor. The default hash is bound
to the existing validated module. Remove `build/npu-module` before rebuilding
against changed inputs. The build patches a copy, preserving the supplied BSP.

## CPU1 firmware

Use the `freertos` tree from
[sipeed/LicheeRV-Nano-Build@d4003f1](https://github.com/sipeed/LicheeRV-Nano-Build/tree/d4003f15b35d43ad4842f427050ab2bba0114fa5).
The build checks the input hashes in `vendor-manifest.json`.

```sh
FREERTOS_DIR=/path/to/LicheeRV-Nano-Build/freertos \
CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl- make firmware
```

The kernel needs the [upstream remoteproc integration](https://github.com/scpcom/sophgo-sg200x-debian/blob/6f102cbb1f490046603e5a458d81e244e00f4bbd/configs/chip/sg200x/patches/linux/0003-Add-Remoteproc-Driver-for-cvitek.patch),
then `remoteproc-prepare.patch` followed by `remoteproc-wake.patch`.
Reserved RAM, reset, clocks and mailbox wiring must match the firmware.
The legacy `soph_rtos_cmdqu` ISP owner must be disabled before starting CPU1.

On the matching prepared board, as root:

```sh
install -m 0644 build/freertos/rvnano-freertos.elf /lib/firmware/
install -m 0755 board/rvnano-freertos.sh /usr/local/sbin/rvnano-freertos
install -m 0644 board/rvnano-freertos.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now rvnano-freertos
```

Do not install firmware built for another memory layout. Idle disables timer
interrupts and waits in WFI for the mailbox; reading telemetry does not wake CPU1.
