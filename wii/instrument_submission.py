"""Add opt-in writer-PC attribution to generated stores without editing inputs."""
import argparse
from pathlib import Path
import re


def instrument(source):
    matches = list(re.finditer(r'/\* ([0-9a-f]{8}):[^\n]*\*/|\b(ST32LE|ST32)\(', source))
    pc = None
    edits = []
    for match in matches:
        if match[1]:
            pc = int(match[1], 16)
        else:
            if pc is None:
                raise ValueError('Store has no instruction PC')
            edits.append((match.start(), match.end(),
                          f'WII_SUBMISSION_{match[2]}(0x{pc:08x}u,'))
    if not edits:
        raise ValueError('No stores found')
    for begin, end, replacement in reversed(edits):
        source = source[:begin] + replacement + source[end:]
    return '#include "submission_profile.h"\n' + source, len(edits)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result, count = instrument(args.source.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    print(f'Submission attribution: {count} static stores in {args.source}')
