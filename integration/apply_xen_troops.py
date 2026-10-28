#!/usr/bin/env python3
"""Stage non-secret Xen-Troops build changes in a pinned local checkout."""
import argparse
from pathlib import Path
import shutil
import subprocess

PIN = '008a70e2c6de0f3aeb872497ccea8c0c181e8198'
parser = argparse.ArgumentParser()
parser.add_argument('checkout', type=Path, help='Xen-Troops Raspberry Pi 5 checkout')
parser.add_argument('rtt_binary', type=Path, help='Previously built RT-Thread Xen .bin')
parser.add_argument('--check', action='store_true', help='Validate pinned base markers without modifying files')
args = parser.parse_args()
root = args.checkout.resolve()
binary = args.rtt_binary.resolve()
if not binary.is_file():
    parser.error('rtt_binary must be a built RT-Thread image')
revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
if revision != PIN:
    parser.error(f'Expected Xen-Troops commit {PIN}; found {revision}')

planned = []

def update(path, replacements):
    source = root / path
    original = subprocess.check_output(['git', '-C', str(root), 'show', 'HEAD:' + str(path).replace('\\', '/')], text=True)
    data = original
    for old, new in replacements:
        if old not in data:
            parser.error(f'{path}: expected pinned source marker is missing; no changes written')
        data = data.replace(old, new, 1)
    if not args.check and source.read_text() not in (original, data):
        parser.error(f'{path}: contains unrelated edits; use a clean pinned checkout; no changes written')
    planned.append((source, data))

yaml = 'rpi5.yaml'
update(yaml, [
    ('        - "CONFIG_DOM_STORAGE_FATFS_LOGICAL_DRIVE_NAME=\\"1\\""',
     '        - "CONFIG_DOM_STORAGE_FATFS_LOGICAL_DRIVE_NAME=\\"1\\""\n'
     '        - "CONFIG_SHELL_BACKEND_DUMMY=y"\n'
     '        - "CONFIG_SHELL_BACKEND_DUMMY_BUF_SIZE=8192"\n'
     '        - "CONFIG_CRC=y"'),
    ('        - "%{YOCTOS_WORK_DIR}/%{DOMU_BUILD_DIR}/tmp/deploy/images/%{DOMU_MACHINE}/Image-initramfs-%{DOMU_MACHINE}.bin"\n', ''),
    ('          "dom0/z_sync.bin": "%{ZEPHYR_DOMU1_DIR}/%{ZEPHYR_DOMU1_BUILD_DIR}/zephyr/zephyr.bin"',
     '          "dom0/z_sync.bin": "%{ZEPHYR_DOMU1_DIR}/%{ZEPHYR_DOMU1_BUILD_DIR}/zephyr/zephyr.bin"\n'
     f'          "dom0/rtt.bin": "{binary.as_posix()}"'),
    ('          "dom0/linux-pv-image": "%{YOCTOS_WORK_DIR}/%{DOMU_BUILD_DIR}/tmp/deploy/images/%{DOMU_MACHINE}/Image-initramfs-%{DOMU_MACHINE}.bin"\n', ''),
])
image = Path('layers/xt-prod-devel-rpi5-domd/recipes-core/image/rpi5-image-xt-domd.bbappend')
update(image, [
    ('    optee-test', '    rpi-xen-access       \\\n    rtt-guest-support    \\\n    optee-test'),
    ('IMAGE_ROOTFS_SIZE = "8388608"', 'IMAGE_ROOTFS_SIZE = "4194304"'),
])
recipe = root / 'layers/xt-prod-devel-rpi5-domd/recipes-core/rpi-xen-access'
if args.check:
    print('Pinned Xen-Troops base is compatible; no files modified.')
    raise SystemExit(0)
for source, data in planned:
    backup = source.with_name(source.name + '.before-rtt')
    if not backup.exists():
        shutil.copy2(source, backup)
    source.write_text(data)
recipe.mkdir(parents=True, exist_ok=True)
here = Path(__file__).resolve().parent
shutil.copy2(here / 'rpi-xen-access.bb', recipe / 'rpi-xen-access.bb')
(recipe / 'files').mkdir(exist_ok=True)
for filename in ('10-rpi-xen.conf', '10-rpi-direct.network'):
    shutil.copy2(here / 'files' / filename, recipe / 'files' / filename)
support = root / 'layers/xt-prod-devel-rpi5-domd/recipes-core/rtt-guest-support'
support.mkdir(parents=True, exist_ok=True)
shutil.copy2(here / 'rtt-guest-support.bb', support / 'rtt-guest-support.bb')
(support / 'files').mkdir(exist_ok=True)
for source in (here.parent / 'LICENSE', here.parent / 'rtt-xen/linux_bridge.c',
               here.parent / 'rtt-xen/control_shared.h', here.parent / 'rtt-xen/guest_service.py',
               here.parent / 'rtt-xen/rtt-guest.service'):
    shutil.copy2(source, support / 'files' / source.name)
pseudo = root / 'layers/xt-prod-devel-rpi5-domd/recipes-devtools/pseudo'
pseudo.mkdir(parents=True, exist_ok=True)
shutil.copy2(here / 'pseudo_%.bbappend', pseudo / 'pseudo_%.bbappend')
print('Staged Xen-Troops source changes. Set your own public key in local.conf before building.')
