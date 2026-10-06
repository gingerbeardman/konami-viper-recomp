#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
MISSION_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$MISSION_TEST_DIR"' EXIT HUP INT TERM
for test in rules mode targets; do
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
        "tests/test_mission_$test.c" -lm -o "$MISSION_TEST_DIR/$test"
    if [ "$test" = targets ]; then "$MISSION_TEST_DIR/$test"; else "$MISSION_TEST_DIR/$test" "$MISSION_TEST_DIR/settings.ini"; fi
done
for style in 1 2; do
    ${CC:-cc} -std=c11 -Wall -Wextra -Werror -Iruntime \
        -DGAME_ENH_RACE_RESTART_STYLE=$style tests/test_race_restart.c -lm -o "$MISSION_TEST_DIR/restart"
    "$MISSION_TEST_DIR/restart"
done
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Werror \
    -Iruntime tests/test_course_reverse.c -lm -o "$MISSION_TEST_DIR/reverse"
"$MISSION_TEST_DIR/reverse"
python3 - "$MISSION_TEST_DIR" <<'PYCONFIG'
import sys
sys.path.insert(0, 'tools')
import game
game.write_config_header(game.load('gticlub2'), sys.argv[1])
PYCONFIG
case $(uname -s) in
    Darwin) MISSION_GC_SECTIONS=-Wl,-dead_strip ;;
    *) MISSION_GC_SECTIONS=-Wl,--gc-sections ;;
esac
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra \
    -Wno-missing-field-initializers -Wno-unused-function -ffunction-sections -fdata-sections \
    -Iruntime -I"$MISSION_TEST_DIR" tests/test_mission_menu.c "$MISSION_GC_SECTIONS" \
    -lm -o "$MISSION_TEST_DIR/menu"
"$MISSION_TEST_DIR/menu"
# Verify unsupported games still compile with their own generated profiles.
for game_id in gticlub2ea thrild2 thrild2a thrild2j; do
    python3 - "$MISSION_TEST_DIR" "$game_id" <<'PYCONFIG'
import sys
sys.path.insert(0, 'tools')
import game
game.write_config_header(game.load(sys.argv[2]), sys.argv[1])
PYCONFIG
    ${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra \
        -Wno-missing-field-initializers -Wno-unused-function -Iruntime -I"$MISSION_TEST_DIR" \
        -fsyntax-only runtime/enhanced.c
done
