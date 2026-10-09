#!/bin/sh
# Emulator settings follow middleware's Wii smoke tests; port code is standalone.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
DOL=${1:?Usage: bash wii/run_dolphin.sh path/to/program.dol}
[ -f "$DOL" ] || { echo "Missing DOL: $DOL" >&2; exit 1; }
# SIGTERM may open Dolphin's quit confirmation and leave it alive. Refuse a
# second instance; the caller must force-stop its owned test and verify exit.
python3 - <<'PY'
import subprocess, sys
rows = subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True)
running = []
for row in rows.splitlines():
    fields = row.strip().split(None, 1)
    if len(fields) == 2 and fields[1].rsplit('/', 1)[-1] == 'Dolphin':
        running.append(fields[0])
if running:
    sys.exit('Dolphin already running (PID ' + ', '.join(running) +
             '). Force-stop the owned test with kill -9 and verify exit before launching.')
PY
mkdir -p "$ROOT/build/wii/dolphin-user"
mkdir -p "$ROOT/build/wii/runs"
RUN_DIR=$(mktemp -d "$ROOT/build/wii/runs/run.XXXXXX")
cp "$DOL" "$RUN_DIR/viper.dol"
python3 "$ROOT/wii/pad_dol.py" "$RUN_DIR/viper.dol"
shasum -a 256 "$RUN_DIR/viper.dol" > "$RUN_DIR/dol.sha256"
printf '%s\n' "$DOL" > "$RUN_DIR/source-dol.txt"
shasum -a 256 "$DOL" > "$RUN_DIR/source-dol.sha256"
# Preserve renderer source alongside binary diagnostics. This is the source
# at launch time; the archived ELF remains authoritative for compiled code.
mkdir -p "$RUN_DIR/renderer-source"
mkdir -p "$RUN_DIR/device-source"
mkdir -p "$RUN_DIR/runtime-source"
cp "$ROOT/runtime/ppc_rt.h" "$ROOT/runtime/runtime.h" "$RUN_DIR/runtime-source/"
cp "$ROOT/wii/gpr_multiple.h" "$ROOT/wii/specialize_gpr_multiple.py" "$ROOT/wii/test_gpr_multiple.py" "$RUN_DIR/runtime-source/"
cp "$ROOT/wii/cr_unpack.h" "$ROOT/wii/test_cr_unpack.py" "$RUN_DIR/runtime-source/"
if [ -d "$(dirname "$DOL")/generated" ]; then
  for source in "$(dirname "$DOL")"/generated/*.gpr.c; do
    [ -f "$source" ] || continue
    mkdir -p "$RUN_DIR/compiler-generated-source"
    cp "$source" "$RUN_DIR/compiler-generated-source/"
  done
fi
cp "$ROOT/wii/ram_access_cold.c" "$ROOT/wii/test_split_ram_helpers.py" "$RUN_DIR/device-source/"
cp "$ROOT/wii/test_split_ram_float.py" "$RUN_DIR/device-source/"
for source in voodoo_headless.c voodoo_headless.h pc_profile.c main.c Makefile build_profile.sh submission_profile.c submission_profile.h instrument_submission.py specialize_submission.py specialize_driving_writer.py specialize_driving_tail.py test_driving_writer.py driving_writer_probe_body.c specialize_bounded_submission.py bounded_submission.h texture_layout_cache.h test_texture_layout_cache.py; do
  cp "$ROOT/wii/$source" "$RUN_DIR/device-source/$source"
done
cp "$ROOT/wii/texture_layout.h" "$RUN_DIR/device-source/texture_layout.h"
for source in native_texture_source.h gx_texture_resource.h texture_resource_probe.c gx_renderer.c render_family.h gx_rejection.h gx_color_equation.h voodoo_color_plan.h rejection_policy.h gx_pipeline_plan.h gx_fog_stage.h gx_tmu_equation.h gx_tmu_pipeline_plan.h gx_constant_fog.h gx_material_plan.h bulk_ram.h gx_combiner_program_cache.h combiner_program_probe_common.h combiner_program_probe.c test_combiner_program.py gx_tmu_prefix_cache.h tmu_prefix_probe_common.h test_tmu_prefix.py extract_tmu_prefix_reference.py tmu_prefix_test_constants.h gx_tev_words.h gx_tev_stage.h lookup_plane_cache.h lookup_bind_probe.c projective_texture.h projective_texture.c wdepth_split.c wdepth_split.h merged_lookup_probe.c test_merged_wdepth.py test_merged_lookup.py voodoo_tmu_plan.h voodoo_tmu_eval.h texture.h texture.c texture_cache.h; do
  cp "$ROOT/wii/$source" "$RUN_DIR/renderer-source/$source"
done
cp "$ROOT/wii/test_native_texture_bind.py" "$RUN_DIR/renderer-source/"
cp "$ROOT/wii/test_staging_guard.py" "$RUN_DIR/device-source/"
cp "$ROOT/wii/specialize_direct_state.py" "$ROOT/wii/test_direct_state.py" "$RUN_DIR/device-source/"
cp "$ROOT/wii/gx_material_run.h" "$ROOT/wii/test_material_run.c" "$RUN_DIR/renderer-source/"
cp "$ROOT/wii/gx_renderer.h" "$RUN_DIR/renderer-source/"
FIXED_DOLPHIN_BIN="$ROOT/build/tools/dolphin-2606-fix-build/Binaries/Dolphin.app/Contents/MacOS/Dolphin"
EMULATOR_BIN=${DOLPHIN_BIN:-$FIXED_DOLPHIN_BIN}
DEFAULT_CPU_CORE=5
if [ "$EMULATOR_BIN" = "$FIXED_DOLPHIN_BIN" ]; then
  DEFAULT_CPU_CORE=4
fi
printf '%s\n' "$EMULATOR_BIN" > "$RUN_DIR/dolphin-binary.txt"
shasum -a 256 "$EMULATOR_BIN" > "$RUN_DIR/dolphin-binary.sha256"
# Keep adjacent debug artifacts for address lookup after later rebuilds.
# Build and launch sequentially so these belong to the same link invocation.
SOURCE_BASE=${DOL%.dol}
if [ -f "$(dirname "$DOL")/split_ram_float_probe.c" ]; then
  cp "$(dirname "$DOL")/split_ram_float_probe.c" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/f64_ram_probe.c" ]; then
  cp "$(dirname "$DOL")/f64_ram_probe.c" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/split_ram_probe.c" ]; then
  cp "$(dirname "$DOL")/split_ram_probe.c" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/generated/gl_000.bulk.c" ]; then
  mkdir -p "$RUN_DIR/compiler-generated-source"
  cp "$(dirname "$DOL")/generated/gl_000.bulk.c" "$RUN_DIR/compiler-generated-source/"
  shasum -a 256 "$RUN_DIR/compiler-generated-source/gl_000.bulk.c" > "$RUN_DIR/compiler-generated-source/source.sha256"
fi
if [ -f "$(dirname "$DOL")/tmu_prefix_reference.h" ]; then
  cp "$(dirname "$DOL")/tmu_prefix_reference.h" "$RUN_DIR/renderer-source/"
fi
if [ -f "$(dirname "$DOL")/bulk_writer_probe.c" ]; then
  cp "$(dirname "$DOL")/bulk_writer_probe.c" "$ROOT/wii/test_bulk_writer.py" "$ROOT/wii/test_bulk_publish.py" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/driving_writer_probe.c" ]; then
  cp "$(dirname "$DOL")/driving_writer_probe.c" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/bounded_submission_probe.c" ]; then
  cp "$(dirname "$DOL")/bounded_submission_probe.c" "$ROOT/wii/test_bounded_submission.py" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/borrow_clip_probe.c" ]; then
  cp "$(dirname "$DOL")/borrow_clip_probe.c" "$ROOT/wii/test_borrow_clip.py" "$RUN_DIR/renderer-source/"
fi
if [ -f "$(dirname "$DOL")/split_copy_probe.c" ]; then
  cp "$(dirname "$DOL")/split_copy_probe.c" "$ROOT/wii/test_borrow_clip.py" "$RUN_DIR/renderer-source/"
fi
if [ -f "$(dirname "$DOL")/gx-fusion-sdk.audit.txt" ]; then
  cp "$ROOT/wii/test_tev_words.c" "$RUN_DIR/renderer-source/"
  cp "$(dirname "$DOL")/gx-fusion-sdk.audit.txt" "$(dirname "$DOL")/gx-fusion-sdk.o" "$(dirname "$DOL")/gx-fusion-sdk.original.o" "$RUN_DIR/renderer-source/"
fi
if [ -f "$(dirname "$DOL")/generated/gl_000.instrumented.c" ]; then
  cp "$(dirname "$DOL")/generated/gl_000.instrumented.c" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/generated/gl_000.bulk.c" ]; then
  cp "$(dirname "$DOL")/generated/gl_000.bulk.c" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/direct_submit59_probe.c" ]; then
  cp "$(dirname "$DOL")/direct_submit59_probe.c" "$RUN_DIR/device-source/"
  cp "$ROOT/wii/voodoo_headless_test.c" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/direct_preheader_probe.c" ]; then
  cp "$(dirname "$DOL")/direct_preheader_probe.c" "$ROOT/wii/voodoo_headless_test.c" "$RUN_DIR/device-source/"
fi
if [ -f "$(dirname "$DOL")/direct_vertex_probe.c" ]; then
  cp "$(dirname "$DOL")/direct_vertex_probe.c" "$ROOT/wii/voodoo_headless_test.c" "$RUN_DIR/device-source/"
fi
if [ -f "$SOURCE_BASE.elf" ]; then
  cp "$SOURCE_BASE.elf" "$RUN_DIR/program.elf"
fi
if [ -f "$SOURCE_BASE.map" ]; then
  cp "$SOURCE_BASE.map" "$RUN_DIR/program.map"
fi
printf 'architecture=%s\ncpu_core=%s\nemulation_speed=%s\n' "${WII_DOLPHIN_ARCH:-native}" "${WII_DOLPHIN_CPU_CORE:-$DEFAULT_CPU_CORE}" "${WII_DOLPHIN_SPEED:-1}" > "$RUN_DIR/launch-settings.txt"
SHADER_MODE=${WII_DOLPHIN_SHADER_MODE:-0}
case "$SHADER_MODE" in
  0|1) ;;
  *) echo 'WII_DOLPHIN_SHADER_MODE must be 0 (specialized) or 1 (synchronous ubershaders)' >&2; exit 2 ;;
esac
printf 'shader_mode=%s\n' "$SHADER_MODE" >> "$RUN_DIR/launch-settings.txt"
printf 'Dolphin test artifact: %s\n' "$RUN_DIR/viper.dol"
if [ "${WII_DOLPHIN_ARCH:-}" = x86_64 ]; then
  set -- /usr/bin/arch -x86_64 "$EMULATOR_BIN"
elif [ "${WII_DOLPHIN_ARCH:-}" = arm64 ]; then
  set -- /usr/bin/arch -arm64 "$EMULATOR_BIN"
elif [ -n "${WII_DOLPHIN_ARCH:-}" ]; then
  echo 'WII_DOLPHIN_ARCH must be empty, arm64 or x86_64' >&2
  exit 1
else
  set -- "$EMULATOR_BIN"
fi
if [ "${WII_DOLPHIN_DEBUGGER:-0}" = 1 ]; then
  set -- "$@" --debugger
else
  set -- "$@" --batch
fi
# Optional diagnostic interpreter fallback. The default patched2606 ARM64
# integer JIT contains the r0 carry-instruction fix and runs without this.
case "${WII_DOLPHIN_JIT_INTEGER_OFF:-0}" in
  0) ;;
  1) set -- "$@" -C Dolphin.Debug.JitIntegerOff=True ;;
  *) echo 'WII_DOLPHIN_JIT_INTEGER_OFF must be 0 or 1' >&2; exit 1 ;;
esac
printf 'jit_integer_off=%s\n' "${WII_DOLPHIN_JIT_INTEGER_OFF:-0}" >> "$RUN_DIR/launch-settings.txt"
if [ "${WII_DOLPHIN_LOG_ERRORS:-0}" = 1 ]; then
  set -- "$@" -C Logger.Options.WriteToFile=True -C Logger.Options.Verbosity=3 -C Dolphin.Interface.UsePanicHandlers=False -C Dolphin.Interface.PauseOnFocusLost=False
  python3 - "$ROOT/build/wii/dolphin-user/Config/Logger.ini" > "$RUN_DIR/log-categories.txt" <<'PY'
import configparser, sys
c = configparser.ConfigParser(interpolation=None)
c.optionxform = str
c.read(sys.argv[1])
names = set(c['Logs']) if c.has_section('Logs') else set()
for name in sorted(names | {'JIT', 'MASTER', 'CORE', 'BOOT', 'Video'}):
    print(name)
PY
  while IFS= read -r log_category; do
    set -- "$@" -C "Logger.Logs.$log_category=True"
  done < "$RUN_DIR/log-categories.txt"
fi
printf 'log_errors=%s\n' "${WII_DOLPHIN_LOG_ERRORS:-0}" >> "$RUN_DIR/launch-settings.txt"
# Emulator-side inputs exercise the live WPAD path without changing game code.
# Controller expressions are read from INI, not ordinary -C config overrides.
# Use a private profile so saved user mappings remain intact.
USER_DIR="$ROOT/build/wii/dolphin-user"
if [ "${WII_DOLPHIN_INPUT_TEST:-0}" = 1 ]; then
  USER_DIR="$RUN_DIR/user"
  mkdir -p "$USER_DIR"
  for profile_part in Config Load Wii WiiSDSync; do
    if [ -d "$ROOT/build/wii/dolphin-user/$profile_part" ]; then
      cp -R "$ROOT/build/wii/dolphin-user/$profile_part" "$USER_DIR/"
    fi
  done
  python3 - "$USER_DIR/Config/WiimoteNew.ini" "${WII_DOLPHIN_STEER_TEST:-0}" "${WII_DOLPHIN_START_TEST_SECONDS:-0}" <<'PY'
import configparser, sys
from pathlib import Path
p = Path(sys.argv[1])
c = configparser.ConfigParser(interpolation=None)
c.optionxform = str
c.read(p)
if not c.has_section('Wiimote1'):
    c.add_section('Wiimote1')
c['Wiimote1']['Buttons/2'] = '1.0'
c['Wiimote1']['Buttons/+'] = 'timer(10)>0.9'
start_seconds = int(sys.argv[3])
if not 0 <= start_seconds <= 86400:
    raise ValueError('WII_DOLPHIN_START_TEST_SECONDS must be 0..86400')
if start_seconds:
    # Timer expressions use host elapsed time. A long period avoids restarting
    # the Start pulses during any practical diagnostic run; zero keeps legacy.
    c['Wiimote1']['Buttons/+'] = f'(timer(10)>0.9)&(timer(1000000000)<{start_seconds / 1000000000:.12f})'
if sys.argv[2] == '1':
    c['Wiimote1']['D-Pad/Left'] = 'timer(12)>0.75'
    c['Wiimote1']['D-Pad/Right'] = '(timer(12)>0.25)&(timer(12)<0.5)'
with p.open('w') as f:
    c.write(f)
PY
  printf 'input_test=held_throttle_periodic_start\n' >> "$RUN_DIR/launch-settings.txt"
  printf 'steer_test=%s\n' "${WII_DOLPHIN_STEER_TEST:-0}" >> "$RUN_DIR/launch-settings.txt"
  printf 'start_test_seconds=%s\n' "${WII_DOLPHIN_START_TEST_SECONDS:-0}" >> "$RUN_DIR/launch-settings.txt"
  set -- "$@" -C Dolphin.Input.BackgroundInput=True
fi
# Optional emulated CPU clock override (e.g. 0.6): code runs slower relative
# to emulated time, as in heavier scenes; for adaptive-timing tests.
if [ -n "${WII_DOLPHIN_OVERCLOCK:-}" ]; then
  printf 'cpu_overclock=%s\n' "$WII_DOLPHIN_OVERCLOCK" >> "$RUN_DIR/launch-settings.txt"
  set -- "$@" -C Dolphin.Core.OverclockEnable=True -C Dolphin.Core.Overclock="$WII_DOLPHIN_OVERCLOCK"
fi
# Console aspect: 169 (default, like the user's Wii: a 16:9 console and
# window) or 43 (the scripted benchmark: its EFB oracle is the full 4:3 frame).
case "${WII_DOLPHIN_ASPECT:-169}" in
  169) console_wide=True; window_aspect=1; window_w=854 ;;
  43) console_wide=False; window_aspect=2; window_w=640 ;;
  *) echo 'WII_DOLPHIN_ASPECT must be 169 or 43' >&2; exit 2 ;;
esac
printf 'aspect=%s\n' "${WII_DOLPHIN_ASPECT:-169}" >> "$RUN_DIR/launch-settings.txt"
# The render window opens with a window_w x 480 picture area (DolphinQt
# ignores RenderWindowWidth/Height and restores Qt.ini renderwidget/geometry,
# so that is written here; Dolphin is killed, so it never saves over it).
# Position: WII_DOLPHIN_WINDOW="x y screen screen_width" (default: the left
# monitor, where it was placed by hand).
python3 - "$USER_DIR/Config/Qt.ini" "$window_w" ${WII_DOLPHIN_WINDOW:--1512 38 1 1512} <<'PY'
import sys,os,struct,re
p,w,x,y,screen,sw=sys.argv[1],*map(int,sys.argv[2:7])
title=28;h=480
frame=(x,y,x+w-1,y+title+h-1);normal=(x,y+title,x+w-1,y+title+h-1)
blob=struct.pack('>IHH4i4iiBBi4i',0x01d9d0cb,3,0,*frame,*normal,screen,0,0,sw,*normal)
value='@ByteArray('+''.join('\\x%02x'%b for b in blob)+')'
text=open(p,encoding='latin-1').read() if os.path.exists(p) else ''
text=re.sub(r'\n?\[renderwidget\]\n(?:[^\[\n][^\n]*\n?)*','\n',text)
text=text.rstrip('\n')+'\n\n[renderwidget]\ngeometry='+value+'\n'
open(p,'w',encoding='latin-1').write(text)
PY
python3 - "$USER_DIR/Config/Dolphin.ini" <<'PY'
import sys,os,configparser
p=sys.argv[1]
c=configparser.RawConfigParser();c.optionxform=str
if os.path.exists(p):c.read(p,encoding='utf-8')
if c.has_section('Display'):
    for k in ('RenderWindowWidth','RenderWindowHeight'):c.remove_option('Display',k)
    c.set('Display','RenderWindowAutoSize','False')
with open(p,'w',encoding='utf-8') as f:c.write(f,space_around_delimiters=True)
PY
printf 'user_dir=%s\n' "$USER_DIR" >> "$RUN_DIR/launch-settings.txt"
printf 'fps_overlay=True\n' >> "$RUN_DIR/launch-settings.txt"
exec "$@" \
  -u "$USER_DIR" \
  -C Dolphin.Core.CPUCore="${WII_DOLPHIN_CPU_CORE:-$DEFAULT_CPU_CORE}" \
  -C Dolphin.Core.EmulationSpeed="${WII_DOLPHIN_SPEED:-1}" \
  -C Dolphin.Core.WiiSDCard=True \
  -C Dolphin.Core.WiiSDCardEnableFolderSync=True \
  -C Dolphin.BluetoothPassthrough.Enabled=False \
  -C WiimoteNew.Wiimote1.Source=1 \
  -C Graphics.Hacks.EFBAccessEnable=True \
  -C Graphics.Settings.ShowFPS=True \
  -C Graphics.Settings.ShaderCompilationMode="$SHADER_MODE" \
  -C SYSCONF.IPL.AR=$console_wide -C Graphics.Settings.AspectRatio=$window_aspect --exec="$RUN_DIR/viper.dol"
