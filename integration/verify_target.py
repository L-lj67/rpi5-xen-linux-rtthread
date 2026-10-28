#!/usr/bin/env python3
"""Read-only SSH/local acceptance checks; prints a JSON report and exit status."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def collect():
    checks, evidence = {}, {}

    def command(name, argv):
        try:
            result = subprocess.run(argv, capture_output=True, text=True, timeout=65)
            evidence[name] = result.stdout.strip()
            if result.returncode:
                evidence[name + '_error'] = result.stderr.strip() or f'exit {result.returncode}'
            return result
        except (OSError, subprocess.TimeoutExpired) as error:
            evidence[name + '_error'] = str(error)
            return None

    try:
        checks['xen_hypervisor'] = Path('/sys/hypervisor/type').read_text().strip() == 'xen'
    except OSError:
        checks['xen_hypervisor'] = False
    result = command('linux_domain', ['xenstore-read', 'domid'])
    checks['linux_domain_1'] = result is not None and result.returncode == 0 and result.stdout.strip() == '1'
    evidence['linux_cpus'] = os.cpu_count()
    checks['linux_3_vcpus'] = os.cpu_count() == 3
    checks['xen_grant_device'] = Path('/dev/xen/gntdev').exists()
    result = command('service', ['systemctl', 'is-active', 'rtt-guest.service'])
    checks['rtt_service_active'] = result is not None and result.returncode == 0
    result = command('service_enabled', ['systemctl', 'is-enabled', 'rtt-guest.service'])
    checks['rtt_service_enabled'] = result is not None and result.returncode == 0 and result.stdout.strip() == 'enabled'
    result = command('domains', ['python3', '/root/xen-tests/guest_service.py', 'status'])
    listing = result.stdout if result is not None and result.returncode == 0 else ''
    checks['management_pins_succeeded'] = all(f'{name}_pin=0' in listing for name in ('dom0', 'linux', 'rtt'))
    rows = {name: (int(domid), int(memory), int(cpus)) for name, domid, memory, cpus in
            re.findall(r'^(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s', listing, re.M)}
    checks['dom0_resources'] = rows.get('Domain-0') == (0, 128, 1)
    checks['linux_resources'] = rows.get('Dom0less-1') == (1, 2048, 3)
    rtt = rows.get('rtt_domu')
    checks['rtt_resources'] = rtt is not None and rtt[1:] == (256, 1)
    checks['control_advancing'] = False
    checks['fp_errors_zero'] = False
    if rtt:
        snapshots = []
        for index in range(2):
            result = command(f'snapshot_{index}', ['/root/xen-tests/rtt-linux-bridge', str(rtt[0]), '--snapshot'])
            if result is not None and result.returncode == 0:
                try:
                    snapshots.append(json.loads(result.stdout))
                except ValueError:
                    pass
            if index == 0:
                time.sleep(.3)
        if len(snapshots) == 2:
            checks['control_advancing'] = snapshots[1].get('control', 0) > snapshots[0].get('control', 0)
            checks['fp_errors_zero'] = all(s.get('fp_failures') == 0 for s in snapshots)
    return {'schema': 1, 'target_time_utc': datetime.now(timezone.utc).isoformat(),
            'passed': all(checks.values()), 'checks': checks, 'evidence': evidence,
            'scope': 'Current live system only; does not prove cold boot or physical peripheral timing.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ssh', metavar='USER@HOST', help='Run checks over SSH using existing host trust')
    parser.add_argument('--identity', type=Path, help='Path to your private key; contents are never read by this script')
    parser.add_argument('--known-hosts', type=Path, help='Optional existing known_hosts file')
    parser.add_argument('--output', type=Path, help='Save report locally; must not already exist')
    parser.add_argument('--local', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.output and args.output.exists():
        parser.error('Output already exists; choose a new report path')
    if args.ssh:
        if args.ssh.startswith('-') or any(c.isspace() for c in args.ssh):
            parser.error('Invalid SSH destination')
        argv = ['ssh', '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes', '-o', 'ConnectTimeout=10',
                '-o', 'StrictHostKeyChecking=yes']
        if args.identity:
            argv += ['-i', str(args.identity)]
        if args.known_hosts:
            argv += ['-o', 'UserKnownHostsFile=' + str(args.known_hosts)]
        argv += [args.ssh, 'python3', '-', '--local']
        result = subprocess.run(argv, input=Path(__file__).read_text(encoding='utf-8'),
                                capture_output=True, text=True, timeout=180)
        try:
            report = json.loads(result.stdout)
        except ValueError:
            print(result.stderr.strip() or result.stdout.strip() or 'SSH did not return a report', file=sys.stderr)
            return 1
        report['collected_at_utc'] = datetime.now(timezone.utc).isoformat()
    else:
        report = collect()
    rendered = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.write_text(rendered, encoding='utf-8')
    print(rendered, end='')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
