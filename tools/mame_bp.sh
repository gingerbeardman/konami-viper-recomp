#!/bin/sh
# usage: [MAME_SET=thrild2] MAMEBP=39c60 MAMEBP_EXTRA='d@2328' tools/mame_bp.sh ROMPATH SECONDS
# ROMPATH can be this repository's roms/ folder (same layout as a MAME rompath).
HERE=$(cd "$(dirname "$0")" && pwd)
ROMS=$(cd "$1" && pwd); SECS="${2:-10}"; DIR=$(dirname "$ROMS")
cd "$DIR" && mame "${MAME_SET:-thrild2}" -rompath "$ROMS" -video none -sound none -nothrottle -seconds_to_run "$SECS" \
  -debug -debugger none -autoboot_script "$HERE/mame_bp.lua" -nvram_directory nv -cfg_directory cfg 2>/dev/null | grep BPHIT
