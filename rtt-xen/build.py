#!/usr/bin/env python3
"""Build the pinned real RT-Thread kernel plus a minimal single-core Xen port."""
from pathlib import Path
import subprocess
import argparse
import os

parser = argparse.ArgumentParser()
parser.add_argument('--qemu', action='store_true')
parser.add_argument('--rtthread', type=Path, default=Path(os.environ.get('RTTHREAD_SRC', '../rt-thread')))
parser.add_argument('--output', type=Path)
parser.add_argument('--toolchain-prefix', default=os.environ.get('CROSS_COMPILE', 'aarch64-none-elf-'))
args = parser.parse_args()

source = args.rtthread.resolve()
here = Path(__file__).resolve().parent
build = (args.output or (here.parent / ('build-qemu' if args.qemu else 'build-xen'))).resolve()
build.mkdir(parents=True, exist_ok=True)
prefix = args.toolchain_prefix
commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
assert commit == 'e6905134d511900f5c3f0e880a014e2c5903ac87', commit
kernel = ['clock', 'components', 'cpu_up', 'defunct', 'idle', 'ipc', 'irq', 'kservice',
          'mem', 'object', 'scheduler_comm', 'scheduler_up', 'thread', 'timer']
files = [source / 'src' / (name + '.c') for name in kernel]
files += [source / 'src/klibc' / (name + '.c') for name in
          ['kerrno', 'kstdio', 'kstring', 'rt_vsnprintf_tiny', 'rt_vsscanf']]
files += [here / 'board.c', here / 'port.S']
flags = ['-march=armv8-a', '-O2', '-g', '-ffreestanding',
         '-fno-builtin', '-fno-stack-protector', '-fno-pic', '-fno-pie',
         '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra',
         '-D__RTTHREAD__', '-D__RT_KERNEL_SOURCE__',
         '-I' + str(here), '-I' + str(source / 'include')]
if args.qemu:
    flags.append('-DRTT_XEN_QEMU_TEST')
objects = []
for file in files:
    obj = build / (file.stem + '.o')
    subprocess.run([prefix + 'gcc', *flags, '-c', str(file), '-o', str(obj)], check=True)
    objects.append(str(obj))
elf = build / 'rtthread-xen.elf'
subprocess.run([prefix + 'gcc', '-nostdlib', '-no-pie', '-Wl,--gc-sections',
                '-Wl,-Map=' + str(build / 'rtthread-xen.map'), '-T', str(here / 'link.ld'),
                *objects, '-lgcc', '-o', str(elf)], check=True)
subprocess.run([prefix + 'objcopy', '-O', 'binary', str(elf), str(build / 'rtthread-xen.bin')], check=True)
subprocess.run([prefix + 'size', str(elf)], check=True)
print('RT-Thread source:', commit)
print('Built:', build / 'rtthread-xen.bin')
