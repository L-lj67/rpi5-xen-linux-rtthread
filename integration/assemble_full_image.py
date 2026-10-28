#!/usr/bin/env python3
"""Assemble and read-back verify an SD image copy; never accesses physical disks."""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

parser = argparse.ArgumentParser()
parser.add_argument('base', type=Path, help='Xen-Troops full.img base image')
parser.add_argument('output', type=Path, help='New image path (must not exist)')
parser.add_argument('--zephyr', type=Path, required=True, help='Rebuilt Zephyr Dom0 zephyr.bin')
parser.add_argument('--dtbo', type=Path, required=True, help='Configured DomD overlay')
parser.add_argument('--rtt', type=Path, required=True, help='Built RT-Thread guest .bin')
parser.add_argument('--mcopy', default='mcopy', help='Path to mtools mcopy')
args = parser.parse_args()

base, output = args.base.resolve(), args.output.resolve()
inputs = [args.zephyr.resolve(), args.dtbo.resolve(), args.rtt.resolve()]
if not base.is_file() or not all(path.is_file() for path in inputs):
    parser.error('All input paths must be regular files')
if output.exists() or output == base or output in inputs or output.suffix != '.img':
    parser.error('Output must be a new .img file, separate from every input')
if not output.parent.is_dir():
    parser.error('Output parent directory must exist')
if not (Path(args.mcopy).is_file() or shutil.which(args.mcopy)):
    parser.error('mcopy is required; install mtools or pass --mcopy /path/to/mcopy')

def gpt_partitions(image):
    with image.open('rb') as file:
        file.seek(512)
        header = bytearray(file.read(512))
        if header[:8] != b'EFI PART':
            raise ValueError('Not a GPT image')
        header_size = struct.unpack_from('<I', header, 12)[0]
        checksum = struct.unpack_from('<I', header, 16)[0]
        if not 92 <= header_size <= 512:
            raise ValueError('Invalid GPT header size')
        struct.pack_into('<I', header, 16, 0)
        if zlib.crc32(header[:header_size]) != checksum:
            raise ValueError('GPT header CRC mismatch')
        entry_lba = struct.unpack_from('<Q', header, 72)[0]
        count, size, entries_crc = struct.unpack_from('<III', header, 80)
        if count > 256 or size != 128:
            raise ValueError('Unsupported GPT entry layout')
        file.seek(entry_lba * 512)
        entries = file.read(count * size)
        if len(entries) != count * size or zlib.crc32(entries) != entries_crc:
            raise ValueError('GPT entry CRC mismatch')
    result = {}
    for index in range(count):
        entry = entries[index * size:(index + 1) * size]
        if entry[:16] == bytes(16):
            continue
        start, end = struct.unpack_from('<QQ', entry, 32)
        name = entry[56:128].decode('utf-16-le').rstrip('\0')
        if not 0 < start <= end or (end + 1) * 512 > image.stat().st_size:
            raise ValueError('Invalid GPT partition bounds')
        if name in result:
            raise ValueError('Duplicate GPT partition name')
        result[name] = start * 512
    if set(result) != {'boot', 'dom0'}:
        raise ValueError(f'Expected boot and dom0 partitions, found {list(result)}')
    return result

partitions = gpt_partitions(base)
ops = [
    ('boot', inputs[0], 'zephyr.bin'),
    ('boot', inputs[1], 'bcm2712-raspberrypi5-xen.dtbo'),
    ('dom0', inputs[2], 'dom0/rtt.bin'),
]
with tempfile.TemporaryDirectory(prefix='rtt-assemble-', dir=output.parent) as temporary:
    staged = Path(temporary) / 'staged.img'
    shutil.copyfile(base, staged)
    for partition, source, destination in ops:
        image_spec = f'{staged}@@{partitions[partition]}'
        subprocess.run([args.mcopy, '-o', '-i', image_spec, str(source), '::/' + destination], check=True)
        extracted = Path(temporary) / (partition + '-' + source.name)
        subprocess.run([args.mcopy, '-i', image_spec, '::/' + destination, str(extracted)], check=True)
        with source.open('rb') as source_file, extracted.open('rb') as extracted_file:
            expected = hashlib.file_digest(source_file, 'sha256').digest()
            actual = hashlib.file_digest(extracted_file, 'sha256').digest()
        if actual != expected:
            raise ValueError(f'FAT readback mismatch: {partition}/{destination}')
        print(f'READBACK OK {partition}/{destination}: {expected.hex()}')
    if gpt_partitions(staged) != partitions:
        raise ValueError('GPT changed during file-level image assembly')
    staged.replace(output)
print(f'Created verified image copy: {output} ({output.stat().st_size} bytes)')
