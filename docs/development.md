# Development stages

The feature commits reconstruct the actual progression from saved source
snapshots. All commit dates are publication dates; no author dates were backdated.
Original source hashes and upstream pins are in [source-provenance.json](source-provenance.json).

| Stage | Change |
|---|---|
| Project ownership | Apache-2.0, Copyright 2026 Nguyen Dong Quan, third-party license boundaries |
| Telemetry dashboard | /proc/stat, shared WFI counters, command-path NPU counters, configured clocks |
| Interrupt idle | ABI v3, mailbox wake, no idle timer ticks, real FreeRTOS control/helpers |
| Dashboard alignment | CPU0/CPU1 naming, aligned columns, independent CPU test from another SSH session |
| Sampling | One-second default, per-session menu, full-frame erase to prevent Idleve/Sleepng |
| Concurrent sessions | Shared driver lifetime, Running/Idle/N/A, embedded NPU wrapper, positional JSONL logging |
| Community build | Relative paths, explicit external dependencies, public source provenance and usage docs |
| Verification | Offline screen/PID regression, curated historical hardware results and CI |

Historical states and CLI in older commits describe those revisions. Use main
for the final interface. No old stage was reinstalled on the board for this import.
