#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
TEMP_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_TEST_DIR"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Iruntime tests/test_controller_gyro.c -lm -o "$TEMP_TEST_DIR/test"
"$TEMP_TEST_DIR/test"
