#!/usr/bin/env python3
"""Bounded CPU/memory pressure and cross-domain control validation."""
import json
import sys
from pathlib import Path
import subprocess
import time

def load(until):
    memory = bytearray(64 * 1024 * 1024)
    value = 1
    while time.monotonic() < until:
        for i in range(0, len(memory), 4096):
            value = (value * 1664525 + 1013904223) & 0xffffffff
            memory[i] = value & 255

if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--worker':
        load(float(sys.argv[2]))
        raise SystemExit(0)
    domid = Path('/run/rtt-guest.domid').read_text().strip()
    until = time.monotonic() + 65
    workers = [subprocess.Popen([sys.executable, __file__, '--worker', str(until)]) for _ in range(3)]
    try:
        began = time.monotonic()
        result = subprocess.run(['/root/xen-tests/rtt-linux-bridge', domid, '--demo', '60'],
                                capture_output=True, text=True, timeout=75, check=True)
        elapsed = time.monotonic() - began
        Path('/root/xen-tests/rtt-stress-samples.jsonl').write_text(result.stdout)
        samples = [json.loads(line) for line in result.stdout.splitlines()]
        first, last = samples[0], samples[-1]
        summary = dict(samples=len(samples), elapsed_seconds=elapsed,
                       controls=last['control'] - first['control'],
                       approx_hz=(last['control'] - first['control']) / elapsed,
                       final=last, linux_workers=3, linux_memory_mib=192)
        Path('/root/xen-tests/rtt-stress-summary.json').write_text(json.dumps(summary, indent=2))
        print(json.dumps(summary, indent=2), flush=True)
        assert last['control'] > first['control'] + 5500
        assert last['input_seq'] > first['input_seq']
        assert all(s['fp_failures'] == 0 and s['deadline_misses'] == 0 for s in samples)
    finally:
        for worker in workers:
            if worker.poll() is None:
                worker.terminate()
            worker.wait(timeout=5)
