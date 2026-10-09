# Controller rumble investigation — 2026-10-04

## Recovered prior findings — 2026-10-02

The earlier investigation was already recorded in
[upstream rumble PR #5](https://github.com/spita90/konami-viper-recomp/pull/5).
Its physical-test results were:

- Official Nintendo Switch Pro over USB: three clear, equal SDL diagnostic
  pulses; Apple finite-duration effects also produced clear pulses. The user
  confirmed that the official controller worked in-game with SDL.
- Third-party controller identifying as Switch Pro over Bluetooth: SDL pulses
  were silent; Apple effects faded.
- The controller and connection differed between these tests, so they did not
  isolate official-controller Bluetooth behavior.

The user's
[14:49 UTC comment](https://github.com/spita90/konami-viper-recomp/pull/5#issuecomment-5954991110)
reported controller shutdown with rumble in GTI Club 2, but not Thrill Drive 2.
The later
[15:45 UTC comment](https://github.com/spita90/konami-viper-recomp/pull/5#issuecomment-5955969994)
identified the third-party desk controller as the problematic one and confirmed
the official Switch Pro.

Local history also retains the investigation: `23ab69d` tried sustained Apple
effects; `542f15f` restored standard finite players and engine recovery;
`a17102e` isolated one-second test pulses; `5767353` made Apple opt-in and SDL
the default; `6d66cf2` added the standalone SDL comparison.

These findings establish the historical baseline: **the fading repro needs the
Apple route on the third-party Bluetooth controller; SDL silence was already
known.** The CLI Apple repro's current silence is a difference to investigate,
not evidence that the historical fading observation was absent.

## Current standalone tests — 2026-10-04

The user's reported issue is fading rumble with a third-party controller. The
first standalone repro used SDL, the game's default transport. The user reported
feeling no rumble with that repro. A direct Apple GameController/CoreHaptics test
was then added to compare the game's experimental Apple route.

Neither successful API calls nor the reported haptics capability establish
physical vibration. After reconnection, the original three-pulse Apple test
reproduced **three slight, fading rumbles** on the IINE. An official controller
felt **very strong** at the same 50% requested intensity. Earlier runs were
silent: the user tentatively reported no vibration after the first Apple run
and requested another run. During the later repeat while holding the IINE
controller, the user confirmed **no vibration**.

## Host and device

- Apple Silicon arm64; macOS 15.8.1, build 24H32.
- SDL 2.32.74 (Homebrew sdl2-compat), bundled SDL3 3.4.18.
- Connected device presents as Nintendo Switch Pro Controller over Bluetooth:
  VID 057e, PID 2009; GUID 0500d71f7e0500000920000001006803.
- Apple vendor name: Pro Controller in the initial run; **IINE Pro Controller**
  in the later run while the user held the controller. Product category:
  Switch Pro Controller in both. The exact IINE model remains unknown.
- Apple's supported haptic localities: Default, Left Handle, Right Handle,
  Handles, All.
- Actual third-party make/model, firmware, battery charge, and selected controller
  mode have not been supplied. SDL reports firmware=0 (not a known version).

## Observed API behavior

| Route | Discovery | Commands | Physical observation |
| --- | --- | --- | --- |
| SDL default | One recognized controller, rumble advertised | Ten equal 1s effects at low=high=32768; all accepted | User reported no rumble from initial repro; observation during agent-run test not separately confirmed |
| SDL HIDAPI requested | One recognized controller, same device path | Discovery only for this explicit route | Not tested separately |
| SDL native requested (HIDAPI disabled) | No recognized controller | No effects sent | Not applicable |
| Direct Apple GameController/CoreHaptics | One controller, haptics available | Ten equal 1s continuous events, intensity=0.5, sharpness=0.5; all accepted | User tentatively reported none; repeat requested |

Apple logged engine stopped reason=5 after explicit test cleanup. No API errors
or disconnections were reported in the first Apple pulse run.

## Repeat while the user held the controller

The later Apple pulse run identified IINE Pro Controller. It accepted pulses 1–5,
then the engine stopped with reason=6 and the app detected that the controller
had disconnected. Cleanup returned CoreHaptics error -4805. The process exited
with status 1. Pulses 6–10 were not sent. The user confirmed **no physical
vibration** while holding the controller during this run. These results do not
confirm fading or establish what caused the silence or disconnect.

Full output is preserved as `apple-pulses-held.txt` in the built package. This
disconnect is a separate finding from the previously reported silent SDL output.

The recovered PR establishes that Apple output over Bluetooth previously
produced fading on the third-party controller. The next comparison needs to
match the game environment that produced that result; accepted CLI commands
alone are not a working physical-rumble baseline. The exact model and previous
selected strength still need to be established.

The coding agent's sandbox hides the connected controller from discovery.
Running the diagnostics outside that sandbox detects it. This explains the
earlier agent discovery result of zero controllers; it does not explain the
user's physical observation when running the app in Terminal.

## Reproduce and collect evidence

### Original three-pulse test recovered and rerun

The original October 2 executable `/private/tmp/viper-isolated-haptics` and its
source `/private/tmp/viper-isolated-haptics.m` were still present. On October 4
at 17:44 local time it was rerun outside the agent sandbox. It discovered one
controller and submitted all three native Apple pulses successfully (`1`).
The source calls `controller_haptics_rumble(0.5f, 1.0)`, pumps the main run loop
for 1.2 seconds, stops, and waits another second between pulses. The user
confirmed **no physical vibration** during this rerun. Full output is saved as
`apple-original-three-pulses.txt` in the built package.

### After a fresh Bluetooth connection

The user powered off/reconnected the controller and reported physical rumble
on connection. This confirms the motors operated during connection; it does
not establish whether that effect was initiated by macOS or controller firmware.
At 17:57 local time the original three-pulse Apple test was rerun immediately
after the user reported being connected. One controller was discovered, and all
three equal 50% commands returned `1` (submitted). Physical observation for
that first post-reconnect run was not supplied. Output is saved as
`apple-three-pulses-after-reconnect.txt` in the built package.

At 18:00 local time the same original test was repeated. All three commands
returned `1`, and the user confirmed **three slight rumbles, fading**, with
the first pulse already weak. This reproduces the reported fading with three
equal 50% native Apple effects. Output is saved as
`apple-three-pulses-after-reconnect-repeat.txt`.

### Official controller comparison

The user connected an official controller as well; Apple's device names were
Real Pro Controller and Pro Controller. The harness was updated to select an
exact unique vendor name, keeping both devices connected. Three equal 50%
pulses were explicitly targeted to Real Pro Controller and all were accepted.
The user reported that these pulses were **very strong**. This contrasts with
the third-party controller's weak, fading response at the same requested
intensity. It does not yet isolate firmware, transport or hardware as the cause.
Output: `apple-official-three-pulses.txt`.

The updated harness accepts `--controller` and `--strength` greater than 0
through 1 (default 0.5). Its Apple pulse phase now matches the original timing:
three 1s events, each followed by a 1.2s run-loop wait, explicit stop, and 1s gap.
The 100% third-party comparison was then run with the user's go-ahead, explicitly
targeting Pro Controller. All three 1s events at intensity=1.0, sharpness=0.5
were accepted; the process exited successfully. The user reported the pulses
felt similar, perhaps not fading, but **still very weak**. Fading at 100% is
therefore not confirmed. Output: `apple-iine-three-pulses-full-strength.txt`.

| Controller | Apple requested intensity | User observation |
| --- | --- | --- |
| IINE third-party | 0.5 | Three slight rumbles, fading; first already weak |
| Official Switch Pro | 0.5 | Very strong pulses |
| IINE third-party | 1.0 | Still very weak; pulses similar, fading uncertain |

The observation to report is both the confirmed 50% fading case and the weak
100% response. The official and third-party connection conditions have not been
fully isolated, so these results do not establish the underlying cause.

The user additionally reported hearing the IINE motor moving very slowly. This
is an audible/physical impression, not a measured motor-speed value.

### Simultaneous comparison default

At the user's request, default behavior now targets all discovered controllers
with haptics together. Each has its own engine/player. All players are prepared
before scheduling a shared host-time pulse start, translated to each engine's
timebase. `--controller` still selects one unique exact device name.

The default sequence is three 1s pulses at 50%, three 1s pulses at 100%, then
three pairs of 150ms pulses at 100%. The short-pulse pattern uses 150ms gaps
within pairs and 800ms between pairs. The user can hold both controllers for
direct comparison. Commands and errors identify each controller. This new
simultaneous run is saved as `apple-simultaneous-comparison.txt` in the vendor
package. Both Pro Controller and Real Pro Controller accepted all 12 effects:
three at 50%, three at 100%, and six short effects in three pairs. The log
records a shared scheduled start for each pair of controller submissions. The
process exited successfully, with no reported API failure or disconnect.

The user confirmed that **the IINE faded very quickly during the 50% phase**.
By the time the test reached the 100% phase, the user could feel **no vibration**
from it. Thus accepted 100% commands did not restore perceptible output after
the initial phase. This differs from the earlier isolated 100% run, which was
very weak but perceptible. The official controller's physical behavior during
this particular simultaneous run and the IINE's short-pulse phase have not been
separately reported. The earlier official-only 50% run was very strong.

Quit other controller apps and run from the extracted package directory:

```sh
./apple-rumble-repro --mode pulses 2>&1 | tee apple-pulses.txt
./apple-rumble-repro --mode hold 2>&1 | tee apple-hold.txt
./apple-rumble-repro --mode refresh 2>&1 | tee apple-refresh.txt
./rumble-repro --mode pulses 2>&1 | tee sdl-pulses.txt
```

Record whether each phase is silent, steady, weaker within one effect, or weaker
on later pulses. If reconnecting restores vibration, record that and use the
same starting state for each comparison. The source requests constant strengths;
the hold and refresh phases have not yet been physically assessed in this session.

The Apple repeat run is saved in `apple-pulses-repeat.txt` in the built package.
That run finished all ten pulses without a reported error. Its result and the
user's physical observation should be kept together.
