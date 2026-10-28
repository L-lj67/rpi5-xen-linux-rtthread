# RT-Thread guest on Raspberry Pi 5 with Xen and Linux

An experimental asymmetric multiprocessing setup on one Raspberry Pi 5:

| Domain | CPU affinity | Memory | Role |
|---|---|---:|---|
| Zephyr Dom0 | CPUs 0–2 | 128 MiB | Xen domain management |
| Yocto Linux DomD | CPUs 0–2, 3 vCPUs | 2 GiB | Devices, applications, networking and AI |
| RT-Thread DomU | CPU 3, 1 vCPU | 256 MiB | Periodic control and state logic |

RT-Thread does not access physical peripherals. Linux owns GPIO, display, camera and eventual actuators. The current two-axis PID loop uses simulated plant state. It demonstrates RT-Thread scheduling and bidirectional Linux/RT-Thread IPC; it is **not** a finished robot controller.

The RT-Thread guest uses the real RT-Thread 5.4.0 kernel source, not a bare-metal loop that prints its banner. It has an AArch64 EL1 port, GICv2 virtual interrupt handling, the ARM virtual timer, Xen console and grant-table sharing, FP/SIMD context switching and a bounded control task. On this Pi, Xen masks the expired virtual timer when injecting its interrupt; the port explicitly unmasks `CNTV_CTL_EL0` after setting the next compare value.

## Source layout

- `rtt-xen/`: guest port, Linux shared-memory bridge, systemd guest launcher, load-test tool.
- `dom0-extension/`: restricted Zephyr Dom0 management extension and patch script.
- `evidence/`: compact measured test summary.

The large Yocto/Zephyr/RT-Thread source trees, built images, SSH keys, local addresses and private project documents are deliberately omitted. See the [architecture and validation notes](ARCHITECTURE.md) for boundaries and measured results.

## Build the guest

This source was developed with:

- [Xen-Troops Raspberry Pi 5](https://github.com/xen-troops/meta-xt-prod-devel-rpi5), commit `008a70e2c6de0f3aeb872497ccea8c0c181e8198`;
- [RT-Thread](https://github.com/RT-Thread/rt-thread), commit `e6905134d511900f5c3f0e880a014e2c5903ac87`;
- Zephyr SDK 0.16.8 AArch64 cross compiler, or a compatible bare-metal AArch64 GCC toolchain;
- Xen 4.19 and Linux 6.12.25 with 16 KiB pages on the tested board.

The pinned RT-Thread checkout needs `include/`, `src/`, `components/libc/`, `libcpu/aarch64/` and `bsp/raspberry-pi/raspi5/` available. From this repository:

```sh
python3 rtt-xen/build.py \
  --rtthread /path/to/rt-thread \
  --toolchain-prefix /path/to/aarch64-zephyr-elf-
```

The output is `build-xen/rtthread-xen.bin`. Pass `--qemu` to build the QEMU `virt` variant in `build-qemu/`.

The `dom0-extension/apply_patch.py /path/to/zephyr-dom0-xt` script patches a checkout of Xen-Troops' Zephyr Dom0 and saves original files with `.before-remote-admin` suffix. Build Dom0 with `dom0-extension/remote-admin.conf` enabled. The storage partition must contain `dom0/rtt.bin` before starting the `rtt_domu` configuration. The extension accepts only the two named test domains, and verifies uploaded RT-Thread images by CRC and SD readback before replacing the active file. It is not a general remote shell.

For the tested 2 GiB Linux resource map, the DomD device-tree overlay uses `memory = <0 0x200000>` (KiB), `cpus = <3>`, and static memory banks `(0x50000000, 0x78000000)` plus `(0x20000000, 0x08000000)`. These settings require checking the actual board memory map and image layout before deployment. They must not be copied blindly onto another host.

Build `rtt-xen/linux_bridge.c` with the Linux AArch64 cross compiler, then install it at `/root/xen-tests/rtt-linux-bridge` along with `guest_service.py`. Install `rtt-guest.service` into systemd. The service creates the RT-Thread domain, attaches its console, and requires the shared-memory control counter to advance before declaring startup successful. The on-board control client uses the Dom0 Xenstore interface and requires no interactive Pi keyboard.

This is research code intended as an implementation reference. Check the target's Xen/Zephyr APIs, page size, memory map, storage paths, trust model and device tree before using it elsewhere.

## License

The code in this repository is licensed under Apache-2.0; see [LICENSE](LICENSE). Upstream components retain their own licenses and are linked above rather than vendored.
