# Agent handoff

Goal: reproduce the Raspberry Pi 5 (4 GiB) Xen + Zephyr Dom0 + Yocto Linux DomD + RT-Thread DomU architecture described in [DEPLOYMENT.md](DEPLOYMENT.md). Read that guide and [ARCHITECTURE.md](ARCHITECTURE.md) before changing build inputs.

## Fixed inputs and boundaries

- Xen-Troops Raspberry Pi 5: `008a70e2c6de0f3aeb872497ccea8c0c181e8198`.
- RT-Thread: `e6905134d511900f5c3f0e880a014e2c5903ac87`.
- Target: Pi 5 with 4 GiB RAM; Zephyr Dom0 128 MiB, Linux 2 GiB/3 vCPUs, RT-Thread 256 MiB/1 vCPU. These memory and CPU settings are board specific.
- Linux owns camera, display, GPIO, network, AI and actuators. RT-Thread handles periodic control logic and communicates with Linux through Xen shared memory. The current controller uses a simulated plant.
- Never commit private keys, passwords, personal addresses, raw logs or original device images. Supply an operator's **public** SSH key through private Yocto `local.conf`.
- Scripts in this repo work on source checkouts and regular image files. Do not add automatic physical-disk flashing or choose a disk by letter alone. Before any media write, independently verify model, capacity, serial, partition layout and system-disk identity with the operator.

## Suggested work order

1. Record tool versions, available space, board RAM and upstream commit IDs. Keep existing Yocto downloads and sstate caches.
2. Build `rtt-xen/build.py` from the pinned RT-Thread tree. Keep the resulting `build-xen/rtthread-xen.bin` outside Git.
3. Run `integration/apply_xen_troops.py ... --check` on a clean pinned Xen-Troops checkout, then apply it and `dom0-extension/apply_patch.py`. Review the diff and set `RPI_XEN_SSH_PUBKEY` in private `local.conf`.
4. Follow Xen-Troops build instructions for Xen, Zephyr Dom0 and Yocto DomD. Check that `rpi-xen-access` and `rtt-guest-support` are included and build successfully. Preserve logs locally, without publishing secrets.
5. Run `integration/configure_domd_dtbo.py` on the generated overlay. Run `integration/assemble_full_image.py` to create a separate `full-rtt.img`; check GPT, FAT readback and matching `rootfs.img`.
6. After verified SD and USB imaging, use SSH public-key login to collect boot evidence. Verify Xen domains, resources and CPU placement, RTT guest start, advancing control counts, zero FP failures and bidirectional bridge data. Power cycle and repeat. Check actual camera, GPIO, display and AI workloads separately before claiming competition readiness.

## Acceptance and reporting

Report each gate as passed, failed or untested with command output or measured evidence. A successful guest compile does not prove a successful Yocto image; a successful image build does not prove a boot; a live guest does not prove peripheral integration. Current public checkout has been used for guest compilation and pinned-source marker checks. Its new Yocto recipes, image assembler and clean-host end-to-end deployment still need independent verification. Stop on a failed gate, fix it and resume from the existing cache. Do not claim one-click deployment or cold-boot reproducibility until those gates pass.
