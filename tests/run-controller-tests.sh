#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
TEMP_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_TEST_DIR"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Iruntime tests/test_controller_math.c -lm -o "$TEMP_TEST_DIR/math"
"$TEMP_TEST_DIR/math"
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Werror \
    -Iruntime -Itests/fixtures $(sdl2-config --cflags) tests/test_frontend_input.c \
    $(sdl2-config --libs) -lm -o "$TEMP_TEST_DIR/input"
"$TEMP_TEST_DIR/input"
