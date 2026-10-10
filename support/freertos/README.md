# Optional C906L firmware integration

The custom application uses the pinned FreeRTOS kernel 10.4.6, its THead port,
static tasks, a 1 kHz control timer, and the shared protocol ABI v3.
When all tasks wait indefinitely, timer IRQs are disabled and WFI waits for the
mailbox interrupt. Monitoring shared RAM does not wake the core.

The startup adaptation is MIT; the original custom application and helpers are
Apache-2.0. External kernel/port copyright is retained; see ../../LICENSES/MIT.txt.

Build with an existing FreeRTOS tree matching vendor-manifest.json:

```sh
FREERTOS_DIR=/path/to/LicheeRV-Nano-Build/freertos \
CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl- make firmware
make helpers CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl-
```

The build checks all 47 pinned input hashes. No download, board install, reboot,
or firmware start is performed by these targets. Kernel-side remoteproc prepare
and wake patches are in ../../kernel/remoteproc and retain GPL-2.0-only.

The carveout, reset/clock/mailbox wiring and firmware need matching integration.
Do not run this application alongside the legacy ISP soph_rtos_cmdqu owner.
The unit and wrapper are reference deployment files for the validated system,
not an automatic installer for arbitrary vendor images.
