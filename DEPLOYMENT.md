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
```

The first script adds `rtt.bin` to the FAT image inputs, removes the unused Linux PV test guest, adds the public-key SSH and RTT service recipes plus the tested pseudo 1.9.11 host fix, and sets the tested 4 GiB USB rootfs size. It backs up edited source files and accepts reruns of its own changes. Fetch the upstream dependencies before applying the Dom0 patch (the Dom0 checkout does not exist immediately after cloning Xen-Troops):

```sh
cd /path/to/xen-troops
moulin rpi5.yaml --DOMD_ROOT usb --ENABLE_HDMI yes
ninja fetch-dom0 fetch-domd yocto/build-domd/conf/moulin.conf
cd /path/to/this-repository
python3 dom0-extension/apply_patch.py /path/to/xen-troops/zephyr/zephyr-dom0-xt
```

Before the Yocto build, add your own public key to its private `yocto/build-domd/conf/local.conf`, for example:

```conf
RPI_XEN_SSH_PUBKEY = "ssh-ed25519 YOUR_OWN_PUBLIC_KEY"
RPI_XEN_STATIC_ADDR = "192.168.77.2/24"
```

Use your real public key, not the example. Build Dom0 with the three Kconfig options from `dom0-extension/remote-admin.conf`: dummy shell backend, 8192-byte buffer and CRC support. The Xen-Troops YAML staging script inserts these options. From the Xen-Troops directory run `ninja full.img rootfs.img`. This repository does not vendor the large dependency trees. For a limited host, set `BB_NUMBER_THREADS = "4"`, `PARALLEL_MAKE = "-j4"` and `INHERIT += "rm_work"` in private local.conf. Preserve downloads and sstate caches between retries; on WSL monitor Windows host space separately from the virtual Linux disk.

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

`integration/rtt-guest-support.bb`, staged by `apply_xen_troops.py`, compiles the Linux bridge with Yocto's target compiler, installs `guest_service.py` and enables `rtt-guest.service`. The Linux image must also supply Python 3, Xenstore tools, `/dev/xen/gntdev`, OpenSSH and systemd. The public recipes and resulting Linux rootfs passed a cache-assisted Yocto build; see [the verification record](evidence/reproduction-checks.md). This was not a clean-host rebuild.

After establishing SSH host trust on your isolated link, run the read-only acceptance script from your PC:

```sh
python3 integration/verify_target.py --ssh root@192.168.77.2 \
  --identity /path/to/your/private-key --output acceptance.json
```

Use `--known-hosts /path/to/existing/known_hosts` if your connection uses a separate trust file. The script requires an already trusted host key, prints JSON and exits nonzero on a failed check. It checks resources, service enablement, pin-operation results, advancing control counts and FP errors. It does not upload software, reboot or change domain state. Its report records the target clock separately, because an offline board may have the wrong time.

Acceptance checks on the target should confirm Xen and Linux boot, SSH public-key login, 2 GiB/3-vCPU Linux resources, Dom0 management permissions, a 256 MiB/1-vCPU RTT domain pinned to CPU 3, advancing RTT control counts and zero FP errors in the shared-memory snapshot. Repeat a bounded Linux load test and measure end-to-end device timing with your actual peripherals. A full power-cycle test remains necessary for a new build.

## Publication boundary

The original complete SD and USB images are not mirrored here. They contain machine-specific SSH/network setup, have not been audited as public images, and are much larger than a suitable source release. The project also excludes raw build logs, personal design documents and private credentials. Source staging, component/rootfs builds, image assembly and current-target checks have been exercised, but an independent clean-host build, fresh-media flash and cold-boot acceptance run have not yet been completed from this public checkout.
