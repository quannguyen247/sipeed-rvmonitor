# Recorded board fixtures

These files are the stdout/ANSI/JSONL outputs of the 2026-10-10 board validation.
They contain telemetry and test results. SSH stderr, IP addresses, Wi-Fi names,
credentials, private audit paths and unrelated machine logs are excluded.
The payload bytes are unchanged; SHA-256 and the exact source test time are in
[manifest.json](manifest.json). Copyright 2026 Nguyen Dong Quan; Apache-2.0.

The two 80x24 SSH TTY sessions remained open while CPU1 and NPU tests ran in
another terminal. They recorded 98 and 107 completed display frames. Default
logging recorded ten samples selected at 1000 ms; actual spacing includes work
and Linux scheduling. CPU0 tests ran under those concurrent observers and a
250 ms logger. Their PASS line confirms the checksum, not the 1 ms deadline.

Offline replay checks terminal overwrite/erase and recorded data integrity.
It does not simulate hardware or establish a new real-time guarantee.
