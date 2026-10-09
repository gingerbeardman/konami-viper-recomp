# Rumble tests

For the standalone macOS package intended for controller-vendor investigation,
see [manual/rumble-repro/README.md](manual/rumble-repro/README.md). It compares
equal pulses, one sustained effect, and the game's constant-strength refresh
cadence, with bundled SDL libraries and no game dependency.
The smaller [Apple-only vendor package](manual/rumble-repro/VENDOR-README.md)
defaults to simultaneous three-pulse 50% and 100% comparisons on all controllers,
followed by three pairs of short 100% pulses. It also supports adjustable
intensity and explicit controller selection.
Its [findings](manual/rumble-repro/FINDINGS.md) record the IINE's weak/fading
50% output, weak 100% output and the official controller's strong 50% output.

Run `tests/run-rumble-tests.sh`. No game data is needed.
Uses an SDL virtual device and a recording rumble transport to test torque, stop, expiry duration, throttling, transient failures and retry throttling and tick wrap. SDL 2.24+ is needed for the virtual-device test.

## Physical SDL comparison

Build `tests/manual/build-sdl-rumble.sh`, then quit games and run
`/tmp/viper-sdl-rumble`. It sends three equal one-second 50% pulses through
`SDL_GameControllerRumble`, pumping SDL events throughout. It does not call our
Apple backend. Exactly one recognized controller must be connected.

Use `--list` for discovery without vibration, or `--driver hidapi` / `--driver native`
to compare SDL routing preferences. The latter disables HIDAPI; the former enables
it but does not guarantee SDL selects that driver. Logs include runtime version,
revision, requested driver hints, device path, capability and submission errors.
An accepted API call is not proof of physical vibration. Record whether the pulses
are equal, fade, are silent, or disconnect the controller. Ctrl-C stops the test.
