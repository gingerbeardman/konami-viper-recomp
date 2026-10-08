# Mission mode

Available in **GTI Club 2 JAB enhanced mode**. Choose **MISSIONS** in the main menu
or **Pause → Missions**. Course, car, transmission and traffic mode come from each CSV row.
Car 1 is native vehicle ID 0. The original choosers, camera sweep, and starting
countdowns are skipped.

`mode=time_attack` runs without native traffic; `mode=traffic` uses the native
race with traffic. Missing `mode` defaults to time attack for compatibility.
The MODE column shows the yellow Time Attack badge, or its grey version for traffic. Existing missions
explicitly use `time_attack` in the CSV.

Choose CLEAR MISSION RECORDS below the mission list to remove all saved times
and medals. Confirmation defaults to CANCEL. This clears mission progress only.

The selector shows ten missions per page, numbered in CSV row order. Reordering
rows updates those numbers automatically; completion records follow mission names.
Names align to the left, with native car/tuned and transmission icons. An earned
medal appears beside the name, or a green completion badge for untimed missions.
Timed missions show their best time in a dedicated BEST column. Silver times within 5% of gold get a
green/yellow beginner mark beside their time on both selection and results screens.
The optional `type` CSV column supplies a short menu label such as DRIVE,
COLLECT, DESTROY, DODGE, STUNT or SPEED. JUMP and TURN recipes display as STUNT.
For indestructible items such as kiosks, use `type=COLLECT` with
`target_event=touch`: each distinct object counts on player contact and remains visible.

| Mission | Objective | Time limit | Collision limit |
| --- | --- | --- | --- |
| Start → CP1 | Reach the first checkpoint from the native starting grid | Gold ≤17 s; silver ≤19 s; bronze ≤21 s | Unlimited |
| CP4 → CP5 | Complete the section without contact | None | 0 |
| CP6 → CP7 | Reach the next checkpoint | Gold ≤17 s; silver ≤19 s; bronze ≤21 s | Unlimited |
| CP9 → CP10 | Drive behind both bus stops, then reach the exit | None | Unlimited |

The first sprint retains its established first checkpoint after the starting grid.
Section missions use native checkpoint numbering, including the unused route
marker slot: CP4→CP5 uses native markers 5→6, CP6→CP7 uses 7→8, and CP9→CP10
uses 10→11. CP10 is the exit of the bus-stop street, before the lap finish.

Missions 2–4 start 75 metres before their entry checkpoint at 30 m/s (108 km/h).
The approach drives itself with full throttle and steering through native physics.
Crossing the entry gate hands control to the player and starts judging; the
approach consumes no mission time or collision allowance. A blocked approach
fails setup after 10 seconds or contact. Spawn headings come from forward route
geometry. Placement seeds the native route node, point, index and distance so
tracking does not attach the car to a nearby parallel lane.

The bus-stop mission requires both wall-side passages, on opposite pavements,
then the street exit. Its gates are 127 and 204 metres into the section, offset
11 metres to opposite sides of the native centreline, with 2.5 m half-width and
queried terrain height. Passing either shelter on the wrong side immediately fails with **MISSED BUS
STOP**. Returning cannot recover the attempt. The main road cannot satisfy these gates. Shelter gap
alignment still needs human driving validation; the camera survey establishes
the two shelters and the earlier section start.

```sh
make -j8 GAME=gticlub2
./gticlub2 --enhanced
```

The game loads the original track and initializes the assigned vehicle through
its normal loader. Native recovery initializes physics at the mission start.
The first mission retains the native grid's progress; other missions start at
checkpoint positions or a configured rolling approach. Checkpoint missions use
the section exit gate, allowing ordinary racing lines and shortcuts; the bus-stop
mission additionally requires its two rear gates. Ordered gates judge the existing track without changing
meshes, scenery, collision or native route data. Untimed missions show elapsed
time. The bus-stop HUD identifies the next stop and then the section exit.

A contact is the native wall or object/vehicle flag, including light scrapes.
Continuous contact counts once; it requires 200 ms continuously clear before
another contact can count. Exceeding the allowance fails immediately, including
when a contact coincides with the finish crossing. Native recovery jumps fail
instead of sweeping through gates. Exact-deadline finishes succeed; late ones fail.

Results freeze play. **Enter / Start** retries through a complete native
return-to-attract teardown followed by the native loader. Traffic and race timing
are initialized again; mission retries never restore partial race control records.
**Back / Esc** opens mission selection. Selecting another mission runs native
return-to-attract initialization behind the loading screen and then automatically
starts its assignment. Best times are saved alongside settings in
`<settings-file>.missions`, keyed by route geometry, vehicle, and recipe. Changing
a section or its rules cannot reuse a prototype recipe's best time.

## Native state and audio

Missions retain the native running state, including its audio, timer and race
bookkeeping. The previous out-of-range race-state override bypassed that work.
After placing a car, the adapter synchronizes the native lap/checkpoint baseline
and commentary position to prevent replaying progression from before the spawn.
The native remaining-time counter is held and time bonuses are suppressed, while
the mission judge owns its own time limit and result.

The camera sweep is completed at its final interpolation frame, after native
loader/player readiness. Native camera initialization still executes. Countdown
skipping invokes the native clock/control initialization and finishes the start
transition. Normal arcade play does not use these mission overrides.

## Implementation and validation

- `runtime/mission_rules.h`: ordered directional swept gates, bounds, optional
  deadlines, contact debounce and recovery checks.
- `runtime/mission_mode.h`: definitions, route checkpoints, native loader,
  placement, terrain queries, progress records and guest adapter.
- `runtime/enhanced.c`: selector, HUD, pause, results and input ownership.
- `games/gticlub2/game.json`: enables the JAB adapter. Other profiles do not
  expose missions.

```sh
tests/run-mission-tests.sh
tests/run-controller-tests.sh
tests/run-display-tests.sh
```

Tests cover gate order/direction, swept crossings, bounds, deadline equality,
untimed sections, contact priority/debounce, recovery, stack/register preservation,
fixed assignments, chooser/sweep skipping, retained running state, checkpoint
sections, bus-stop bypass rejection, required stops plus exit, persistence,
selection and retries. Unsupported profiles are syntax checked.

Real-game runs check native-grid and checkpoint placement, both timed failures,
untimed sections, same-mission retries, and startup state transitions. Native
sound-command tracing checks startup and post-placement behaviour. Audible playback
and mission difficulty still need playtesting.

## Mission direction

`direction` accepts `auto` (the default), `forward`, or `reverse`. Auto and
forward follow the native linked route at the selected checkpoint, including
Town and Mountain return legs after a loopback. Reverse follows that same route
backwards; `cp1=5,cp2=4,direction=reverse` is supported.

Direction controls checkpoint gate normals, entry heading, rolling spawn side
and automatic driving look-ahead. Rolling tracking is restricted to its approach
section so a nearby opposite-flow road cannot take control. For a custom coordinate
start, `start_heading` remains an absolute heading in degrees as authored.
This per-mission override is separate from global Reverse and the Mirror game-menu option.

## Track diagnostics

Time Trial's pause menu includes **TRACK DEBUG** (also **F10**). It shows player
coordinates, the native route marker, the nearest scenery record and its position,
and outlines the active mission gates in the world. Yellow identifies the next
gate. Gate coordinates and widths are also listed, so placement can be checked
while driving or using the F6/F7 cameras. General scenery stays in the top info
text. Nearby cars and breakables receive small HUD-font labels in the world,
with their type/model and native item number. Coordinates retain their minus signs.
`RT_TRACK_DEBUG=1` enables it for headless captures.

Mission startup requests the native per-car engine sample bank before skipping
selection. Native car selection sends this request at `8b014`; without it, the
swappable sample buffer retains the attract/menu bank and engine loops read the
wrong samples. Startup also runs the native engine-sequence setup and menu cleanup.
The instant start preserves that engine sequence instead of resetting all sounds.

Mission mode suppresses native checkpoint toasts, time-bonus announcements,
wrong-way speech and banners, and the separate native message-icon renderer
(the red no-entry sign). The host mission HUD and results remain visible.

Bus-stop miss checks are bounded to the local street and terrain height; crossing
the extended plane on a distant section or another road level does not fail.

The first bus gate is 127 metres after entry, on the left wall-side pavement,
beyond the kiosk near X594, Z696;
the second is 204 metres after entry, on the right. These offsets follow the
native camera direction; the previous side signs were reversed.

Gate 2 is aligned near X530, Z657 in the second shelter’s wall-side passage.

### Editing missions in CSV

Edit `missions.csv` in the launch directory and restart the game. Set
`RT_MISSIONS_CSV=/absolute/path/file.csv` to use another file. Invalid rows are
reported in the console and any previously loaded list is retained. There is
no compiled mission list; a missing or invalid CSV at startup leaves no missions.

Columns may be reordered. `name` and `description` are required. `description`
is shown as the briefing in mission selection. Quote descriptions containing commas. `car` is numbered from 1;
`transmission` is `at` or `mt`; `course` is `town`, `coast`, or `mountain`.
`enabled=0` keeps an unfinished survey definition out of mission selection;
blank, `1`, or `true` enables it. Disabled rows may contain incomplete fields.
The optional `notes` column keeps coordinate surveys and tuning notes beside
their mission. Notes do not appear in the game. The current coast and mountain
batch includes disabled survey rows; these are not implemented missions yet.
`cp1=0` means the starting grid. `cp2` is the finish checkpoint.
`collisions` is an allowed contact count, or blank/`unlimited`.
`gold`, `silver`, and `bronze` are seconds; leave all three blank for no time
limit. Bronze also sets the deadline. `rolling` is the approach distance in
metres; zero or blank disables auto drive. `rolling_speed` is its initial speed
in km/h and must be positive when `rolling` is enabled. The current missions
explicitly specify 75 metres and 108 km/h. All mission definitions live in CSV.

Optional `gatex`, `gatez`, and `gatew` add ordered mandatory gates before the
finish checkpoint. Use matching semicolon-separated lists for multiple gates.
Widths are total metres, or presets: `kerb`/`curb` = 5 m, `lane` = 9 m,
`road`/`full road width` = 36 m. These are fixed tolerances, not measurements
of the local road. Gate direction follows the nearest route segment and
height comes from the terrain. Track Debug shows the resulting gates.

The native accepted checkpoint event also completes the finish gate after all
mandatory scenery gates are passed. Collision and time limits still apply on
that update. This avoids relying solely on a sampled terrain plane at the exit.

Mission HUD uses the native course/time text renderer, shared with unlimited
laps, instead of a scaled menu font. It uses three green-label/white-value pairs in the Lap/Best/Time
positions: MISSION/name, WAYPOINTS/completed count, and TIME/elapsed mission
clock. The standard Time Trial clock and the duplicate top banner are hidden. CSV gate direction is constrained
to the mission section, so nearby opposite-flow carriageways cannot own it.

Collision limits count road/car contacts, solid scenery impacts, and breaks of
destructible scenery such as cloister pillars. Breaking an object counts even
when the native response clears its solid-contact flag.

`collision_types` selects which impacts count toward `collisions`: `walls`,
`cars` (including traffic), `scenery` (other solid course objects), `breakables`,
or `all`. Combine classes with semicolons, e.g. `walls;breakables`. Missing or
blank means `all` for compatibility. Existing missions explicitly use `all`.
With `collisions=0,collision_types=walls;breakables`, either a wall scrape or a
pillar impact/break fails, while a vehicle collision does not affect the limit.
Rolling auto drive still treats any impact as a blocked approach.

`collision_delay` sets seconds of visible impact aftermath before a collision
failure freezes play and displays its result. Current rows specify `0.75`.
The failure and mission time are recorded immediately; the delay cannot rescue
the run. Blank or zero displays the result immediately.

Mission launch and reload fast-forward the native selection/loading phase until
the car is placed. Rolling approaches run at normal speed. Reloads retain the
full native teardown so traffic, timing, and sound initialization stay intact.

Optional `endx`, `endz`, and `endw` replace the checkpoint finish with a custom
finish gate. Supply all three together; widths use the same metre/preset syntax
as waypoint gates. `cp2` still bounds the route section for direction lookup,
but passing it cannot complete a custom-finish mission. Cloister Fork finishes
at X 212.6, Z 326.8 with full road width and 8/10/12-second medal thresholds.

### Destroying scenery

The CSV also includes CHICANE (CP4 to the tunnel entrance), SWEET TOOTH
(7 kiosks), CONE BUT NOT FORGOTTEN (35 cones), PEOPLE PLEASER (20 distinct
pedestrians dodging), SUPERMARKET SWEEP (5 tunnel stalls, then CP5), TAKE A SEAT
(55 chairs), AN APPLE A DAY (3 fruit stands), PHONE HOME (6 phone boxes),
GARBAGE COLLECTOR (7 bins), MANY RIVERS TO CROSS (river bank to opposite steps),
SPEED DEMON (170 km/h between CP9 and CP10), and FLOWER POWER (34 flower pots).

`target_models` selects native scenery classes, separated by semicolons:
`hitA;hitB` (mixed solid scenery, including kiosks), `pylonA` (cones), `walkerA` (pedestrians), `dogA` (dogs), `chairA;chairB`,
`boxA;boxB;boxC;boxD;boxE` (fruit crates), `phoneA`, `trashA`, and
`plantA;plantB`. Blank/`all` in `target_count` means all matching instances;
a number sets a smaller goal. `target_label` supplies the HUD counter title.
`CAT PERSON` selects `dogA`, requires one player-owned scare event,
and finishes after the CSV completion delay. It starts on the Mountain return
road before the dogs; pedestrian reactions cannot complete it.
The native reaction hook follows the committed evasive animation, before the
extra-time award that excludes dogs. A native drive from the CSV spawn completed
the mission after a player-owned dog scare at 3.031 seconds, with no contacts.
Each instance counts once, with player-owned impacts required. Pedestrians
and dogs count when their native evasive animation starts. A dog target
does not count pedestrian reactions. `target_group_radius`
groups adjacent instances into a single target, so crates in one fruit stand
count as one stand. Automatic completion is the default; `target_finish=finish`
requires the total followed by the mission finish gate. Collection runs allow
additional laps while targets remain.

Destroyed model targets stay removed for the attempt: native scenery streaming
cannot recreate an already-destroyed chair, bench, pillar or breakable prop when returning to
its area. Unhit objects stream normally, pedestrians remain after dodging, and
retrying clears the destruction state and restores targets. Touch targets such as
kiosks stay visible and count once per attempt.

Fixed course scenery can use matching `targetx`/`targetz` coordinate lists and
`targetradius` instead. These targets require a scenery/wall contact within
the matching radius; simply driving near a stall does not count.

`speed` specifies the required peak km/h before the finish. `tuned=1` selects
the native tuned version of the assigned car. `startx`, `startz`, and
`start_heading` together override the spawn; heading is degrees, with zero
pointing along positive Z. Such a start uses `rolling_speed` as initial km/h
and requires `rolling=0`. `jump_height` specifies minimum airborne height
above native terrain before crossing the finish. The river mission uses the
tuned car and a custom finish on the opposite steps.

`landingx`, `landingz`, `landing_radius` and `landing_height` define a jump's
landing zone in metres. `landingy` specifies its height, or blank queries native
terrain there. A landing mission requires `jump_height` and a grounded landing
inside that zone, then finishes automatically.
When a landing mission also defines `gatex`/`gatez` approach waypoints, all of
those gates must be crossed before the landing counts. The last approach gate
does not complete or fail the mission while the car is still airborne.

`turn_degrees`, `turnx`, `turnz`, and `turn_radius` define a handbrake turn.
The car must enter at speed, rotate while the handbrake is held, then release
it within the angle tolerance without contact. `two_wheel_seconds` requires
that accumulated duration on one left/right wheel pair before the finish.
`roll_degrees=360` requires a full barrel roll and an upright landing before the
mission's finish; an ordinary jump or two-wheel tilt cannot satisfy it. Rotation
is measured from the wheel frame in either direction, independently of Euler
yaw flips. Side or roof contact does not prematurely reset the attempt. The
native HUD shows accumulated roll degrees, then COMPLETE after landing.
`gate_order=any` accepts custom waypoint gates in either direction and any
order, counts each once, and finishes at the last required gate. Blank or
`ordered` keeps the normal sequence and finish checkpoint.
`wallride_distance` requires that many metres tilted against a wall in the
corridor between one custom entry gate and the custom end gate. Ordinary
road driving and airborne rolls do not contribute to this distance.

`target_vehicles=all` selects active traffic cars. Native vehicle type IDs
(`0` through `15`, separated by semicolons) select particular models.
These missions require `mode=traffic`. `target_event=contact` counts a
player-owned collision; `target_event=destroy` requires the native crash
transition. NPC collisions and repeated impacts on the same car do not count.
`target_count` sets the total; the Thrill Drive missions currently require ten.

`target_event=visit` uses `targetx` and `targetz` lists, `targetradius`, and a
`target_label` to count visits to authored surface regions, such as grass
patches. Each region counts once. The car must be on the ground inside the
region; jumping over it does not count. This event requires coordinate targets
and cannot be combined with `target_models` or `target_vehicles`.

Track debug also reports the nearest active car's native type (`CAR`) and
asset model number (`MODEL`), its distance, and X/Z position. This works in
traffic missions as well as Time Attack and helps identify vehicle selectors.
The readout remains visible while paused. `H` is heading in degrees from -180
to 180, using the CSV `start_heading` convention. In track debug, **Cmd+C** copies
`x,z`; **Option+Cmd+C** copies heading alone, with one decimal place.

**Pause → Explore in Time Attack** cancels mission judging and continues at the
current position with unlimited laps and track debug enabled. It is also
available from mission results by pressing **Esc** to open the pause menu.

`CLOISTER CRASH` starts at CP2 and ends at the same zebra-crossing gate as
CLOISTER FORK. It has no intermediate gates, no collision limit and no time
limit. Destroying the last of the 14 cloister pillars completes the mission
immediately, without needing to cross the finish. This applies to all missions
with destruction targets. Collision and time failures still take priority.

Optional CSV columns `breakx` and `breakz` list the target centres separated by
semicolons. `breaktype` selects the native scenery kind (2 for these pillars),
`breakradius` is the matching radius in metres (1.5 here), and `breaklabel` is
the HUD counter title. All coordinates, target count, label and mission rules
are authored in the CSV. Targets count once when the native game commits their
broken state; ordinary impacts and unrelated scenery do not count. Target
matching circles must not overlap. Missing targets fail at the finish gate.

Collection missions stop the clock at the last collected item and show results
0.75 seconds later, letting the impact play out. `completion_delay` sets this delay
in seconds. Missions requiring an end gate still finish at that gate.
The nearest remaining item distance appears when `hint_remaining` or fewer
items remain (default 3), or after `hint_seconds` without a collection (default 30).
Distances are straight-line metres to the remaining target locations.

On mission select, Left/Right adjusts gold by one second and immediately saves
that mission's medal fields to the active CSV. Untimed missions start at 10 seconds.
Silver is always gold +2 seconds, bronze gold +4 seconds, including when loading
CSV definitions. Updates match unique mission names, preserving row order and
other fields; reordering is supported. Keep names unique and stable to retain records.
A failed write shows COULD NOT SAVE CSV and leaves the current times unchanged.

## Course transformation work remaining

Native RAM audits cover Town's 22, Coast's 14 and Mountain's 29 main/shortcut
sections. Reversal and reversal back retain exact positions, graph links and
checkpoint flags, with cumulative progress within 0.01 metres. This validates
the graph operation on all three courses; it does not replace full-race testing.

Mirror is implemented as a persistent Game menu toggle. It reflects perspective
projection and player steering while keeping orthographic HUD text readable.
Native rendered validation caught reversed backface winding; reflecting the FIFO
triangle culling sign restores the road and car surfaces. Debug labels follow the
reflection; mission autopilot retains native steering and gate coordinates.
The Town mission capture verifies rendering, while signs and the other two
courses still need visual validation.

Brake confirmation is connected to the native course chooser, while Accelerator
retains normal direction. Reverse swaps every incoming/outgoing route branch,
reverses its point geometry and checkpoint sequence, and places the player before
the start line facing the reversed route. Native Town validation crossed the
start line with lap 1 and checkpoint 1, without the initial-origin wrap bug.
Course reload restores exact saved route bytes before reinitialization.

Reverse is not fully validated yet: complete laps, opponents/traffic, directional
signs and the other courses remain to be checked. The startup/first checkpoint
check is not proof of a complete reversed race.
The two independent choices produce Normal, Mirror Normal, Reverse, and Mirror
Reverse. Restart must retain both choices, and returning to course select must
let the confirming pedal choose direction again. Their implementation must
transform the driving route and its directional visuals together:

- Reverse: start heading, checkpoint traversal and lap progression, route look-ahead,
  traffic direction, chevrons and checkpoint sign facing.
- Mirror: reflected world/camera orientation, steering, checkpoints and mission gates,
  directional signs and debug projection; HUD text must remain readable.
- Audit other course arrows and direction-dependent markers on all three courses.

The native resource registry identifies `CAB_SMB_arrowA`, `CAB_SMB_arrowB` and
`CAB_SMB_arrowC` as resource codes `0x1011f`, `0x10120` and `0x10121`.
Reverse rotates these meshes by 180 degrees in fresh render-queue placement
matrices, including the generated-arrow path as well as scenery submissions.
The native Town capture verifies the changed X/Z basis with unchanged position
and vertical scale, and still reaches lap 1/checkpoint 1 after the start line.
The registry also exposes `CB_SMB_CP01B` through `CB_SMB_CP07B` as
`0x2000a` through `0x20010`, and `CC_SMB_CPOLE01a` as `0x300aa`.
The same queue transformation covers these checkpoint assets, but their course
placement and orientation still need an in-game check. The Town
RAM capture contains 59 references to the arrow A descriptor in the transient
render records; these include camera-transformed matrices, so changing those
records is not a persistent scene transformation.
Checkpoint identification alone does not prove their final facing in-game.

## Parked-car target work remaining

The parser accepts `parked_models` as `all`, semicolon-separated native TCAR model
numbers, or `nearest`. An explicit model list can also include one
`targetx`/`targetz` anchor: it selects the nearest matching model within
`targetradius`, ignoring closer cars of another model. Odd One Out uses model 2
(the yellow car) at X 613.5, Z 263.3. For `nearest`, these same fields select
one placement near the supplied anchor. Selection is distinct from judging an impact.
The catalog is read after native course initialization: placements and models can
vary between initializations, so a previously captured slot cannot be assumed to
identify the same car in a later run.

Exact player impact attribution is verified in native captures: a side impact
counts its parked-car collider, while touching another car does not satisfy a
selected target. ODD ONE OUT uses the harbour anchor. LOVE BUG selects native
TCAR model 11 (Beetle), and MAN WITH A VAN selects model 12 (white van). Both
filters were visually checked and completed a native player-impact test.

Set `RT_MISSION_STUNT_LOG=1` for sampled jump position, terrain height, speed and landing distance in native diagnostics.

## Native collection catalog audit

Coast contains 59 small-post targets, 19 selected road-sign targets, 17 parasols,
70 flower pots across both variants, 15 garbage cans, 48 chairs, six phone boxes,
ten tables and six benches. Its 20 fruit crates form five stand groups. The
empty cone repeat is omitted. These are target-discovery checks, not claims of
completed collection runs.

Fresh traffic-mode captures find 50 eligible cars for THRILL DRIVE on Town,
38 for THRILL DRIVE II on Coast and 21 for THRILL DRIVE III on Mountain. Each
CSV recipe requires ten distinct player-owned destruction events. Time Attack
captures have a different vehicle population and cannot validate these recipes.

When a scenery class includes unwanted objects, append a native placement index
to select individual instances: `target_models=hitA:2;hitB:4`. An unqualified
name, such as `hitA`, still selects every placement in that class. Indices belong
to their own class and are independent of the mission order.

### Permanent mission IDs

Every mission in `missions.csv` has a unique positive integer `id`. Keep that ID
when renaming, reordering or tuning the mission. Give a new mission a new unused
ID; do not recycle IDs from deleted missions. IDs are separate from the menu row
numbers. Duplicate or missing IDs reject a reload and retain the previous data.

Progress and selection follow the ID. Existing name-based saves migrate using
the current name or the semicolon-separated `legacy_names` field. Keep those
legacy names until older saves have migrated. CSV files without an `id` column
remain compatible with the older name-based format.

### Player-controlled stunt approaches

`lead_in` is the distance in metres along the road before the authored custom
`startx,startz` challenge anchor where player control and timing begin. The
anchor and objective gates stay in place. `rolling` is the additional approach
before that handover; it is the only part driven automatically. `lead_in`
requires a custom start, ranges from 0 to 500, and defaults to 0 for existing
missions. The authored heading selects the road's travel direction; the
approach heading is calculated from the road ahead.

Mission 23 uses an 80m player-controlled lead-in, preceded by 30m of auto-drive
at 60km/h. Its cliff waypoint and bridge landing remain player objectives.

### Gate editor

With track debug enabled, Select opens the editor for the selected mission.
It opens a free-roam camera: left stick moves forward/back and turns, right
stick looks, and triggers raise/lower the camera. Click the left stick to
switch between camera movement and gate editing. A adds a gate five metres
ahead of the camera and selects it for editing. X deletes, B selects previous,
and Y selects next. In gate editing, left stick moves the gate, right stick
changes width/height, D-pad left/right rotates, triggers tilt from upright
to flat, D-pad up/down changes elevation, and shoulders cycle waypoint,
failure, finish, rolling start and start. Magenta edges are below the sampled road surface; cyan
marks the road intersection.

Home chooses the current checkpoint as the start and selects its fixed-size
marker. D-pad left/right adjusts heading; right-stick up/down adjusts rolling
speed in km/h. Zero disables rolling. Start saves gates and start settings
to the mission's permanent CSV ID; Select discards the draft. Saved gates
remain visible during track debug exploration. Gate geometry uses optional
`gatey`, `gateh`, `gater`, `gatetilt` and `gatetype` semicolon lists. Tilt is
in degrees: zero is upright, 90 is flat.

Gate type `rstart` (rolling start, drawn blue) makes the first gate the timing
line, and the game works out the rest. The car starts up to 10 m behind the gate
on its heading, where the ground stays level, and drives itself straight through
it; timing starts at the gate, which is not a waypoint. The speed comes from the
leg to the next gate. Where the ground drops away between them (a jump), it is
the speed that carries the car 8 m past the far side of the gap, or to the next
gate if that is nearer; this uses the game's gravity of about 23.4 m/s² and a
lip lift of about 0.17 of the car's speed, measured on the Town river jump. On
the road it falls from 140 to 60 km/h as the turn to the next gate grows from
15° to 90°. `rolling_speed` (km/h) overrides the speed. An `rstart` row cannot
also set `startx`/`startz`/`start_heading`, `rolling` or `lead_in`; saving from
the editor clears them. STUNT DRIVER (59) overrides the speed with 200 km/h to
clear its parked car; RETURN TO SENDER (69) uses the computed 148 km/h.

Authored parked-car obstacles use `obstacle_model` (native TCAR model 0–15),
`obstacle_x`, `obstacle_z`, and `obstacle_heading` (degrees). Leave the model blank
for no obstacle. The native terrain supplies its height; native car rendering,
streaming and collision handle the placement. Retries replace the placement.
STUNT DRIVER (permanent ID 59) replaces the disabled reverse-gear draft and uses
this placement above the Town steps. Its initial gates need playtesting.
