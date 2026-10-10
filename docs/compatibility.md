# Validated system and limits

The current implementation is deliberately bound to the measured system:
Sipeed Lichee RV Nano / SG2002, Ubuntu 22.04.5, Linux 5.10.271-rvnano,
kernel build version #4 Fri Oct 9 02:40:26 +07 2026, custom prepared BSP and
FreeRTOS shared ABI v3. Stock images are not claimed to work unchanged.

Linux controls CPU0. CPU1 is a separate C906L RTOS domain, not a second Linux
SMP processor. RAM mapping is validated against the live DT carveout. C1 test
requires the supplied application, helper and remoteproc mailbox integration.
Missing C1 telemetry is N/A; CPU-only tests do not load the NPU module.

Opening the normal monitor loads its verified telemetry module if needed,
then reads counters. It is not purely a read-only startup operation.
The final cooperating invocation unloads only the generation the group loaded,
when no external reference/active command prevents normal rmmod. Preloaded or
externally reloaded modules are retained. SIGKILL cannot run cleanup; a later
invocation can release a deferred group-owned module. No force unload is used.

CPU0 load includes the monitor and SSH. Very short work can fall between samples.
CPU1 Idle includes interrupt-driven WFI, not full power-off. The monitor has no
overclock, voltage, DVFS or power-saving control. Configured clock is not a live
oscilloscope measurement, and NPU busy percentage is not arithmetic occupancy.
One-millisecond control results are finite tests, not a universal WCET guarantee.
