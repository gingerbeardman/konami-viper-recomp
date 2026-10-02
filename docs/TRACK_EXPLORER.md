# Experimental track explorer

Private experiment on `experiment/track-explorer`, for **GTI Club 2 JAB (Japanese)** in enhanced mode. This is not an upstream PR. Other game profiles have no explorer hooks.

Build with `python3 recomp/recomp.py gticlub2` then `make GAME=gticlub2 -j8`.
Start a game and choose a course, then press **F6** to toggle the drone tour. You can also press F6 before the race starts to arm it.

- **Accelerator / brake** (A / B, triggers, or Up / Down): hold to increase/decrease cruise speed; release to keep it steady.
- **Left / right shoulder** (L / R, or Q / E): lower/raise the drone; release to hold height. Steering and gyro do not change drone height.
- **Steering stick / gyro** (or Left / Right): look up to 90 degrees left/right of the route heading. Centre the input to look forward again.
- **Gyro tilt up / down**: look up to 45 degrees above/below the normal viewing angle. L3 recentres both gyro axes; R3 enables/disables gyro as usual.
- **[ / ]**: decrease/increase speed, from stationary to 160 metres/second (default 40).
- **− / =**: lower/raise the drone, 3–60 metres above the road (default 12).
- **F6** again: return to the normal camera.
- **Escape / Home**: the existing pause menu, including return to the main menu.

The overlay displays live cruise speed in km/h and selected height above the road in metres.

The drone follows the selected course's linked road centreline, looks ahead through turns, follows terrain elevation, and loops continuously. Race-state transitions are held during exploration; the race clock's origin advances to exclude time spent exploring. Returning to the main menu cancels an active tour. Settings are not persisted.

This is a camera experiment, not an editor: the game continues simulating traffic and cars, its normal HUD remains visible, and the drone does not collide with buildings, bridges or tunnels. Routes with overhead scenery can clip at high altitudes. Only the JAB Town route has been visually checked; the other selected-course routes use the same path reader but need playtesting.

## Implementation notes

`runtime/track_explorer.c` contains the experiment. Its two hooks are installed only by `games/gticlub2/game.json`:

- `0x8ec84`, before the race renderer applies the camera: read route root `0x8c0188`, follow 0x44-byte linked nodes and their 32-byte path points, and write the camera at `0x8c1cf8`.
- `0xb403c`, after world rendering: override the local race-state register with an out-of-range state, using the game's existing no-transition exit without changing its stored state.

Ground height uses the game's tile lookup (`0x5576c`) and collision-plane interpolation (`0x566f0`) on a copied PPC register context. The temporary guest stack is saved and restored, and the live CPU budget is untouched. No ROM-derived data is committed.

For headless checks, `RT_TRACK_EXPLORER=1` arms the tour. `RT_ENH_MENU` accepts `seconds:drone` to toggle it. Use disposable settings/NVRAM copies. A full lap logs `completed full course loop`; normal frame dumps include the control hint.

Validation: rebuilt JAB, ran a 210-second headless session, completed the 4,992.1 m Town loop, toggled the drone off at 195 seconds, and inspected the restored car camera and running race timer. Inspected intermediate terrain-following frames as well. `git diff --check` passed. The build retains the pre-existing `cpu.c` unused `nmatch` warning.
