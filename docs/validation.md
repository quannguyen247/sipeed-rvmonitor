# Validation and evidence boundaries

## Publication-time checks

`make test` compiles the current renderer, JSON writer and percentage functions
on the host without hardware access. It checks long-to-short state replacement,
settings dismissal, column positions, temperature rounding, missing data and
counter resets. The integer PID runs on the host against known 1000/5000-step
checksums. The extracted pure C uses the source on main; the sampler and
privileged driver paths are excluded from this host probe.

The same test command replays the recorded ANSI streams and verifies fixture
hashes and JSONL records. GitHub CI cross-compiles the monitor/Linux helpers
and runs these host checks. It does not execute firmware or NPU inference.

The publication build uses the existing Xuantie musl toolchain. Its stripped
RISC-V monitor is 75,672 bytes, SHA-256
`0af6365add2ecc7e7e2042752662a08539ffb82b7424149423afdaedd1406019`,
identical to the validated board binary. Only license comments were added to
the final monitor source. The recorded tests were not rerun by installing a
historical revision or rebooting the board for this repository import.

The reorganized firmware also built successfully after checking all 47 pinned
vendor files. The NPU module builder applied the telemetry patch and produced
an 81,464-byte module using the prepared kernel. That module has a different
fingerprint from the installed one and was **not loaded onto the board**.
Five warnings remain in unchanged vendor code (three printf formats and two
unused variables). The monitor and Linux-helper builds passed -Werror.
These cross-builds verify compilation, not a new firmware/module runtime test.

## Recorded board run, 2026-10-10

The prepared SG2002 system used Linux 5.10.271-rvnano #4, FreeRTOS 10.4.6 and
shared ABI v3. See [fixture manifest](../tests/fixtures/manifest.json) for the
exact UTC timestamp and unchanged raw output hashes.

| Check | Observed result |
|---|---|
| Default JSONL logger | 10 samples; selected 1000 ms, actual 1004.634-1004.707 ms |
| Default logger CPU cost | 0.4604-0.4816% of one Linux CPU, during this run |
| Two concurrent 80x24 SSH dashboards | 98/107 frames; both observed CPU1 and NPU Running |
| External NPU self-test | 4 INT8 cases, 121,212 values equal CPU, 12 hardware IRQs, 12 jobs |
| CPU1 PID, 1000 cycles / 1 ms | wake median/p99/max 18.24/20.24/21.08 us; max finish 21.12 us; 0 misses |
| CPU0 OTHER, same PID | max finish 4410.52 us; 53 misses of 1 ms |
| CPU0 FIFO20, same PID | max finish 3125.72 us; 43 misses of 1 ms |
| Driver lifetime | First loader could exit before last reader; preloaded driver/external client retained |
| CPU1 after tests | Interrupt WFI (sleep mode 2), fault cause 0 |

Both CPU tests matched checksum 902335789. CPU0 PASS means functional agreement;
the measured deadlines were missed under two 100 ms dashboards and a 250 ms
logger. CPU1 results are finite observations, not a universal WCET proof.
The self-test's short NPU jobs may fall between one-second display samples.

The recorded final cleanup left the NPU module unloaded, retained the existing
kernel/package/runtime payloads and introduced no reboot. These are dated
observations, not claims about every future invocation or stock board image.
