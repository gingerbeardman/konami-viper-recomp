#!/usr/bin/env python3
"""Validate a committed bounded headless race log; report emulator-model timing."""
import argparse
import json
import re
from pathlib import Path


def check(text):
    points = {}
    pending = None
    last = 0.0
    for line in text.splitlines():
        if 'VIPER WII STOP' in line:
            raise ValueError(line)
        m = re.fullmatch(r'VIPER WII PROFILE guest=([\d.]+) elapsed_us=(\d+)', line)
        if m:
            guest = float(m[1])
            if guest <= last or pending is not None:
                raise ValueError('non-monotonic or incomplete checkpoint')
            last = guest
            pending = (int(guest), int(m[2]))
        m = re.fullmatch(r'VIPER WII WORK packets=(\d+) triangles=(\d+) presents=(\d+) clears=(\d+)', line)
        if m:
            if pending is None:
                raise ValueError('work without checkpoint')
            sec, elapsed = pending
            points[sec] = dict(zip(('packets', 'triangles', 'presents', 'clears'), map(int, m.groups())))
            points[sec]['elapsed_us'] = elapsed
            pending = None
    end = re.search(r'VIPER WII SCRIPTED END result=PASS phase=4 step=11 substate=5 ram_fnv32=([0-9a-f]{8})', text)
    if pending or not end or 75 not in points or int(last) != 75:
        raise ValueError('missing complete driving checkpoint at guest 75')
    if text[end.end():].strip():
        raise ValueError('data after completion marker')
    if not all(sec in points for sec in range(1, 76)):
        raise ValueError('missing boot checkpoint')
    for sec in range(2, 76):
        if any(points[sec][k] < points[sec-1][k] for k in points[sec]):
            raise ValueError('decreasing work/time counter')
    us = points[66]['elapsed_us'] - points[63]['elapsed_us']
    frames = points[66]['presents'] - points[63]['presents']
    if us <= 0 or frames <= 0:
        raise ValueError('empty race workload')
    return {'result': 'PASS', 'driving_substate': 5, 'ram_fnv32': end[1],
            'interval': [63, 66], 'elapsed_us': us, 'presents': frames,
            'packets': points[66]['packets'] - points[63]['packets'],
            'present_equivalent_fps': frames * 1e6 / us,
            'timing_basis': 'Dolphin instruction cycle model including diagnostic I/O; not physical Wii throughput'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        result = check(args.log.read_text())
    except ValueError as error:
        parser.exit(1, f'FAIL: {error}\n')
    rendered = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end='')


if __name__ == '__main__':
    main()
