#!/usr/bin/env python3
"""Apply the explicitly approved extension to the local pinned Dom0 source."""
from pathlib import Path
import shutil
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('dom0_project', type=Path, help='Path to pinned zephyr-dom0-xt checkout')
args = parser.parse_args()
project = args.dom0_project.resolve()
if not (project / 'src/dom0.c').is_file():
    parser.error('dom0_project must contain src/dom0.c')
here = Path(__file__).resolve().parent

def update(relative, function):
    target = project / relative
    backup = target.with_name(target.name + '.before-remote-admin')
    if not backup.exists():
        shutil.copy2(target, backup)
    target.write_text(function(target.read_text()))

def main_patch(text):
    if 'remote_admin_start();' not in text:
        assert '#include "dom0.h"' in text and 'exit_err:\n' in text
        text = text.replace('#include "dom0.h"', '#include "dom0.h"\nvoid remote_admin_start(void);')
        text = text.replace('exit_err:\n', '\tif (!ret) remote_admin_start();\n\nexit_err:\n')
    return text

def cmake_patch(text):
    line = 'target_sources(app PRIVATE src/remote_admin.c)'
    return text if line in text else text + '\n' + line + '\n'

def domain_patch(text):
    marker = 'struct dom0_domain_cfg domain_cfgs[] = {'
    if '.name = "rtt_domu"' in text:
        return text
    config = '''static struct xen_domain_cfg rtt_cfg = {
    .name = "rtt_domu",
    .mem_kb = 256 * 1024,
    .flags = (XEN_DOMCTL_CDF_hvm | XEN_DOMCTL_CDF_hap),
    .max_evtchns = 10, .max_vcpus = 1,
    .gnt_frames = 32, .max_maptrack_frames = 1,
    .gic_version = XEN_DOMCTL_CONFIG_GIC_V2,
    .tee_type = XEN_DOMCTL_CONFIG_TEE_NONE,
    .cmdline = "", .ssidref = 12,
    .load_image_bytes = storage_image_kernel_read,
    .get_image_size = storage_image_kernel_get_size,
};

'''
    assert marker in text
    return text.replace(marker, config + marker + '\n    { .domain_cfg = &rtt_cfg, .image_kernel_path = DISK_BIN_PATH "rtt.bin" },')

update('src/dom0.c', main_patch)
update('CMakeLists.txt', cmake_patch)
update('src/dom_cfg/rpi_5.c', domain_patch)
shutil.copy2(here / 'remote_admin.c', project / 'src/remote_admin.c')
print('Approved remote management extension applied; original files backed up.')
