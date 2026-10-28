# Source reproduction checks — 2026-10-29

These checks used the public port source and clean local checkouts of the pinned Xen-Troops and Dom0 sources. They reused existing toolchains, Yocto dependency trees, downloads and sstate. This is **not** an independent clean-host rebuild. No physical media were written during these checks.

| Gate | Result | Evidence |
|---|---|---|
| Clean Xen-Troops source staging | Passed | Pinned base markers validated; integration applied to a separate clean checkout; Git diff whitespace checks passed. |
| RT-Thread guest build | Passed | RT-Thread `e6905134d511900f5c3f0e880a014e2c5903ac87`, SDK 0.16.8; ELF text 25,500 B, data 304 B, BSS 45,056 B. |
| Clean Dom0 patch and compilation | Passed | Dom0 base `03b961479a6b56c99228aedbe206ba1ed6668b54`; remote extension and 256 MiB guest compiled; 202 Ninja steps completed. |
| Access and guest-support Yocto packages | Passed | 1,861 tasks successful, 1,819 reused. Target AArch64 bridge and systemd package produced. |
| Linux rootfs with public integration layer | Passed | 5,128 tasks successful, 5,114 reused; ext4 and tar rootfs generated. |
| Rootfs contents | Passed | Guest Python/service and SSH configuration match public source; bridge is executable AArch64 ELF; RTT service, SSH socket and networkd enabled; SSH include, key mode 0600 and static Ethernet address verified. |
| Resource-overlay generation | Passed | Linux memory `0 200000`, CPUs `3`, static banks `0 50000000 0 78000000 0 20000000 0 8000000`. |
| Regular SD-image assembly | Passed | 1,091,567,616-byte image copy; three FAT files read back and SHA-256 matched; independent `sgdisk -v` found no GPT problems. Used the previously verified Dom0 binary for this assembly check. |
| Automated live target checks | Passed | 12 checks: Xen, Linux domain 1, three Linux vCPUs, grant device, RTT service active/enabled, pin-operation success, domain memory/vCPU resources, advancing control count and zero FP errors. Tested against the existing deployed system, not the newly built rootfs. |
| New-media cold boot | Not tested | Deferred; no claim that the new artifacts have booted on hardware. |
| Independent clean-host full build | Not tested | Existing upstream dependencies and caches were reused. |

The Yocto host emitted one warning about missing `CONFIG_TASK_IO_ACCOUNTING` in the WSL host kernel; package and image tasks succeeded. The offline target's clock was incorrect, so its timestamp is not evidence of the test date. The SSH verifier records target time and collector time separately.

Raw logs, SSH material and machine-specific image contents remain local. Existing hardware timing evidence is in `rtt-stress-summary.json`; it measures a synthetic controller, not physical actuator timing.
