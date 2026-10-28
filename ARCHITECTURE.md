# Architecture and validation

The Pi 5 boots Xen 4.19, Zephyr Dom0 and Yocto Linux as domain 1. Zephyr creates the `rtt_domu` domain from a FAT partition image. Dom0 hard-pins the RT-Thread vCPU to CPU 3; Linux and Dom0 run on CPUs 0–2. The Linux domain has 2 GiB and three vCPUs; RT-Thread has 256 MiB and one vCPU. The guest heap extends to approximately 224 MiB within its allocation. A 32 MiB heap probe touched and checked memory at 4 KiB intervals on hardware.

The RT-Thread guest runs a 1 kHz system tick and a 100 Hz two-axis synthetic PID task. Two threads exercise full floating-point/SIMD context switching. Linux sends targets and reads control outputs through Xen grant tables. The control header and active fields occupy the first 192 bytes of a 16 KiB aligned guest region. Linux has 16 KiB native pages while Xen grants are 4 KiB; the bridge maps only the first grant into one native page. The remaining three grants are reserved for future use. The bridge supports snapshots, explicit targets and a bounded demonstration stream.

The restricted Dom0 management interface is reachable through Xenstore only by Linux domain 1. It can list domains, start/pause/resume/destroy only `rtt_domu` and `rpi_5_domu`, capture their bounded console output and replace the fixed RT-Thread image with size/CRC/readback checks. It cannot run arbitrary shell commands or manage the Linux/Zephyr control domains. SSH access to Linux uses locally provisioned public-key authentication; credentials are not part of this repository.

## Hardware measurement

In a 60.02-second run with three Linux CPU workers allocating 192 MiB combined, the Linux bridge collected 1,196 IPC samples and observed 5,997 RT-Thread control cycles (99.91 Hz over the observation window). The RT-Thread guest reported maximum control wakeup lateness of 78 microseconds, maximum control computation of 3 microseconds, zero deadlines missed at a 10 ms threshold, and zero FP context errors. Raw video, AI inference and physical actuator load were not part of this test. The compact summary is in [`evidence/rtt-stress-summary.json`](evidence/rtt-stress-summary.json).

The current Linux image exposes GPIO and DRM nodes. MIPI camera/screen integration, AI inference, touch UI, physical motor control and a full-board power-cycle recovery test remain outside the verified scope. End-to-end actuator timing is governed by Linux after it receives RT-Thread's desired motion parameters, so the guest's control jitter is not an end-to-end actuator guarantee.
