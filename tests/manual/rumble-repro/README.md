# macOS controller rumble fade reproduction

These standalone command-line apps send constant, equal commands through
Apple GameController/CoreHaptics (`apple-rumble-repro`) or SDL
(`rumble-repro`). They isolate the rumble transport from the game,
cabinet force feedback, settings, and input handling. The reported issue is that
physical vibration becomes weaker despite unchanged requested strength. Whether
this occurs on your hardware must be checked by feel; API success cannot measure
vibration or identify whether firmware, transport, or the host driver causes it.

## Run the supplied binary

The supplied build is for Apple Silicon macOS. Keep the executable and bundled
dylibs in the same directory. No Homebrew or game files are needed to run it.
This development binary is ad hoc signed, not Apple notarized; use the source
build below if your Mac's security settings prevent running it.

Quit games and other apps using the controller. Connect exactly one controller
in the same mode and over the same connection that shows the problem. In Terminal,
change to this directory, then run:

```sh
./apple-rumble-repro --list
./apple-rumble-repro 2>&1 | tee rumble-apple.txt
```

The default Apple comparison takes about 25 seconds. It sends three 50% pulses,
three 100% pulses and three pairs of 150ms 100% pulses to all discovered
haptics-capable controllers together. `--strength` alone selects one three-pulse
phase. `--mode all` runs the longer pulse/hold/refresh diagnostic (about 43 seconds).
The SDL comparison takes about 56 seconds.
**Ctrl-C stops the effect and exits.**
The Apple executable uses only macOS system frameworks and needs no bundled dylibs.
Its continuous events match the game's experimental Apple backend: intensity 0.5,
sharpness 0.5, one reused engine, and a stopped/replaced player on each refresh.
Engine resets, stops, and API errors are logged.

Apple effects request **intensity=0.5, sharpness=0.5**. SDL effects request
**32768 / 65535 (50%) on both motors**. Neither app requests an intensity fade:

1. **Pulses:** three equal one-second Apple effects (ten for SDL). Apple waits
   1.2 seconds after each start, explicitly stops, then waits one second.
   Compare the first and last pulses.
2. **Hold:** one ten-second effect. Observe whether its strength decays while it
   is still meant to be playing.
3. **Refresh:** twenty seconds of 100 ms effects renewed approximately every
   50 ms. This repeats the game's constant-torque rumble commands. Observe whether
   strength decays even as identical commands continue to be accepted.

To repeat one phase, use `--mode pulses`, `--mode hold`, or `--mode refresh`.
These are separate diagnostic cases, not a claim that all three show the issue.

Apple intensity can be changed with `--strength 1` for a 100% request. All three
pulses use the same requested value. This does not guarantee physical strength.
By default all haptics-capable controllers play the same pattern together.
To target only one controller, use `--list` and select a unique exact vendor name:

```sh
./apple-rumble-repro --mode pulses --controller 'Real Pro Controller'
./apple-rumble-repro --mode pulses --controller 'Pro Controller' --strength 1
```

Names may be customized or change between connections; confirm them with
`--list` first. Ambiguous or missing matches produce no effects.

## Compare driver routing

Compare SDL with the direct Apple test:

```sh
./rumble-repro --list
./rumble-repro 2>&1 | tee rumble-default.txt
./rumble-repro --driver hidapi > rumble-hidapi.txt 2>&1
./rumble-repro --driver native > rumble-native.txt 2>&1
```

`hidapi` enables SDL's HIDAPI preference; `native` disables HIDAPI. These flags
do not guarantee a particular implementation is selected or that the selected
route supports rumble. Keep each log, including any error or disconnect.
API acceptance does not confirm physical vibration. Report silent output
separately from fading output. See FINDINGS.md for the local investigation.

## Record alongside the log

- Controller make/model, firmware version, and selected controller mode.
- macOS version; Bluetooth or USB (and cable/dongle if relevant).
- Battery charge, and whether other controllers show the same behavior.
- For each phase: steady, fades within an effect, later pulses weaker, silent,
  or disconnects; approximate time until fading begins.
- Whether a power cycle or reconnect restores the initial strength. If so, start
  each comparison from that state, with the same rest time between runs.

Logs include SDL version/revision, driver hints, device name/path, VID/PID/GUID,
reported firmware (may be zero if unavailable), and accepted commands/errors.
Device paths may identify your device; review logs before forwarding.
Apple logs include macOS version, vendor/category, haptic localities and effects.
The controller presents as a Switch Pro and does not reveal its third-party brand;
record that manually.

## Build from source

Requires macOS Xcode Command Line Tools, SDL 2.24+ development files, and `rg`.
With Homebrew SDL2 installed, run:

```sh
sh build.sh ./dist
./dist/rumble-repro --list
```

`SDL2_CONFIG` and `CC` can override `sdl2-config` and the compiler. The build script
copies the linked SDL2 dylib and its license; with Homebrew's sdl2-compat it also
copies SDL3 and its license. It signs the executable and libraries for local use.
The build uses the local compiler architecture; build on an Intel Mac to produce
an Intel package. The source has no dependency on the game repository.

To build only the Apple repro, without SDL or Homebrew:

```sh
clang -fobjc-arc -Wall -Wextra -Werror apple-rumble-repro.m \
  -framework Foundation -framework GameController -framework CoreHaptics \
  -o apple-rumble-repro
./apple-rumble-repro --mode pulses
```
