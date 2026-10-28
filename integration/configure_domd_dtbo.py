#!/usr/bin/env python3
"""Write a separate 4 GiB Pi 5 DomD overlay with the measured resource map."""
import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path, help='Original bcm2712-raspberrypi5-xen.dtbo')
parser.add_argument('output', type=Path, help='New .dtbo, different from source')
args = parser.parse_args()
source = args.source.resolve()
output = args.output.resolve()
if not source.is_file() or source == output or output.exists():
    parser.error('Source must exist; output must be a new, different file')
node = '/fragment@0/__overlay__/domD'
for prop in ('memory', 'cpus', 'xen,static-mem'):
    subprocess.run(['fdtget', '-t', 'x', str(source), node, prop], check=True, capture_output=True)
shutil.copyfile(source, output)
subprocess.run(['fdtput', '-t', 'x', str(output), node, 'memory', '0', '200000'], check=True)
subprocess.run(['fdtput', '-t', 'x', str(output), node, 'cpus', '3'], check=True)
subprocess.run(['fdtput', '-t', 'x', str(output), node, 'xen,static-mem',
                '0', '50000000', '0', '78000000', '0', '20000000', '0', '8000000'], check=True)
for prop in ('memory', 'cpus', 'xen,static-mem'):
    result = subprocess.run(['fdtget', '-t', 'x', str(output), node, prop],
                            check=True, capture_output=True, text=True)
    print(prop + ': ' + result.stdout.strip())
