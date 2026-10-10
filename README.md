# RVMonitor for Sipeed Lichee RV Nano

A small C terminal monitor for SG2002: CPU0 (Linux C906), CPU1 (FreeRTOS
C906L), NPU activity, temperature, RAM used and configured clock rates.
Includes live sampling, CPU/NPU self-tests and JSONL logging. The measured
musl static executable is 74 KiB; no curses or monitor daemon is needed.

## Build and install

The supported board runs Ubuntu 22.04.5 with the prepared
`5.10.271-rvnano #4` kernel, NPU telemetry module and FreeRTOS ABI v3.
For the required driver/firmware files, see [board/README.md](board/README.md).

On the board, install GCC, make and static libc development files, then run:

```sh
sudo apt install gcc make libc6-dev git
git clone https://github.com/quannguyen247/sipeed-rvmonitor.git
cd sipeed-rvmonitor
make
sudo make install
sudo rvmonitor
```

`make install` installs the monitor and the two CPU test helpers. It does not
replace the kernel or start firmware. To cross-compile on a PC instead:

```sh
make CROSS_COMPILE=/path/to/riscv64-unknown-linux-musl-
```

Use `make -B` after changing compiler or build flags. Run `make test` for the
host rendering, JSON, counter and PID checks.

## Usage

```sh
rvmonitor
rvmonitor --nputest
rvmonitor --c0test
rvmonitor --c1test
rvmonitor --logging "/root/log.jsonl" 20 500
rvmonitor --help
```

Run as root. The dashboard samples every **1 second**. Press `s` to change
the interval; `q` or Esc exits. Multiple dashboards can run alongside a test.
All rows use **Running / Idle / N/A**.

`--logging ["PATH" [SAMPLES [INTERVAL_MS]]]` defaults to `/root/rvmonitor_log`,
10 samples and 1000 ms. Quote the path; parent directories must exist.
Each sample is one JSON line. A repeat run overwrites the file.

NPU load measures command-path busy time, including DMA and waits; MHz shows
the configured clock. CPU1 Idle means interrupt-driven WFI. The final monitor
unloads an idle NPU driver only if the monitor group loaded it.

## Source layout

| Path | Contents |
| --- | --- |
| [src/](src/) | Monitor, shared ABI and CPU test helpers |
| [board/](board/) | Optional FreeRTOS firmware and kernel patches |
| [tests/](tests/) | Host regression tests |

## License

Copyright **2026 Nguyen Dong Quan**. Project code is [Apache-2.0](LICENSE).
Vendor-derived kernel patches and startup retain GPL/MIT; see [NOTICE](NOTICE).
