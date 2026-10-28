# Reproducing the tested architecture

This is a source-level reproduction path for a Raspberry Pi 5 with 4 GiB of RAM. It is **not** a one-click installer. The scripts stage source changes and assemble a new regular image file; they never select or write a physical disk. Follow upstream Xen-Troops build instructions for the base images.

## Inputs and prerequisites

1. Obtain the Xen-Troops Raspberry Pi 5 repository at commit `008a70e2c6de0f3aeb872497ccea8c0c181e8198` and follow its own build instructions for Xen 4.19, Zephyr Dom0 and Yocto DomD. The tested configuration boots Linux's root filesystem from a separate USB drive. Keep its upstream licenses and source repositories intact.
2. Obtain RT-Thread commit `e6905134d511900f5c3f0e880a014e2c5903ac87` with its `include/`, `src/` and `components/libc/` sources. Use Zephyr SDK 0.16.8's `aarch64-zephyr-elf-` tools or a compatible bare-metal GCC cross toolchain.
3. Provide your own SSH public key. The public recipe requires `RPI_XEN_SSH_PUBKEY` in the private Yocto `local.conf`; the repository does not contain a key. Set `RPI_XEN_STATIC_ADDR` there if `192.168.77.2/24` conflicts with your isolated direct-link network. Keep SSH private keys off images and source control.
4. Have `fdtget`/`fdtput`, `mcopy` from mtools, Python 3.11+, the upstream build dependencies, and a verified method to write SD and USB images. Check physical drive identities before writing any media. Budget sufficient disk space for Yocto, its caches and temporary image copies.

## Source staging

Build RTT from this repository:

```sh
python3 rtt-xen/build.py --rtthread /path/to/rt-thread \
  --toolchain-prefix /path/to/aarch64-zephyr-elf-
```

On a **clean checkout** of the pinned Xen-Troops revision, first validate and then stage the integration changes:

```sh
python3 integration/apply_xen_troops.py /path/to/xen-troops build-xen/rtthread-xen.bin --check
python3 integration/apply_xen_troops.py /path/to/xen-troops build-xen/rtthread-xen.bin
python3 dom0-extension/apply_patch.py /path/to/xen-troops/zephyr/zephyr-dom0-xt
```

The first script adds `rtt.bin` to the FAT image inputs, removes the unused Linux PV test guest, adds the public-key SSH and RTT service recipes, and sets the tested 4 GiB USB rootfs size. It backs up edited source files. The second patches the Zephyr Dom0 management code. Before the Yocto build, add your own public key to its private `local.conf`, for example:

```conf
RPI_XEN_SSH_PUBKEY = "ssh-ed25519 YOUR_OWN_PUBLIC_KEY"
RPI_XEN_STATIC_ADDR = "192.168.77.2/24"
```

Use your real public key, not the example. Build Dom0 with the three Kconfig options from `dom0-extension/remote-admin.conf`: dummy shell backend, 8192-byte buffer and CRC support. The Xen-Troops YAML staging script also inserts these options. Rebuild the full Xen/Zephyr/Yocto artifacts using the upstream build workflow; this repository does not vendor their large dependency trees.

## Resource overlay and image assembly

After the Yocto image build, create a new DomD overlay from the generated `bcm2712-raspberrypi5-xen.dtbo`:

```sh
python3 integration/configure_domd_dtbo.py /path/to/original.dtbo /path/to/new-rtt.dtbo
```

The overlay requests Linux 2 GiB/3 vCPUs using the 4 GiB Pi 5 memory map tested here. Assemble a *new regular image file* from the Xen-Troops `full.img` and the rebuilt artifacts:

```sh
python3 integration/assemble_full_image.py /path/to/full.img /path/to/full-rtt.img \
  --zephyr /path/to/zephyr.bin \
  --dtbo /path/to/new-rtt.dtbo \
  --rtt build-xen/rtthread-xen.bin
```

The script checks the GPT and reads back each copied FAT file against its SHA-256 digest. It refuses an existing output. Keep `rootfs.img` from the same Yocto build paired with this `full-rtt.img`; the assembler does not modify `rootfs.img`. Before flashing, independently inspect both image files, the target board and *physical* SD/USB identities. Never infer a target disk from a drive letter alone. Flash the SD image and USB rootfs using your platform's verified imaging procedure; this repository intentionally has no disk-writing command. A different board, RAM size or storage layout requires adaptation.

## Linux service and verification

`integration/rtt-guest-support.bb`, staged by `apply_xen_troops.py`, compiles the Linux bridge with Yocto's target compiler, installs `guest_service.py` and enables `rtt-guest.service`. The Linux image must also supply Python 3, Xenstore tools, `/dev/xen/gntdev`, OpenSSH and systemd. Verify these in the generated rootfs and on the booted target; the new recipe has not yet passed an independent clean Yocto rebuild.

Acceptance checks on the target should confirm Xen and Linux boot, SSH public-key login, 2 GiB/3-vCPU Linux resources, Dom0 management permissions, a 256 MiB/1-vCPU RTT domain pinned to CPU 3, advancing RTT control counts and zero FP errors in the shared-memory snapshot. Repeat a bounded Linux load test and measure end-to-end device timing with your actual peripherals. A full power-cycle test remains necessary for a new build.

## Publication boundary

The original complete SD and USB images are not mirrored here. They contain machine-specific SSH/network setup, have not been audited as public images, and are much larger than a suitable source release. The project also excludes raw build logs, personal design documents and private credentials. Source staging and image assembly are provided, but an independent clean build, fresh-media flash and cold-boot acceptance run have not yet been completed from this public checkout.
