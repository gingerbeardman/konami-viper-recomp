#!/bin/sh
# Scripted-bench measurement in Dolphin (see memory: wii-dolphin-measurement).
# Usage: sh wii/dolphin_measure.sh LABEL DOL [timeout_s]
# Launches wii/run_dolphin.sh, waits for this run's SCRIPTED END in the SD
# boot.log, screenshots the window, kill -9s Dolphin (never one it did not
# start), extracts boot.log/ram.bin/efb.bin into the run directory and appends
# "label run driving_us ram_fnv32 efb_sha12 result" to build/wii/results.tsv.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LABEL=$1; DOL=$2; TIMEOUT=${3:-1200}
SD=$ROOT/build/wii/dolphin-user/Load/WiiSD.raw
LOG=$ROOT/build/wii/last-dolphin-run.log
cd "$ROOT"
pgrep -x Dolphin >/dev/null && { echo "Dolphin already running; refusing"; exit 1; }
[ -x build/winid ] || swiftc -O wii/winid.swift -o build/winid
# A finished log left in the SD sync folder looks like an instant pass.
rm -f "$ROOT/build/wii/dolphin-user/Load/WiiSDSync/viper/boot.log"
WII_DOLPHIN_ASPECT=${WII_DOLPHIN_ASPECT:-43} WII_DOLPHIN_SPEED=${WII_DOLPHIN_SPEED:-0} sh wii/run_dolphin.sh "$DOL" > "$LOG" 2>&1 &
WRAPPER=$!
# Watch and stop Dolphin itself: the launcher can exit while Dolphin runs on.
PID=
for i in $(seq 1 30); do PID=$(pgrep -nx Dolphin) && break; sleep 1; done
[ -n "$PID" ] || { echo "$LABEL: Dolphin did not start"; exit 1; }
RUN_DIR=$(sed -n 's/^Dolphin test artifact: \(.*\)\/viper.dol$/\1/p' "$LOG")
echo "pid=$PID run_dir=$RUN_DIR"
# Dolphin rebuilds WiiSD.raw from the sync folder at launch; let that finish.
sleep 8
start=$(date +%s); found=0; booted=0; last_shot=; shot_at=0
SHOT=$(mktemp -t dolphin-shot).png
# Phase 1: wait for the new boot to write a log without END; phase 2: END.
# Dolphin may write the SD image lazily, so END can stay invisible until it
# exits: also watch the window, and treat two identical screenshots 20 s apart
# (the static result screen; the game never stands still that long) as done.
while kill -0 $PID 2>/dev/null; do
  if python3 wii/read_sd_log.py "$SD" 2>/dev/null | grep -q "SCRIPTED END"; then
    [ $booted = 1 ] && { found=1; break; }
  else
    booted=1
  fi
  now=$(date +%s)
  [ $(( now-start )) -ge "$TIMEOUT" ] && break
  # Independent of the log: the SD image can keep a stale finished log.
  if [ $(( now-start )) -ge 45 ] && [ $(( now-shot_at )) -ge 20 ]; then
    shot_at=$now
    WID=$(build/winid $PID 2>/dev/null) && screencapture -x -o -l "$WID" "$SHOT" 2>/dev/null
    # Left half only: Dolphin's FPS counter (top right) changes on a still screen.
    h=$(python3 -c "import sys,hashlib;from PIL import Image;im=Image.open(sys.argv[1]);print(hashlib.sha1(im.crop((0,0,im.width//2,im.height)).tobytes()).hexdigest())" "$SHOT" 2>/dev/null)
    [ -n "$h" ] && [ "$h" = "$last_shot" ] && { found=2; break; }
    last_shot=$h
  fi
  sleep 2
done
rm -f "$SHOT"
echo "elapsed=$(( $(date +%s)-start ))s booted=$booted end_found=$found"
sleep 3
WID=$(build/winid $PID 2>/dev/null) && screencapture -x -o -l "$WID" "$RUN_DIR/screen-end.png"
kill -9 $PID $WRAPPER 2>/dev/null
for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 $PID 2>/dev/null || break; sleep 1; done
pgrep -x Dolphin >/dev/null && echo "DOLPHIN STILL RUNNING"
python3 wii/read_sd_log.py "$SD" > "$RUN_DIR/boot.log" 2>/dev/null
python3 wii/read_sd_log.py "$SD" --file ram.bin --output "$RUN_DIR/ram.bin" 2>/dev/null
python3 wii/read_sd_log.py "$SD" --file efb.bin --output "$RUN_DIR/efb.bin" 2>/dev/null
# After the kill the SD image is complete: the log decides.
grep -aq "SCRIPTED END" "$RUN_DIR/boot.log" && [ $found != 0 ] || { echo "$LABEL: no END from this run"; exit 1; }
python3 - "$LABEL" "$RUN_DIR" >> build/wii/results.tsv <<'PY'
import re,sys,hashlib,os
label,r=sys.argv[1],sys.argv[2]
s=open(r+'/boot.log',errors='replace').read()
t={int(float(g)):int(e) for g,e in re.findall(r'PROFILE guest=([\d.]+) elapsed_us=(\d+)',s)}
end=(re.findall(r'SCRIPTED END result=(\w+).*?ram_fnv32=(\w+)',s) or [('NONE','-')])[-1]
efb=hashlib.sha256(open(r+'/efb.bin','rb').read()).hexdigest()[:12] if os.path.exists(r+'/efb.bin') else '-'
print('\t'.join([label,os.path.basename(r),str(t.get(73,0)-t.get(70,0)),end[1],efb,end[0]]))
PY
tail -1 build/wii/results.tsv
