# Measurement model

CPU0 uses differences in /proc/stat, excluding idle and iowait.
CPU1 uses elapsed minus WFI time in the reserved shared RAM protocol.
The NPU reports serialized command-path busy time, including TIU/TDMA and waits.
This is not MAC occupancy, TOPS, power, or an NPU frequency measurement.

Clock values come from Linux CCF clk_summary and are configured rates.
Gated engines do not execute continuously at those rates. Temperature is the SoC
thermal sensor; RAM used is MemTotal minus MemAvailable.

The supported layout reserves 2 MiB at 0x8fe00000; shared RAM starts at
0x8ff00000. The monitor checks the live device-tree carveout before mapping it.
It does not probe secure boot-vector registers.
