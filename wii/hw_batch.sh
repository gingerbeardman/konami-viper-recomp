#!/bin/sh
# A/B a batch of build variants on the real Wii (see wii/hw_remote.py).
# Usage: sh wii/hw_batch.sh NAME RUNS "label:VAR=1 VAR2=x" ["label2:..."]...
# Each variant is the live-profile bench (WII_PERF_BENCH_ID=40) plus its
# variables; an empty variable list is the baseline. Every variant is checked
# for exactness in Dolphin (wii/dolphin_measure.sh, against ORACLE_RAM /
# ORACLE_EFB) while each also runs RUNS times on the Wii at 192.168.1.224
# (WII_IP), which must be at the Homebrew Channel menu. Results:
# build/wii/hw-batch/NAME/summary.tsv.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
NAME=$1; RUNS=$2; shift 2
OUT=build/wii/hw-batch/$NAME; mkdir -p "$OUT"
WII=${WII_IP:-192.168.1.224}
: > "$OUT/variants.txt"; : > "$OUT/built.txt"
# 1. Build every variant.
for spec in "$@"; do
  label=${spec%%:*}; vars=${spec#*:}
  rm -rf wii/__pycache__
  if ! (. wii/live-profile-env.sh; env $vars WII_PERF_BENCH_ID=40 sh wii/build_profile.sh 1) > "$OUT/build-$label.log" 2>&1; then
    echo "$label: build failed"; grep -m3 -i error "$OUT/build-$label.log"; continue
  fi
  dol=$(sed -n 's/^VIPER BUILD OUTPUT //p' "$OUT/build-$label.log" | tail -1)
  [ -f "$dol" ] || { echo "$label: no DOL"; continue; }
  cp "$dol" "$OUT/$label.dol"; echo "$label $dol" >> "$OUT/built.txt"
done
# 2. Dolphin (RAM + EFB oracle) in the background while the Wii runs each
#    variant (its own RAM check reports MATCH/DIFFERENT): they are independent.
(while read -r label dol; do
   row=$(sh wii/dolphin_measure.sh "$NAME-$label" "$dol" 900 | tail -1)
   echo "$label	$row" >> "$OUT/variants.txt"; echo "dolphin $label: $row"
 done < "$OUT/built.txt") &
DOLPHIN=$!
while read -r label dol; do
  while pgrep -f "hw_remote.py" >/dev/null; do sleep 10; done   # one Wii user at a time
  python3 wii/hw_remote.py --wii "$WII" "$OUT/$label.dol" "$OUT/wii-$label" --repeat "$RUNS" --timeout 900 > "$OUT/wii-$label.log" 2>&1
  echo "wii $label: $(grep -o 'ram=[0-9a-f]* [A-Z]* driving=[0-9.]*s' "$OUT/wii-$label.log" | sed 's/ram=[0-9a-f]* //; s/driving=//' | tr '\n' ' ')"
  sleep 8
done < "$OUT/built.txt"
wait $DOLPHIN
# 3. Summary; a variant is exact when Dolphin matches both oracles.
printf 'label\tdolphin_us\tdolphin_ram\tdolphin_efb\texact\twii_runs\n' > "$OUT/summary.tsv"
while IFS='	' read -r label _ run us ram efb result; do
  exact=no; [ "$ram" = "${ORACLE_RAM:-40971e3d}" ] && [ "$efb" = "${ORACLE_EFB:-2c392b9d7460}" ] && exact=yes
  times=$(grep -o 'ram=[0-9a-f]* [A-Z]* driving=[0-9.]*s' "$OUT/wii-$label.log" 2>/dev/null | sed 's/ram=[0-9a-f]* //; s/driving=//' | tr '\n' ' ')
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$label" "$us" "$ram" "$efb" "$exact" "$times" >> "$OUT/summary.tsv"
done < "$OUT/variants.txt"
column -t -s '	' "$OUT/summary.tsv"
