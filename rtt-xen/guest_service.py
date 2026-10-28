#!/usr/bin/env python3
"""On-board client of the approved restricted Dom0 management interface."""
import fcntl
import json
import re
import secrets
import subprocess
import sys
import time
from pathlib import Path

BASE = '/local/domain/1/rtt-admin/'

def rpc(command):
    token = secrets.token_hex(8)
    subprocess.run(['xenstore-write', BASE + 'request', token + ' ' + command], check=True)
    until = time.monotonic() + 25
    while time.monotonic() < until:
        answer = subprocess.run(['xenstore-read', '-R', BASE + 'response'],
                                capture_output=True, text=True)
        if answer.returncode == 0 and answer.stdout.startswith(token + ' '):
            header, _, body = answer.stdout.partition('\n')
            if int(header.split()[1]) < 0:
                raise RuntimeError(answer.stdout)
            return body.strip()
        time.sleep(.05)
    raise TimeoutError('Dom0 management response timed out')

def domain():
    listing = rpc('CMD xu list')
    found = re.search(r'^rtt_domu\s+(\d+)\s', listing, re.M)
    return int(found.group(1)) if found else None

def start():
    until = time.monotonic() + 90
    while True:
        try:
            rpc('STATUS')
            break
        except (TimeoutError, subprocess.CalledProcessError):
            if time.monotonic() >= until:
                raise
            time.sleep(1)
    domid = domain()
    if domid is None:
        created = rpc('CMD xu create rtt_domu -p')
        domid = int(re.search(r'domain:(\d+) created', created).group(1))
        rpc('ATTACH ' + str(domid))
        rpc('CMD xu unpause rtt_domu')
    Path('/run/rtt-guest.domid').write_text(str(domid) + '\n')
    for _ in range(10):
        result = subprocess.run(['/root/xen-tests/rtt-linux-bridge', str(domid), '--snapshot'],
                                capture_output=True, text=True)
        if result.returncode == 0:
            first = json.loads(result.stdout)
            time.sleep(.2)
            second = subprocess.run(['/root/xen-tests/rtt-linux-bridge', str(domid), '--snapshot'],
                                    capture_output=True, text=True, check=True)
            current = json.loads(second.stdout)
            if current['control'] > first['control'] and current['fp_failures'] == 0:
                print('RT-Thread ready, domid=' + str(domid), second.stdout.strip(), flush=True)
                return
        time.sleep(.5)
    raise RuntimeError('RT-Thread control heartbeat did not advance')

if __name__ == '__main__':
    with open('/run/rtt-admin.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        action = sys.argv[1] if len(sys.argv) > 1 else 'status'
        if action == 'start':
            start()
        elif action == 'stop':
            if domain() is not None:
                print(rpc('CMD xu destroy rtt_domu'))
            Path('/run/rtt-guest.domid').unlink(missing_ok=True)
        elif action == 'log':
            print(rpc('LOG'))
        elif action == 'status':
            print(rpc('STATUS'))
            print(rpc('CMD xu list'))
        else:
            raise SystemExit('usage: guest_service.py start|stop|status|log')
