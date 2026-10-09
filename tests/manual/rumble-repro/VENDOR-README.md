# IINE rumble comparison

For an Apple Silicon Mac. No installation or extra software is needed.

## Run

1. Connect the IINE controller over Bluetooth in Switch Pro mode. Connect an
   official Switch Pro controller too, if available. Quit games using them.
2. Extract this ZIP and open Terminal in the extracted folder.
3. Hold both controllers and run:

```sh
./rumble-test
```

The test takes about 25 seconds. It pulses every connected controller together:
three pulses at 50%, three at 100%, then three pairs of short pulses at 100%.
Press Ctrl-C to stop.

## What to look for

Compare the controllers during each phase. Do the IINE pulses start weak,
get weaker, or stop being perceptible? Does increasing to 100% restore them?
If already silent, power the IINE off and reconnect before repeating.

## Why this is wrong

The test sends identical commands to both controllers, with no fade or decrease
within each phase. Repeated equal commands should not progressively lose their
vibration, and increasing the requested intensity should not leave it silent.

In our test, the IINE faded rapidly during the 50% phase. By the 100% phase,
no vibration could be felt, although macOS accepted every command. In a separate
50% run, the official Switch Pro produced very strong pulses. This reproduces
the problem through Apple's controller haptics API, without the game.
