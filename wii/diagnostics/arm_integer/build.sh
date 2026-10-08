#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
exec docker run --rm -v "$PWD:/src" -w /src "${WII_SDK_IMAGE:-devkitpro/devkitppc:latest}" bash -lc '
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"
for op in 0 1; do
 for dest in 0 5; do
  make -f wii/diagnostics/arm_integer/Makefile OUT=build/wii/arm-integer-conformance/op$op-r$dest DEST=$dest SUBTRACT=$op all || exit
 done
done'
