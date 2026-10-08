"""Summarize RT_VOODOO_STATELOG desktop reference output as JSON.

Observed states are a gameplay catalogue, not exhaustive coverage or proof
that a normalized state is implemented correctly by the Wii renderer.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('log', type=Path)
a = p.parse_args()
text = a.log.read_text(errors='replace')
names = ('cp', 'fbz', 'alpha', 'fog', 't0', 't1', 'lod0', 'lod1', 'key', 'range')
states = set()
for line in text.splitlines():
    if 'VOODOO_STATE cp=' not in line:
        continue
    fields = dict(re.findall(r'(\w+)=([0-9a-fA-F]{8})', line))
    if any(name not in fields for name in names):
        p.error('truncated or malformed state record')
    states.add(tuple(int(fields[name], 16) for name in names))
if not states:
    p.error('no VOODOO_STATE records; enable RT_VOODOO_STATELOG for the desktop reference')
features = Counter()
for values in states:
    s = dict(zip(names, values))
    cp, fbz = s['cp'], s['fbz']
    if (cp >> 7) & 1: features['texture-alpha local-source override'] += 1
    if ((cp >> 5) & 3) >= 2: features['Z/W local alpha source'] += 1
    if fbz & 4: features['stipple'] += 1
    if fbz & (1 << 13): features['alpha mask'] += 1
    if fbz & (1 << 16): features['depth bias'] += 1
    if fbz & (1 << 18): features['alpha planes'] += 1
    if fbz & 16 and not fbz & 8: features['Z depth source'] += 1
    if fbz & 2 and (s['key'] & 0xffffff or s['range'] not in (0, 0x10000000)):
        features['nondefault chroma key/range'] += 1
    if s['fog'] & 1: features['fog enabled'] += 1
    for unit in (0, 1):
        if s[f't{unit}'] == 0xffffffff:
            continue
        lod = s[f'lod{unit}']
        if (lod & 63) // 4 != ((lod >> 6) & 63) // 4:
            features[f'TMU{unit} variable mip interval'] += 1
        if s[f't{unit}'] & 16: features[f'TMU{unit} LOD dither'] += 1
report = {
    'observed_unique_states': len(states),
    'observed_colour_paths': sorted({f'{s[0]:08x}' for s in states}),
    'observed_tmu_pairs': sorted({f'{s[4]:08x}/{s[5]:08x}' for s in states}),
    'feature_state_counts': dict(sorted(features.items())),
    'catalogue_overflow': 'VOODOO_STATE OVERFLOW' in text,
    'scope': 'observed reference states only; feature counts are not unsupported-state counts',
}
print(json.dumps(report, indent=2))
