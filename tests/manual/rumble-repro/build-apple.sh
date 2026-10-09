#!/bin/sh
set -eu
cd "$(dirname "$0")"
output=${1:-../../../build/iine-rumble-repro}
mkdir -p "$output"
output=$(cd "$output" && pwd)
${CC:-clang} -fobjc-arc -Wall -Wextra -Werror apple-rumble-repro.m \
    -framework Foundation -framework GameController -framework CoreHaptics \
    -o "$output/rumble-test"
codesign --force --sign - "$output/rumble-test"
cp apple-rumble-repro.m build-apple.sh FINDINGS.md LICENSE.txt "$output/"
if [ -f VENDOR-README.md ]; then
    cp VENDOR-README.md "$output/README.md"
else
    cp README.md "$output/README.md"
fi
{
    sw_vers
    uname -m
    ${CC:-clang} --version
} > "$output/BUILD-INFO.txt"
file "$output/rumble-test"
printf 'Built Apple-only vendor package: %s\n' "$output"
