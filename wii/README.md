# Wii port

The active target is a straight C Wii/libogc port. An earlier Dreamcast port was
retired and removed from the repository.

The GX renderer now boots through the enhanced menu, car/course selection and
a visibly rendered race in Dolphin2606. Scripted builds have reached the bounded
guest75 driving-state PASS gate. Live Start, sustained accelerator and periodic
steering delivery are verified through Dolphin's WPAD path. Physical Wii Remote
motion and winning course completion remain unverified.
Audio is disabled. Performance remains far below the original game rate; emulator
cycle measurements do not establish physical Wii speed.

Build the current live-input candidate with `sh wii/build_live.sh 1`;
Guarded driving-command batching now defaults on; `WII_LIVE_DRIVING_BULK=0` disables both additional batches. Projection/scissor setup also defaults to once per original triangle during synchronous W-depth splitting; `WII_LIVE_PARENT_PROJECTION=0` disables it. Redundant native texture-attribute setters are also omitted by default; `WII_LIVE_NATIVE_TEXTURE_ATTRS=0` restores them. The combined matched full-render driving measurement is 11.07 modeled game presents/s (38.6% simulation speed), with exact RAM and framebuffer comparisons and live race-to-attract validation.
Its DOL is `build/wii/live-tmu-f1-early-cull-solver-reuse-fifo-bounds-fifo-split-frsqrte-memo-bulk-writer-bulk-ram-native-texture-bind-material-plan-cache-borrow-clip-bulk-publish-texture-layout-cache-combiner-program-cache-split-ram-helpers-splitcopy-dbt-na-pp/viper.dol`.

For the fog-off visual/performance experiment, build with `WII_LIVE_FOG=0 sh wii/build_live.sh 1`. This produces `build/wii/live-tmu-f1-nofog-early-cull-solver-reuse-fifo-bounds-fifo-split-frsqrte-memo-bulk-writer-bulk-ram-native-texture-bind-material-plan-cache-borrow-clip-bulk-publish-texture-layout-cache-combiner-program-cache-split-ram-helpers-splitcopy-dbt-na-pp/viper.dol`. The earlier fog-off comparison improved its matched baseline from6.76 to7.22 game presents/s with exact guest RAM and unchanged depth; fog colour is deliberately omitted. Fog remains enabled by default.
The source-driven full TMU/FBI/predicate compiler and validated optimizations
now default on. `WII_GX_TMU_PIPELINE=0` selects the earlier single-TMU compiler;
set `WII_GX_PIPELINE=0` as well for the old family-based renderer.
Use `sh wii/build_live.sh 2` for alternate-frame rendering. These presets keep
original simulation/input timing and audio disabled. Set
`WII_LIVE_SOLVER_REUSE=0`, `WII_LIVE_FIFO_BOUNDS=0` or
`WII_LIVE_FRSQRTE_MEMO=0` to disable individual optimizations. Each configuration
has its own output suffix. Launch the DOL with `wii/run_dolphin.sh` and the patched
ARM64 Dolphin settings below. Full-TMU live racing, steering delivery and a
sixth-place race-end display are verified; graphics fidelity is not complete.

Current performance comparisons are recorded in `wii/PERFORMANCE.md`.
Resident converted textures remove repeat conversion for the scripted race.
With exact math, the ARM64 patched-Dolphin baseline takes 3.315 modeled
seconds per guest second in the race sample; alternate-frame rendering takes
2.581 (22% less modeled time). Both match the full 16 MiB RAM snapshot.
The newer FIFO/string-word/inline-VRAM/LTO/direct-LFB plus depth-clipping/lookup-object candidate takes 3.012
with full rendering; its matching alternate-frame build takes 2.343. These comparisons are emulator measurements; physical
Wii speed remains unverified. Optional `VIPER_WII_GX_FRAME_DIVISOR=2` changes
rendering only; simulation/input keep their original timing. The display clock
is57.5Hz, but the measured scenes request29 presents per guest second:
these tests compare about29 versus15 rendered frames, not60 versus30. These remain
below real time. `VIPER_WII_IDLE_BATCH` enables the verified worker-loop
optimization; it preserves complete polling rounds and checkpoint timing.
Live tests have driven through a timeout/end transition back to selection;
results-screen fidelity and successful course completion still need validation.

Native Apple Silicon Dolphin2606 has an integer-JIT register-cache assertion
with this workload. An unchanged scripted baseline DOL reaches rendered Town
racing at71km/h/AT3 with the opt-in integer interpreter fallback. A subsequent
full scripted checkpoint passes with byte-identical16MiB RAM to the Intel
baseline. Exact trigger and workaround performance remain unverified.
Use:

```sh
WII_DOLPHIN_ARCH=arm64 WII_DOLPHIN_CPU_CORE=4 WII_DOLPHIN_JIT_INTEGER_OFF=1 WII_DOLPHIN_SPEED=0 bash wii/run_dolphin.sh build/wii/gticlub2-frsqrte-baseline/viper.dol
```

The launcher now defaults to our fixed2606 ARM64 binary and CPU core4 (JITARM64 SC). `DOLPHIN_BIN` and `WII_DOLPHIN_CPU_CORE` remain explicit diagnostic overrides. It does not fall back to the official app if the fixed binary is missing.

A separate local 2606 build with the two-line `addme`/`subfme` register-read
correction now passes both arithmetic probes (20 cases) and the same game
checkpoint with normal integer JIT enabled. Its complete 16 MiB RAM snapshot
matches the Intel baseline byte-for-byte; no assertions were ignored. Run it with:

```sh
DOLPHIN_BIN="$PWD/build/tools/dolphin-2606-fix-build/Binaries/Dolphin.app/Contents/MacOS/Dolphin" WII_DOLPHIN_ARCH=arm64 WII_DOLPHIN_CPU_CORE=4 WII_DOLPHIN_SPEED=0 bash wii/run_dolphin.sh build/wii/gticlub2-frsqrte-baseline/viper.dol
```

The installed official Dolphin is unchanged. Reproducer and patch details are
in `wii/diagnostics/arm_integer/README.txt`.

This changes emulator compilation only. The default launcher behavior is
unchanged. Before restarting, force-stop the owned test with `kill -9 PID`
and verify it exited; the launcher refuses another running Dolphin instance.

The opt-in `VIPER_WII_STRING_WORDS` candidate groups string loads/stores only
inside a contiguous RAM span. Paged-memory, audited, boundary-crossing and
MMIO accesses retain the byte path. `python3 wii/test_string_words.py` compares
actual runtime helper builds across 16,896 load/store cases under ASan/UBSan.
Both its no-GX and rendered game checkpoints match all 16 MiB of baseline
RAM. The earlier optimized live build sustained Town racing; broader live validation remains pending, so it is not enabled by default in the base Makefile.

`bash wii/build.sh` builds a small platform probe with the installed
`devkitpro/devkitppc:latest` Docker image. `build/wii/probe/probe.dol` checks
video/console initialization, a 32 MiB allocation, and the native timebase.
This probe is not the game and does not establish game performance.

The libogc task adapter, resident guest RAM, endian-aware memory access, SD media
paths, headless Voodoo FIFO/retrace device and deterministic input are implemented.
GX rendering, optional fog/W-depth mappings and Wii/GameCube input adapters are
also implemented. Full graphics fidelity and usable game speed remain unfinished.

Platform checks passed in Dolphin 2606: a 32 MiB MEM2 allocation and native
guest BE/peripheral LE/float access. The LWP adapter also passed 2,000 alternating
handoffs with upward/downward rounding checks and 128 KiB stack configuration
(`failures=0`, `steps=1000,1000`). These checks do not prove game boot.

Build the thread probe with `bash wii/build.sh build/wii/probe/thread_probe.dol`.
Launch with `bash wii/run_dolphin.sh build/wii/probe/thread_probe.dol`.
The launcher uses an isolated emulator profile and temporary settings following
middleware's Wii smoke tests. Verify results on the console; emulator process
existence alone does not establish success.

The scripted headless build has passed hardware tests, reached attract mode,
accepted enhanced Start Game and entered car/course selection in Dolphin 2606.
The first exported run continued through guest second 83 with scripted accelerator
input. All 75 integer-second packet/present checkpoints match the Dreamcast
headless baseline (excluding its later final capture at 75.061). Graphics and
audio are disabled.

`build/wii/gticlub2-scripted/boot-01-comparison.json` records the completed first
run comparison. Guest 63–66 contains 156,845 packets and 86 presents, taking
10,344,831 modeled microseconds (8.31 present-equivalent fps). This is an emulator
cycle-model result, not a physical Wii feasibility verdict.

The updated scripted build commits a bounded PASS/FAIL marker at guest second
75, checking race step 11 and post-countdown substate 5. Validate its committed
log using `python3 wii/check_race.py PATH_TO_BOOT_LOG`. This stricter completion
gate passed visually in Dolphin (`boot-02-visual-result.json`). Directly killing
Dolphin did not preserve a complete SD log for that run, so the saved-log
validator correctly fails; do not confuse the visual result with a validated
committed log. The first run's cleanly exported log remains the workload baseline.

`bash wii/build.sh gx-probe` builds an asset-free GX alpha/depth/order probe.
The launcher enables EFB CPU access for its pixel checks using the command-line
namespace `Graphics` (not `GFX`). The probe passed all five color/depth checks in
Dolphin: alpha rejection preserves depth, opaque geometry occludes and later
nearer geometry remains visible. See `build/wii/gx-probe/validation.json`.
Texturing, Voodoo depth mapping and game graphics integration remain pending.

The device now exposes synchronous draw/clear/present callbacks with a read-only
register, palette and VRAM view; selecting no renderer preserves headless mode.
Host sanitizer checks verify delayed FIFO delivery and callback ordering.
`wii/texture.c` converts supported Voodoo formats to GX RGBA8 tiled storage,
preserving channel expansion and alpha. Exhaustive 16-bit-format tests and
wrapping/palette/padding tests pass. The extended textured GX probe passed all
nine color/depth checks in Dolphin (`gx-texture-probe/validation.json`). Build it with
`bash wii/build.sh OUT=build/wii/gx-texture-probe gx-probe`.

Initial game graphics build:
`bash wii/build.sh -j4 game GAME_OUT=build/wii/gticlub2-gx-boot EXTRA_CFLAGS=-DVIPER_WII_GX_RENDER`.
The first GX game launch reaches its initial untextured draw, but currently stops
on a non-endpoint dithered triangle (`cmd=01002d0b`, `cp=15024100`,
`fbz=0002173b`, `alpha=0c045119`). Uniform fractional color clamping is implemented;
RGB565 dithering/blend fidelity is the next concrete renderer case. This does
not establish visible menu or race graphics.

The ARM64 JIT hits a register-cache assertion on this workload; disabling its
register cache also asserts. The launcher therefore defaults to cached
interpreter (core 5). Its timebase uses Dolphin's instruction cycle model, so
timings are correctness diagnostics rather than measured physical Wii speed.
The installed universal Dolphin can also be tested under Rosetta with
`WII_DOLPHIN_ARCH=x86_64 WII_DOLPHIN_CPU_CORE=1 bash wii/run_dolphin.sh PATH_TO_DOL`.
This selects JIT64; initial boot is running, but complete workload correctness
has not yet been verified on that core.

Stage existing assets with `python3 wii/stage_assets.py ASSET_DIRECTORY`
(stop Dolphin first). The correct default sync directory is
`build/wii/dolphin-user/Load/WiiSDSync/viper`.
Build with `bash wii/build.sh -j4 game` and run with
`bash wii/run_dolphin.sh build/wii/gticlub2/viper.dol`.
Set `WII_DOLPHIN_CPU_CORE=0` for the plain interpreter, or `4` to reproduce
the ARM64 JIT failure.

The current graphics spike uses `GAME_OUT=build/wii/gticlub2-gx-attract` and
`EXTRA_CFLAGS='-DVIPER_WII_GX_RENDER -DVIPER_WII_GX_DITHER_APPROX -DVIPER_WII_GX_CHROMA_APPROX'`.
It renders boot/device-check screens and the Konami logo in Dolphin. Display
scaling incorporates the Voodoo scale registers. AI44 fonts and the observed
flat ARGB4444 state are supported. The opt-in flags omit ordered RGB565 dithering
and enable observed bilinear texture states; exact pixel fidelity is unproven.
Perspective race geometry, additional combiners and fog remain incomplete.

Non-scripted builds poll Wii Remote/GameCube input. Plus/Start starts the enhanced
menu, Remote 2/1 are gas/brake, tilt steers, and Minus recenters. The isolated
Dolphin profile maps keyboard E to Plus. The Start overlay requires the original
128 KiB menu_font.a8; stage_assets.py accepts --menu-font or discovers it beside
the assets. The overlay is visually verified over the warning screen and Konami
logo in `build/wii/runs/run.4zIBai/visual-result.txt`. Keyboard Plus delivery has
not yet produced a confirmed Start transition; input tracing is in progress.
The main input loop now uses LWP priority81, above guest fibers at80, and blocks
at VSync between polls. The asset-free scheduling probe passed 120 concurrent
poll/progress intervals in Dolphin (`build/wii/runs/run.aO94UG/visual-result.txt`).
Build it with `bash wii/build.sh OUT=build/wii/input-schedule-probe input-schedule-probe`.

The screen-space projective texture helper passes host numerical/sanitizer
tests, and a native GX STQ probe passes three EFB pixels that distinguish
projective division from affine coordinates. Runtime evidence is recorded in
`build/wii/runs/run.82ZRf3/visual-result.txt`. Build the probe with
`bash wii/build.sh OUT=build/wii/projective-probe projective-probe`.
This proves GX texture division, not Voodoo depth/mip/combiner fidelity.
The limited-interval W-depth probe also passes in Dolphin2606: three depth
samples, alpha-hole preservation, front/rear ordering and ordinary-depth
controls. Evidence: `build/wii/runs/run.LMNb1d/visual-result.txt`. Build with
`bash wii/build.sh OUT=build/wii/wdepth-probe wdepth-probe`.
Its probe lookup covers W[0.5,0.625). Full-range subdivision is integrated behind
VIPER_WII_GX_WDEPTH_APPROX, disabled by default; game validation is pending.

Each launcher invocation snapshots its DOL under build/wii/runs/run.* and saves
its SHA-256, so rebuilding cannot change the running artifact. Restart by killing
only the verified owned Dolphin PID. Live SD extraction is provisional: host
cache data can be stale, and killing Dolphin does not guarantee a complete log.

The post-filter black-chroma probe passes in Dolphin 2606 cached interpreter
(`build/wii/runs/run.n2zpVU/visual-result.txt`). It checks black rejection,
independent vertex alpha, depth holes and rear geometry, plus sampled nonwhite
modulation. Build with `bash wii/build.sh OUT=build/wii/chroma-probe chroma-probe`.
The renderer now uses this two-stage predicate without prefilter alpha masking.
RGB565 colourpath 1d022401 selects vertex alpha; GX/Voodoo interpolation,
filtering and general modulation rounding equivalence remain unproven.

A captured post-Start black screen was an explicit unsupported combiner stop,
not a GPU deadlock: active guest thread was in rt_fatal's VSync loop, with
cmd0140eccb/cp1d022401/fbz2172b/alpha0c045109/tmu10241a87. This tuple is now
accepted; additional geometry guards can still stop it. GX renderer callbacks
also register their executing LWP as the current GX producer, correcting FIFO
overflow ownership across guest fibers. New runs archive matching ELF/map files
beside the DOL for RAM diagnostics. Force-kill the verified owned test PID before
a fresh launch; SIGTERM can leave an instance during the next launch.

Varying RGB/alpha GX interpolation passes the native colour probe within its
explicit two-code RGB tolerance; alpha rejection and rear-depth holes pass.
Evidence: `build/wii/runs/run.MH90KL/visual-result.txt`. Build with
`bash wii/build.sh OUT=build/wii/color-probe color-probe`.
Observed RGB565 draws now submit per-vertex colours. W-depth splitting can
introduce additional colour quantization seams; fixed-point equivalence remains
unproven. The dither approximation counter counts submitted vertices, including
split children, rather than original triangles.

The experimental game build with VIPER_WII_GX_WDEPTH_APPROX passed its prior
varying-depth guard and reached a clamped-S RGB565 state (run.16JkHc). This proves
forward progress, not correct race rendering. The sampler guard now accepts
independent S/T clamp variants of the same observed local-replacement mode.
Fatal stops explicitly select the boot-console framebuffer to show their reason.

Strip culling now receives the device setup vertex count in the renderer view.
It applies the original alternating sign correction when strip mode and pingpong
correction are enabled, preserving packet-continuation parity. The host device
suite passes ASan/UBSan with strip continuation, independent triples, fan anchors
and cull-sign checks. The game previously stopped at this guard in run.Bsya8W;
its replacement run.p6PFiS is under validation.

The strip-enabled game run.p6PFiS passed its former culling guard and stopped at
palette-indexed local texture mode10241587. This state uses the existing format5
palette conversion/cache and the same independent vertex-alpha colourpath.
Its renderer integration is under validation in run.dOzxFw.

ARGB1555 local replacement passes its game guard (run.fAJU7x); TMU1 pass-through
is now accepted for the same observed state. Alpha modulation has a known GX
rounding mismatch: filtered texture alpha25 and vertexalpha128 produce GX13
versus Voodoo12, changing GREATER12 rejection/depth writes. This experimental
path therefore does not claim exact alpha fidelity; vertexalpha255 avoids that
specific modulation mismatch.

The uniform-alpha cutoff probe passes in Dolphin2606
(`build/wii/runs/run.h3rxuC/visual-result.txt`), including both opposite GX/Voodoo
boundary errors and black-key depth rejection. Build with
`bash wii/build.sh OUT=build/wii/alpha-probe alpha-probe`.
The renderer now applies raw-texture-alpha cutoff comparisons for uniform
iterated alpha under the known texture-alpha colourpaths and GREATER testing.
Final blended alpha is still GX-rounded. Depth lookup follows this extra TEV
stage when present. Game validation of the combined path remains pending.

The alpha-only texture draw passes its state guard in run.H3jWTk. Its next state
enables texture alpha-mask bit testing (cmd0100ed8b/fbz217b/alpha0004511f), which
requires a separate mapping; the renderer guards against testing vertex alpha
as a substitute for filtered texture alpha.

The alpha-mask probe retains an aggregate failure at alpha1 because its original
RGB oracle expects1 while both masked and ordinary GX controls produce0.
Independent parity-only and actual LEQUAL/no-depthwrite/SRC_ALPHA tuple checks
pass all256 inputs; eight measured bilinear edges also pass. Evidence:
`build/wii/runs/run.NIvgTZ/visual-result.txt`. Build the probe with
`bash wii/build.sh OUT=build/wii/alpha-mask-probe alpha-mask-probe`.
The renderer's seven-stage parity path is limited to the observed ARGB1555
fbz217b/alpha4511f state. It is exact relative to GX-filtered alpha parity, not a
proof of equivalent Voodoo filtering or blended colour. Its game guard passes
(run.NtgRTZ); the next no-mask fbz2137b/alpha4511f state is under validation.

The Wii state coverage now also uses the captured depth/blend combinations
(from the retired Dreamcast renderer): LESS without depth writes, LEQUAL/ALWAYS alpha, opaque
RGB565, and additive/destination-colour draws. Chroma-disabled draws preserve
their original alpha instead of applying the black-key predicate. The texture
colour equations and GX depth/blend decoding are unchanged; this is coverage
of existing operations, not a claim of pixel equivalence. The default strict
renderer still compiles with `-Werror`.

Run.fOTVmj passes the previous combiner stop and reaches a W-depth split
failure on an ARGB1555 triangle. Exact vertex diagnostics are being collected;
a complete car-selection or race frame remains unverified. Dreamcast's fog
table conversion and TMU equation decoding provide reference material for the
remaining work, while its PVR-specific blending and wheel-alpha limitations
must not be copied as assumptions about GX fidelity.

Run.7vjvmH captured the failing W-depth triangle as raw float bits on screen.
Its positive W values rule out the separate zero-W contract mismatch. The exact
fixture reproduces reversed winding after float-rounded clipping of a subpixel
piece. Clipping now retains double intermediates and preserves original winding
after final float conversion; no area tolerance discards geometry. The captured
case in both winding directions, zero-W saturation cases, and existing band/
attribute/area tests pass with ASan and UBSan. Full-game validation is running in
run.B3OPnY; GX/Voodoo depth boundary equivalence remains unproven.

Run.B3OPnY now visibly renders an advancing course flyover and the ground-level
scene with multiple textured cars. This verifies 3D output beyond the enhanced
menu, but does not yet verify car selection, the playable race or exact alpha/
depth coverage. Restoring winding of a float-rounded sliver does not restore
its exact unrounded coverage. Provisional timing is slow (about30 host seconds
per guest second during the intro); a Dolphin host sample also shows substantial
emulator throttling, so this is not a physical-Wii performance measurement.

The later run.B3OPnY stop is affine A8 overlay state
`cmd10008d0b/cp1c484104/fbz2136b/alpha4511f/tmu10241206`, also captured by
Dreamcast. That mapping is now built and under test in run.VhoBtu. Set
`WII_DOLPHIN_SPEED=0` for unthrottled testing; the launcher defaults to1 and
records the value in each run's `launch-settings.txt`. Unthrottled host speed
must not be reported as expected physical-Wii performance.

The immutable depth-table build visibly renders car selection in run.K8BkDu.
Skipping non-overlapping clipping bands preserves byte-identical output on
20,000 host differential cases and reduces the measured guest20-to21 interval
to9.738444 emulated seconds (3.09x initial graphics baseline). run.xw65ab reaches
fog mode0x41. `wii/fog.h` implements the Voodoo2 table factor and documents the
GX factor128 approximation; host tests pass. `fog-probe` tests the proposed GX
RGB interpolation plus alpha/depth preservation before game integration.
The local and current upstream MAME fog RGB helpers clamp negative differences
before adding the original colour; a conventional fog lerp can darken RGB and
therefore differs from that software path. The intended fog mapping must remain
explicitly approximate until this arithmetic distinction is resolved.

The native fog probe passes in run.IrnGgR (failures0), testing fog factors
0/32/128/256 and unchanged vertex-alpha128 rejection/depth. Build it with
`bash wii/build.sh OUT=build/wii/fog-probe fog-probe`. This validates the proposed
GX colour operation; W-coordinate lookup and game integration remain pending.

Experimental `VIPER_WII_GX_FOG_APPROX` now integrates mode0x41 using W-band
lookups, midpoint dither and an extra RGB stage preserving alpha. It requires
`VIPER_WII_GX_WDEPTH_APPROX`; fog-triggered splitting also works without depth
writes. A1MiB fog-table allocation is inMEM2 (run.drE9xI address0x911b4e60).
Table changes fence queued draws before overwrite; depth lookup follows fog.
run.drE9xI clears the first fog guard and reaches guest60/phase4step10 before
an RGB565 LEQUAL/ordinary-blend guard; that known combination is now accepted.
Fogged game visuals and playable racing remain unverified.

Exact REG2 varying/constant fog operations pass native probe run.3PQNr2.
Opaque ALWAYS-alpha RGB565 chroma rejection is corrected for the precise
fbz2176b/alpha4510f state: binary nonblack alpha plus GREATER0 rejects keys
without losing valid zero-alpha fragments, whose alpha is unobserved because
blend/alpha-plane writes are off. Native probe run.99JJa1 passes this case and
the earlier chroma/alpha/depth controls. Other ALWAYS blended states retain
their separate guarded behavior.

### Captured race untextured colours

The first rendered race run `run.4WMy59` reached guest71 with the player car
at 47 km/h, then the visible fatal console identified varying untextured RGBA:
`cmd=01002d0b cp=15024100 fbz=21379 alpha=0004411f fog=41`.
The Dreamcast renderer recognises this colour path as untextured W geometry.
The Wii renderer now permits varying vertex colours for this captured state
through its existing `GX_PASSCLR` path. Neither chroma rejection nor alpha-plane
masking is enabled in this state, so uniform-colour shortcuts do not apply.
GX byte interpolation remains an approximation to Voodoo fixed-point setup.
The existing asset-free colour probe validates GX interpolation and alpha/depth
rejection separately; it does not prove full Voodoo equivalence. The updated
scripted build is under test in immutable `run.BRt3W0`; END75 remains unverified.

`run.BRt3W0` subsequently displayed `VIPER WII SCRIPTED END PASS` in Dolphin.
This verifies the bounded guest75 driving-state gate after the untextured colour
fix. The raw SD extraction lacks the final END/hash record, so hash verification
is still pending; live input and visual fidelity remain separate checks.

### Live menu and race verification

`run.6o9f4b` clears the attract-mode EQUAL/additive ARGB4444 state from
Dreamcast (`fbz21359 alpha4411f`) and skips triangles whose submitted GX XY
coordinates have exactly zero area after Y-origin conversion. The preceding
diagnostic run `run.BlwMMT` captured identical submitted V1/V2 positions,
explaining the projective matrix failure; nonzero triangle failures still stop.

Live remote input now has positive Start evidence: `INPUT START consumed
connected=1 menu=1`, followed visually by car selection, course selection and
a rendered race. The race continued at least to elapsed27.834s without a
graphics guard. Automated accelerator presses have not produced a confirmed
accelerator edge; continuous gas/steering response remains unverified.
Keyboard bindings in the isolated Dolphin profile are E=Plus/Start, 2=gas,
1=brake and arrows=steering/shift. Physical Wii motion remains untested.


Measured optimization candidates can be built for live input with:

```sh
WII_GX_TMU_PIPELINE=1 sh wii/build_live.sh 1
```

This produces `build/wii/live-tmu-f1-early-cull-solver-reuse-fifo-bounds-fifo-split-frsqrte-memo-bulk-writer-bulk-ram-native-texture-bind-material-plan-cache-borrow-clip-bulk-publish-texture-layout-cache-combiner-program-cache-split-ram-helpers-splitcopy-dbt-na-pp/viper.dol`. The current combined scripted profile passes exact full guest RAM and captured colour/depth comparisons at8.73 modeled presents/s (run.oJFujE). Earlier solver/bounds-only profiling measured6.11 to6.53; those rates describe that older matched baseline. Live gameplay and a sixth-place race-end display are verified in Dolphin; physical Wii performance remains unproven. Solver reuse and FIFO bounds now default to one; set either switch to zero to disable it. Current detailed measurements are in `wii/PERFORMANCE_PLAN.md`; the isolated `-O3` recomp experiment saves only 0.20% scene time while growing code roughly 39%, so ordinary builds retain `-O2`.

Exact reciprocal-sqrt memoization now also defaults on in live builds (`WII_LIVE_FRSQRTE_MEMO=0` disables it). It keys the complete incoming PPC FPSCR and restores the recorded outgoing image, with special inputs and enabled exception traps on the original arithmetic path. Combined native probes, scripted full-RAM/colour/depth checks and live racing pass. Full-TMU combined profiling measures6.53 modeled presents/s versus6.11 baseline (6.40% less scene time).

Guarded hot-writer batching now defaults on (`WII_LIVE_BULK_WRITER=0` disables it). Native full-FPSCR and device-state proofs pass; the fixed scripted scene retains exact full RAM and EFB colour/depth while reducing headless time by9.50% and rendered time by3.71% (7.02 modeled game presents/s). Live tests show the Town result screen and subsequent Coast attract sequence. Final-tail batching is an opt-in native-tested experiment: it preserves full RAM/EFB and saves only0.22% against bulk-only. It remains disabled by default. Parent-plan reuse stays disabled after a3.16% timing regression.

Full TMU texture binding now omits overwritten compatibility setup by default (`WII_LIVE_NATIVE_TEXTURE_BIND=0` restores it). The exact native scene takes12.006663s,7.16 game presents/s, with unchanged full RAM and EFB colour/depth. Live run.rFbFZG shows Town racing and return to attract.


### Prepared material plans

The full-TMU live build now prepares the pure full-TMU plan once after relevant device registers change. It keeps separate positive-alpha slots and checks the packet attribute byte; geometry, textures and GX emission remain dynamic. The equivalent profiling option is `WII_PERF_MATERIAL_PLAN_CACHE=1` with `WII_GX_TMU_PIPELINE=1`.

Actual-device host checks cover100000 masked writes,30000 TMU equation cases, FIFO register writes, resets and held plan slots under ASan/UBSan. Native run.UelBIM reaches END PASS with byte-identical full RAM and EFB colour/depth. The matched scene takes11.079592s versus12.006663s:7.72% less time,7.76 versus7.16 game presents/s. Ordinary-input run.gIpptM visibly races Town with steering and throttle; return to attract was also observed. Live run.gIpptM subsequently returned to Coast attract with the Start overlay without a halt. Prepared plans now default on for full TMU; `WII_LIVE_MATERIAL_PLAN_CACHE=0` disables them, and the legacy backend leaves them off. These are modeled emulator measurements; real Wii performance remains unverified.


The isolated `WII_PERF_TEV_FUSION=1` experiment combines TMU/FBI arithmetic setup into two BP words and retains the SDK shadow. Native SDK and full-game comparisons pass, but the matched scene improves only0.48%. It remains disabled and requires the exact audited SDK object; an unknown object fails the build pending re-audit. The default port uses ordinary libogc setters.

Borrowed depth clipping now defaults on (`WII_LIVE_BORROW_CLIP=0` opts out). It retains all double calculations and eliminates polygon survivor copies. Native 32,000-case full-FPSCR proof, exact scripted RAM/colour/depth output, and ordinary race-to-attract transition checks pass. The matched race section takes 10.927341 s versus 11.079592 s with prepared material plans alone: 7.87 versus 7.76 modeled game presents/s. This remains far below realtime.

Grouped upload publication defaults on when the bulk writer is enabled (`WII_LIVE_BULK_PUBLISH=0` opts out). It combines presence bits and exact page-version increment counts while retaining every serialized VRAM byte. The combined clipping/publication run.OdR8WY passes exact full RAM and colour/depth comparison at 7.96 modeled game presents/s, 2.48% less scene time than prepared material plans alone. Live run.BIxJqS visually shows Town race introduction and subsequent Mountain attract; logs verify race substate transitions through completion and return. A driving screenshot was not captured. Full guest simulation, fog and double arithmetic remain enabled.

Texture layout caching now defaults on for the full TMU renderer; `WII_LIVE_TEXTURE_LAYOUT_CACHE=0` opts out. It retains nine mip descriptors per TMU in224 bytes and invalidates them on changed layout registers, device reset and backend replacement. Actual-device randomized tests pass ASan/UBSan; scripted full RAM and colour/depth captures match exactly. Combined scene run.nIqf5l measures8.08 modeled game presents/s versus7.96 without this cache (1.46% less scene time). Ordinary live run.kACARf completes race substates and returns to the visible START GAME attract display without halting; a driving screenshot was not captured.

The narrower TMU prefix cache is retained as an opt-in alternative: build with `WII_LIVE_COMBINER_PROGRAM_CACHE=0 WII_LIVE_TMU_PREFIX_CACHE=1 sh wii/build_live.sh 1`, or profile with `WII_PERF_TMU_PREFIX_CACHE=1` alongside the full TMU, native texture binding and material plan cache options. It retains the immutable TMU stage program while updating dynamic constants, textures, matrices and the FBI/fog/depth suffix. Host source-reference and actual-device lifetime checks pass; the native SDK shadow comparison passes98,304 cases. Scripted run.sbxynM matches full RAM and EFB exactly at8.21 modeled game presents/s,1.66% less scene time than its descriptor-cache baseline. Ordinary live run.KH1WxX visibly reaches Town driving, the retired result screen and Coast attract. The broader TMU/FBI program cache has since replaced it in default builds.

The opt-in profile experiment `WII_PERF_MERGED_LOOKUP=1` merges four quarter-depth slabs into one exponent slab, using16 tiled1024x8 depth/fog textures. It is disabled in normal builds. Host geometry and actual lookup-helper tests pass ASan/UBSan; the native constant-coordinate sampler probe passes2320 cases with independent texel/Z24 controls. The full game preserves RAM and measures9.42 modeled presents/s, but changes9145 colour pixels and4461 depth pixels at the checkpoint. This does not establish graphics fidelity. Varying-plane interpolation, clipping and rasterization require further investigation before promotion. Run `python3 wii/test_merged_lookup.py`, `python3 wii/test_merged_wdepth.py`, or build the native probe with `sh wii/build.sh merged-lookup-probe OUT=build/wii/merged-lookup-gpu-probe-final -j2`.

Current matched performance comparison: full rendering8.21 game presents/s (28.7% simulation speed); alternate-frame rendering11.66 logical and5.83 displayed presents/s (40.7% simulation speed); no GX rendering20.49 logical presents/s (71.5% simulation speed). All three reach scripted END PASS with identical full guest RAM; both rendered modes have identical final colour/depth captures. These are modeled Wii results in patched Dolphin2606, not physical Wii measurements. Divisor2 halves this scene’s28.67 guest presents/s to14.33 rendered presents/guest second; it is not a literal60/30 FPS toggle. Divisor1 remains the default.

Guarded direct RAM accesses now default on with the bulk writer (`WII_LIVE_BULK_RAM=0` opts out). The audited fast branch removes general memory dispatch for13 proven RAM accesses without changing arithmetic or scheduling. Host/native384case comparisons preserve CPU/FPSCR/device state, including unaligned inputs and special floats; scripted full RAM/EFB match exactly. Full rendering improves8.21 to8.30 modeled game presents/s (1.04% less time); no-graphics simulation improves71.5% to73.4% of realtime. Live logs complete race transitions and return to visibly rendered Mountain attract. The broader combined TMU/FBI cache now defaults on after full-game and live transition validation.

The wider combiner program cache now defaults on for full TMU builds (`WII_LIVE_COMBINER_PROGRAM_CACHE=0` opts out) and retains both TMU and FBI arithmetic stages. Build explicitly with `WII_LIVE_TMU_PREFIX_CACHE=0 WII_LIVE_COMBINER_PROGRAM_CACHE=1 sh wii/build_live.sh 1`. It keeps dynamic KColors and REG1/REG2, uses public setters and invalidates before material-slot rewrites or external stage owners. Host299044 emitter cases and actual-device lifetime tests pass; native74827 completeSDKshadow comparisons pass. Fullgame run.x5VNIt preserves RAM/EFB and measures8.44 modeled game presents/s versus8.30 (1.71% less time). Live run.StWsWn completes race substates and returns to the visible Coast attract display. A driving screenshot was not captured.

Lookup matrix binding reuse is currently an opt-in profile experiment (`WII_PERF_LOOKUP_BIND_REUSE=1` requires `WII_PERF_LOOKUP_REUSE=1`). It suppresses a depth lookup’s redundant MTX1 upload only after the existing per-triangle fog-plane cache proves identical bytes. Native16384case comparisons preserve complete FPSCR. Full-game run.wGxuaZ reaches END PASS with exact full RAM and colour/depth captures. The matched scene takes10,169,230us versus10,184,052us: only0.15% less time,8.46 versus8.44 modeled game presents/s. This negligible gain does not justify promotion; normal builds retain both uploads.

Split RAM helpers now default on (`WII_LIVE_SPLIT_RAM_HELPERS=0` opts out). Rare RAM mirror/MMIO boundary code moves to cold functions so GCC can inline ordinary accesses. It preserves all memory operations and arithmetic. Host262144 integer and16384 floating comparisons pass ASan/UBSan; native8192 integer and16384 full-FPSCR floating comparisons pass. Scripted full RAM and colour/depth captures match exactly, and ordinary live Town driving returns to Coast attract. Full rendering improves8.44 to8.62 modeled game presents/s (2.09% less time); graphics-disabled simulation improves73.4% to77.1% of realtime. Current alternate-frame rendering gives12.34 logical/6.17 displayed presents/s at43.1% of realtime, versus8.62 at30.1% for full rendering. It retains original simulation and halves this scene’s28.67 guest presents/s; it is not a literal60/30 toggle. Divisor1 remains the default.

Additional measured experiments remain disabled: aligned double-RAM copies (`WII_PERF_F64_RAM=1`) save0.44% in the CPU-only scene; a general descriptor binding cache (`WII_PERF_TEXTURE_BINDING_CACHE=1`) preserves RAM/EFB but regresses0.59%; immutable lookup-object binding reuse (`WII_PERF_LOOKUP_TEXTURE_CACHE=1`) preserves RAM/EFB and saves0.65%. Both binding variants pass32768 native SDK-shadow/full-FPSCR cases. None is enabled by the live builder. The split RAM-helper default remains8.62 modeled game presents/s.

Single-copy clipping now defaults on (`WII_LIVE_SPLIT_COPY_ONCE=0` opts out). It copies each complete vertex trio once and uses overlap-safe movement for interior inputs, preserving all clipping arithmetic and winding correction. Host80000 ASan/UBSan and native32000 full-FPSCR comparisons pass, including overlapping input/output and capacity failures. Scripted full RAM and colour/depth match exactly; ordinary Town driving returns to TOP10 attract. The matched scene improves8.62 to8.73 modeled game presents/s (1.20% less time). The latest headless and alternate-frame numbers above precede this graphics-only change.

Latest matched single-copy measurements: the race-start window gives8.73 displayed game FPS at30.5% simulation speed with full rendering, or12.45 logical/6.23 displayed at43.4% with alternate-frame rendering. During the later driving window, full rendering gives10.77 displayed at37.6%; alternate rendering gives14.19 logical/7.10 displayed at49.5%. The identical CPU path without graphics gives21.06 logical at73.5%. Both rendered builds preserve exact full RAM and captured colour/depth. These are modeled Wii measurements; Dolphin’s FPS overlay measures a different rate.
