# sipeed-rvmonitor

A lightweight C terminal monitor for the **Sipeed Lichee RV Nano / SG2002**.
Watch the Linux C906 (CPU0), FreeRTOS C906L (CPU1) and NPU command activity,
with temperature, RAM used, configured clocks, self-tests and JSONL logging.

```text
rvmonitor  /  Lichee RV Nano SG2002
Linux 5.10.271-rvnano

TEMP  57.4 C    RAM USED  77.5 / 229.0 MiB

CORE                    LOAD                         %      MHz*  STATE
CPU0  C906 / Linux      [#############       ]   63.6%    1050.0  Running
CPU1  C906L / RTOS      [                    ]    0.0%     786.4  Idle
NPU   SG2002            [                    ]    0.0%     700.0  Idle

* configured clock    Sample: 1000 ms
s: sampling settings    q / Esc: exit
```

The snapshot above is an example frame from the recorded board test, not a live feed.
The supported system is the prepared `5.10.271-rvnano #4` stack with ABI v3;
see [compatibility](docs/compatibility.md) before using a different image.

## Run

Run as root on a prepared board:

```sh
rvmonitor
rvmonitor --nputest
rvmonitor --c0test
rvmonitor --c1test
rvmonitor --logging
rvmonitor --logging "/root/log.jsonl"
rvmonitor --logging "/root/log.jsonl" 20
rvmonitor --logging "/root/log.jsonl" 20 500
rvmonitor --help
```

`--logging ["PATH" [SAMPLES [INTERVAL_MS]]]` defaults to `/root/rvmonitor_log`,
10 samples, and 1000 ms. Interval is in **milliseconds**. Quote paths as shown;
the shell passes one path argument and removes quote syntax. Logs are JSONL,
one sample per line. A repeat run overwrites a regular file; concurrent writers
to that same file are refused before truncation. Parent directories must exist.

The dashboard defaults to one sample per second. Press `s` for 100/250/500/1000/
2000/5000 ms in this session; `q` or Esc exits. Esc inside settings returns.
All three rows use **Running / Idle / N/A**. The FreeRTOS numeric `C1_state`
in raw logs is a separate ABI field, not an additional display state.

Open multiple dashboards and loggers at different paths. A self-test can run
from another SSH terminal while they remain open; self-tests alone are serialized.
The last cooperating invocation unloads a driver the group loaded when safe.
The NPU wrapper is inside the C monitor; its validated external tensor engine
remains an explicit dependency. Normal monitoring runs no inference loop.

## Build

```sh
git clone https://github.com/quannguyen247/sipeed-rvmonitor.git
cd sipeed-rvmonitor
make CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl-
make helpers CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl-
make test
```

The measured static binary is about **74 KiB**. There is no curses dependency
or resident monitor daemon. [Build and prepared-board layout](docs/building.md)
describe the external kernel/BSP, optional firmware and NPU dependencies.
Build targets do not install, flash, reboot or load a module onto a board.

## Repository layout

| Path | Purpose |
|---|---|
| src/ and include/ | Monitor, shared ABI and integer PID oracle |
| kernel/npu/ | GPL telemetry patch and module build recipe |
| kernel/remoteproc/ | GPL auxiliary reset/wake integration patches |
| support/freertos/ | Custom application, Linux helpers and pinned-input build |
| scripts/ | Optional module builder using supplied external source trees |
| tests/ | Offline terminal/PID regression and sanitized fixtures |
| docs/ | Measurement definitions, compatibility, development and validation |

## Evidence and limits

See [measurement definitions](docs/architecture.md), [validation](docs/validation.md)
and [development stages](docs/development.md). NPU percentage measures command-path
busy time including DMA/wait; it is not TOPS. MHz is the configured CCF rate.
CPU1 idle means WFI, not zero electrical power. Finite PID tests do not establish
hard real-time bounds for all workloads. The repository import does not imply a
fresh board installation or reboot.

## License

Copyright **2026 Nguyen Dong Quan**. Original project code and documentation are
[Apache-2.0](LICENSE). Kernel patches retain GPL-2.0-only or GPL-2.0-or-later;
the SDK startup adaptation retains MIT. [NOTICE](NOTICE) and [LICENSES](LICENSES)
preserve those boundaries. External SDK sources/libraries are not relicensed or
redistributed here. This is an independent community project.
