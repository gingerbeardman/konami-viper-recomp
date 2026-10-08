#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
exec docker run --rm -v "$PWD:/src" -w /src \
    "${WII_SDK_IMAGE:-devkitpro/devkitppc:latest}" bash -lc \
    'export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC; export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"; exec make -f wii/Makefile "$@"' bash "$@"
