"""Compare complete, matched-work profile windows; output equality is a separate gate."""
import argparse
import json
from pathlib import Path
import re

PROFILE = re.compile(r'^VIPER WII PROFILE guest=(\d+(?:\.\d+)?) elapsed_us=(\d+)$')
WORK = re.compile(r'^VIPER WII WORK packets=(\d+) triangles=(\d+) presents=(\d+) clears=(\d+)$')
FENCE = re.compile(r'^VIPER WII GX FENCE site=4 calls=(\d+) us=(\d+)$')
FIELDS = ('packets', 'triangles', 'presents', 'clears')


def interval(path, begin, end):
    blocks = {}
    current = None
    last_second = None
    for line in Path(path).read_text(errors='strict').splitlines():
        match = PROFILE.fullmatch(line)
        if line.startswith('VIPER WII PROFILE ') and not match:
            raise ValueError(f'{path}: malformed profile header')
        if match:
            guest = float(match[1]); second = int(guest)
            current = None
            if begin <= second <= end:
                if guest-second > 0.01:
                    raise ValueError(f'{path}: profile is not at a guest-second boundary')
                if second in blocks:
                    raise ValueError(f'{path}: duplicate profile second {second}')
                if last_second is not None and second <= last_second:
                    raise ValueError(f'{path}: profile seconds out of order')
                last_second = second
                current = {'elapsed_us': int(match[2])}
                blocks[second] = current
            continue
        if current is None:
            continue
        if line.startswith('VIPER WII GX FENCE site=4 '):
            match = FENCE.fullmatch(line)
            if not match or 'displayed' in current:
                raise ValueError(f'{path}: malformed or duplicate display counter')
            current['displayed'] = int(match[1])
        if line.startswith('VIPER WII WORK '):
            match = WORK.fullmatch(line)
            if not match:
                raise ValueError(f'{path}: malformed work counters')
            current.update(zip(FIELDS, map(int, match.groups())))
            current = None  # WORK closes a complete record; stale tail cannot extend it.
    for second in range(begin, end+1):
        if second not in blocks or any(key not in blocks[second] for key in FIELDS):
            raise ValueError(f'{path}: incomplete profile second {second}')
    for first, second in zip(range(begin, end), range(begin+1, end+1)):
        a, b = blocks[first], blocks[second]
        if b['elapsed_us'] <= a['elapsed_us']:
            raise ValueError(f'{path}: elapsed time failed to advance')
        for field in FIELDS:
            if b[field] < a[field]:
                raise ValueError(f'{path}: {field} decreased')
        if ('displayed' in a) != ('displayed' in b):
            raise ValueError(f'{path}: intermittent display counter')
        if 'displayed' in a and b['displayed'] < a['displayed']:
            raise ValueError(f'{path}: display counter decreased')
    a, b = blocks[begin], blocks[end]
    delta = {key: b[key]-a[key] for key in FIELDS}
    elapsed = b['elapsed_us']-a['elapsed_us']
    displayed = b['displayed']-a['displayed'] if 'displayed' in a else None
    if displayed is not None and displayed > delta['presents']:
        raise ValueError(f'{path}: displayed frames exceed logical presents')
    return {'elapsed_us': elapsed, 'work': delta,
            'simulation_rate': (end-begin)*1e6/elapsed,
            'logical_fps': delta['presents']*1e6/elapsed,
            'displayed_fps': displayed*1e6/elapsed if displayed is not None else None}


def compare(baseline, candidate, begin, end):
    if end <= begin:
        raise ValueError('end must follow begin')
    a, b = interval(baseline, begin, end), interval(candidate, begin, end)
    if a['work'] != b['work']:
        raise ValueError(f'workload differs: {a["work"]} versus {b["work"]}')
    return {'begin': begin, 'end': end, 'baseline': a, 'candidate': b,
            'time_reduction_percent': 100*(a['elapsed_us']-b['elapsed_us'])/a['elapsed_us']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--begin', required=True, type=int)
    parser.add_argument('--end', required=True, type=int)
    args = parser.parse_args()
    try:
        result = compare(args.baseline, args.candidate, args.begin, args.end)
    except (ValueError, OSError, UnicodeError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
