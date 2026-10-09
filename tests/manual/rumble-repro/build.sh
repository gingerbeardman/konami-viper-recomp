#!/bin/sh
set -eu
cd "$(dirname "$0")"
output=${1:-../../../build/rumble-repro}
mkdir -p "$output"
output=$(cd "$output" && pwd)
config=${SDL2_CONFIG:-sdl2-config}
prefix=$("$config" --prefix)
# SDL flags intentionally split into compiler arguments, as in sdl2-config usage.
${CC:-cc} -std=c11 -Wall -Wextra -Werror rumble-repro.c \
    $("$config" --cflags --libs) -o "$output/rumble-repro"
${CC:-cc} -fobjc-arc -Wall -Wextra -Werror apple-rumble-repro.m \
    -framework Foundation -framework GameController -framework CoreHaptics \
    -o "$output/apple-rumble-repro"
dependency=$(otool -L "$output/rumble-repro" | awk '/\/libSDL2[^ ]*\.dylib / {print $1; exit}')
if [ -z "$dependency" ]; then
    printf 'Cannot locate the linked SDL2 dylib.\n' >&2
    exit 1
fi
cp -L "$dependency" "$output/libSDL2.dylib"
chmod u+w "$output/libSDL2.dylib"
install_name_tool -change "$dependency" '@executable_path/libSDL2.dylib' "$output/rumble-repro"
install_name_tool -id '@loader_path/libSDL2.dylib' "$output/libSDL2.dylib"
cp "$prefix/include/SDL2/SDL_copying.h" "$output/SDL2-LICENSE.h"
# sdl2-compat loads SDL3 at runtime. Its loader searches next to this library.
if strings "$dependency" | rg -q 'sdl2-compat:'; then
    if [ -f "$output/libSDL3.dylib" ]; then chmod u+w "$output/libSDL3.dylib"; fi
    cp -L "$prefix/lib/libSDL3.dylib" "$output/libSDL3.dylib"
    chmod u+w "$output/libSDL3.dylib"
    cp "$prefix/include/SDL3/SDL_copying.h" "$output/SDL3-LICENSE.h"
    cp "$prefix/share/licenses/sdl2-compat/LICENSE.txt" "$output/SDL2-COMPAT-LICENSE.txt"
    codesign --force --sign - "$output/libSDL3.dylib"
fi
codesign --force --sign - "$output/libSDL2.dylib"
codesign --force --sign - "$output/rumble-repro"
codesign --force --sign - "$output/apple-rumble-repro"
cp rumble-repro.c apple-rumble-repro.m build.sh README.md FINDINGS.md LICENSE.txt "$output/"
{
    sw_vers
    uname -m
    "$config" --version
    "$config" --prefix
    ${CC:-cc} --version
} > "$output/BUILD-INFO.txt"
file "$output/rumble-repro"
printf 'Built standalone package: %s\n' "$output"
