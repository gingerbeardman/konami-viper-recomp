# Wii graphics performance work

## Current measured position (2026-10-06)

The measurements below this section are historical. Current reproducible ARM64 results and artifact paths are in [PERFORMANCE_PLAN.md](PERFORMANCE_PLAN.md).

Fixed Dolphin 2606 ARM64 JIT, speed unlimited, synchronous shaders, full TMU pipeline, audio disabled, identical scripted guest61–64 interval:

| Build | Modeled interval | Game speed | Displayed presents/s |
| --- | ---: | ---: | ---: |
| Baseline full graphics | 14.0724 s | 0.213 | 6.11 |
| Graphics disabled baseline | 5.4953 s | 0.546 | — |
| Graphics disabled + memo/FIFO | 4.8252 s | 0.622 | — |
| Alternate rendered frames | 9.8706 s | 0.304 | 4.36 |
| Solver reuse + zero quotient + FIFO bounds | 13.8528 s | 0.217 | 6.21 |
| Above plus exact reciprocal-sqrt memo | 13.1715 s | 0.228 | 6.53 |
| Above plus separate FIFO triangle decoder | 13.0802 s | 0.229 | 6.57 |
| Above plus early culling (current default) | 12.7262 s | 0.236 | 6.76 |
| Current optimizations + fog disabled (visual tradeoff) | 11.9096 s | 0.252 | 7.22 |
| Current optimizations + alternate rendered frames | 8.7902 s | 0.341 | 4.89 |
| Graphics disabled + memo/FIFO + separate decoder | 4.7732 s | 0.629 | — |

All listed scripted guest75 full-RAM checkpoints match. Combined optimizations also match all307200 captured colour/depth pixels exactly. Alternate rendering corresponds to roughly29 versus14.5 target presents per guest second, not60 versus30. These are instrumented emulator measurements; physical Wii performance remains unproven. Headless logical presents are not displayed FPS.

The separate triangle decoder saves1.08% headless scene time and0.69% with graphics, preserving exact RAM and captured colour/depth. After live menu/racing/steering and return-to-attract validation, it defaults on; WII_LIVE_FIFO_SPLIT=0 disables it. Changing the CPU sampling period from997us to733us still attributes about32% of samples to upload/FIFO, supporting that optimization target.

Triangle rendering accounts for about60% of full scene time. Texture/TEV setup is about17.5%, fog/depth11.3%, subdivision6.4%; these categories are nested. Headless CPU samples identify FIFO upload/bookkeeping, software sqrt and the generated geometry routine as leading costs. TEV setter cache experiments were slower and remain disabled; recomp-O3 saved only0.20% scene time for roughly39% text growth and is not adopted. No guest operations were removed.


Baseline conditions: GTI Club 2, sound disabled, enhanced scripted Start at
guest18, Dolphin2606 x86 JIT64/Metal/HLE under Rosetta. run.B3OPnY rendered the
course and cars. Provisional guest20-to21 measurements span30.05 emulated Wii
seconds for1 guest second; SD tail is corrupt, so remeasure before drawing a
hardware conclusion. Host sampling shows significant Dolphin throttling.

## Baseline
- [x] Record emulator, architecture, build flags and immutable DOL per run.
- [x] Verify advancing textured course and cars visually.
- [x] Collect per-operation baseline entries (SD extraction remains provisional).
- [ ] Target the original game rate; measure both guest progress and presents.

## Profile
- [x] Measure GX fence calls and elapsed time separately by site.
- [x] Measure clipping time and generated child count.
- [x] Measure depth-table generation and colour conversion time separately.
- [x] Identify largest measured costs before selecting an optimization.

run.GU90Qu guest20-to21 interval:30.08 emulated seconds; depth table generation
17.60s (58%), clipping4.00s (13%), colour conversion/upload4.56s (15%).
Depth fences71ms, colour fences13ms. Same interval generates92,514 children
from54,941 split callbacks. All entries are cumulative differences; overhead
outside those categories is not assigned. Car-selection screen visually verified.

## Strategy
- [x] Compare immutable depth tables with mutable single-slot uploads.
- [x] Compare clipping only overlapping bands with scanning all66.
- [x] Compare multiple colour-cache slots with the single upload slot.
- [x] Estimate gains from measured totals and check RAM cost/synchronization.

Selected change:64 immutable tables,1MiB total (+1008KiB), eliminates per-draw
table generation and its overwrite fence. Allocation address is logged for
memory verification. Maximum gain implied by measured generation cost is about
2.4x overall; actual gain and visual equivalence must be measured.

## Implement
- [x] Implement the highest-value measured change.
- [x] Keep pixel/depth equations and ordering intact.
- [x] Build both experimental and strict renderer configurations.
- [x] Verify native probes or relevant host regressions.

## Validate
- [x] Rerun the same scripted checkpoint and instrumented settings.
- [x] Compare operation times, guest progress and present counts.
- [ ] Inspect course/car alpha, depth and later race rendering.
- [ ] Report limitations; unthrottled host speed is not hardware performance.

run.jxLt2E same guest20-to21 interval:12.389487s versus30.082879s baseline
(2.428x). Split children92,514 unchanged; clipping4.001790s and colour upload
4.558639s unchanged within instrumentation noise. Depth generation delta0;
initial generation31,937us. Allocation0x910b50c0 is inMEM2,1MiB. The run reaches
a later affine A8 alpha-test guard; optimized selection screenshot and gameplay
remain unverified. Further pressure is colour conversion and clipping; measure
and validate each next change separately.

run.xw65ab skips non-overlapping clipping bands: same interval9.738444s,
clipping1.329647s, colour upload4.558638s,92,514 children unchanged. This is1.27x
over immutable tables,3.09x over initial baseline. A20,000-triangle ASan/UBSan
differential test produces byte-identical triangles versus the previous splitter.
The game reaches a fog-enabled draw, which is the next feature gap.

Four-slot colour cache experiment: default4 fixed256KiB images,1MiB total
(+768KiB over the original single slot). Complete texture keys and wrapped
page versions are retained; valid victim rewrites fence queued GX work.
Replacement is round-robin. Host ASan/UBSan invalidation tests and strict native
renderer compile pass. Matching one-slot build is in progress for isolation.

Provisional `run.GsKgJi` guest20-to21:8.486396s versus historical
`run.BRt3W0`9.748460s (1.149x). Colour conversion/upload3.291267s versus
4.558317s (27.8% less). Children92,514, packets31,248, triangles76,222 and
presents29 match. Cache interval hits88,039/misses4,475. A CUA screenshot
subsequently shows a rendered race at71km/h, AT3, elapsed5.374s. This is
preliminary emulator evidence: matching one-slot comparison, boundedEND75
and native queued-image eviction validation remain separate checks.

Matching one-slot `run.2F4ZCG`: guest20-to21 9.758662s versus four-slot
8.486396s (1.1499x). Colour conversion4.558470s versus3.291267s. Cache
misses6,962 versus4,475 (35.72% less), same packets/triangles/children/presents.
Both displayed SCRIPTED END PASS at guest75. This isolates slot count using
the same heap allocation implementation and graphics fixes. Texture trace
profiling is separate and must not be used for clean timing comparisons.

Texture residency diagnostic `run.F7lyE5`, provisional through guest64:
113 observed `(base, dimensions, TMU, format)` source layouts sum7,081,984
RGBA8 bytes (6.75MiB). No observed page-version or PAL8-epoch updates, and no
1024-entry catalogue overflow. This suggests a compact resident cache could
retain much of this sequence. Counts are layouts, not immutable whole-game
assets; page updates can be conservative, duplicate TMUs/overlapping layouts
can overcount, and old revisions/later scenes are excluded. Catalogue overhead
is diagnostic-only; do not compare its elapsed times as optimization evidence.

Resident-cache experiment (`VIPER_WII_GX_RESIDENT_CACHE`): up to256 metadata
entries with exact-size32-byte aligned images and a12MiB image budget. Converted
bytes are keyed by source layout/pages and palette where relevant; sampler/LOD
and post-filter chroma are applied separately. Hits first check the last bound
slot. Valid victim images are fenced before free, including byte-budget eviction.
Host invalidation/canonical-key and converter bounds tests pass under ASan/UBSan;
strict native resident and fixed-slot branches compile. Metadata/allocator
overhead is additional to the image budget.

`run.JZ5BPi` guest20-to21:5.182736s versus four-slot8.486396s (1.637x),
matching92,514 children,31,248 packets,76,222 triangles and29 presents. All92,514
texture binds hit; conversion delta0. At guest74 cache bytes9,491,456, misses127,
evictions0. The final screen lacked readable END text, so bounded PASS is not
claimed for this run. A console initialization/flush fix adds explicit state and
hash evidence; `run.258EVb` reruns the resident variant. A256KiB budget variant
is prepared separately to force evictions before wider rollout.

Resident-cache `run.258EVb` final console PASS phase4 step11 substate5,
RAM hash2b21c69c. Forced256KiB cache `run.lVhKGr` also PASS with peak262144
bytes and183,878 evictions through guest74, hash079307d1. Live RTC initialization
prevents treating these hashes as a cache equivalence comparison. Scripted Wii
runs now use UTC epoch1791103223; interactive RTC remains live. Fixed-clock
resident plane profiling `run.AlHVHn` and a rebuilt small-budget variant are
prepared to compare hashes under the same inputs. Plane timings split texture,
depth and fog solves; profiling overhead is separate from clean cache timings.

Fixed-clock resident `run.AlHVHn` displayed PASS phase4 step11 substate5
hash9730789c. Its plane microsecond totals are invalid: per-call integer
conversion truncated short solves. The profiler now sums raw timebase ticks
and converts once when reporting. Fixed-clock eviction comparison is running
as `run.t1eush`; corrected plane-profile DOL is built but not yet rerun.

Fixed-clock small-budget `run.t1eush` displayed PASS phase4 step11 substate5
hash9730789c, matching large-cache `run.AlHVHn`. It exercised183,878 evictions
through guest74 while peak images remained262144 bytes. This verifies bounded
guest-state equivalence under cache pressure; complete framebuffer equivalence
and later-scene mutation behaviour remain separate checks.

Expanded setup-profile baseline `run.Tigrh8` displayed SCRIPTED END PASS
phase4 step11 substate5 hash9730789c. Guest20-to21 elapsed5.208899s,
split1.329363s, texture conversion0, matching92,514 children,31,248 packets,
76,222 triangles and29 presents. Setup deltas in seconds: projection/clip
0.032106, color/depth/alpha/blend0.049202, texture/TEV0.331605,
fog/depth lookup0.212820, vertex submission0.080590. Matrix solve timing is
included within setup timing and must not be added twice.

W-depth band boundaries now use exact binary32 bit construction instead of
two ldexpf calls per band. A20,000-triangle differential comparison against
the preceding implementation is byte-identical; ASan/UBSan splitter tests
also compare all64 boundaries against an independent ldexpf oracle. Native
speed comparison `run.OydWop` guest20-to21: elapsed4.118420s versus baseline
5.208899s (1.265x); split277700us versus1329363us (4.787x). Work counts match:
92,514 children,31,248 packets,76,222 triangles,29 presents, conversion0.
These are Dolphin-modeled Wii timings, not physical Wii measurements. The
optimized run displayed SCRIPTED END PASS phase4 step11 substate5
hash9730789c, matching baseline guest RAM. This does not prove complete pixel
equivalence or later-scene coverage.

Whole renderer callback timing `run.Me2Ppx` guest20-to21: elapsed4134800us,
triangle callbacks1214490us, clear43us, present176us, split277696us (inside
triangle time). Rendering accounts for29.38%; remainder includes guest CPU,
device processing, scheduling and profiling. It cannot all be labelled guest
execution without further measurements. Bounded console PASS phase4 step11
substate5 hash9730789c. CPU lookup timing is the next diagnostic.

CPU lookup diagnostic `run.WkYQg7` guest20-to21: elapsed4137840us,
43,397 lookups,3610us lookup time (0.087%). Dispatch lookup is not a useful
optimization target at this checkpoint. Native assembly probe
`build/wii-cpu-check/memory_probe.s` confirms ordinary flat-memory LD32's
memcpy already compiles to lwzx on the fast RAM path; avoid assuming bytewise
loads are responsible. The remaining guest/device cost needs further profiling.

Native PC sampler `run.YwwzLC` records4151 samples, overflow0, unmapped0;
269 samples straddle64-byte function buckets and remain unattributed.
Leading unambiguous samples: f_g_00045dd8 25.85%, fifo_run11.13%, triangle10.05%,
voodoo_reg_write5.08%, vram_write4.36%. Instrumented guest20-to21 elapsed
4142940us versus lookup-only4137840us (~0.12% overhead at this checkpoint).
Final console PASS phase4 step11 substate5 hash9730789c.

The leading guest function is a six-record worker polling loop. Astra proposes
batching only complete equal-status idle rounds within the existing budget,
with full context reconstruction; this remains unimplemented pending tests.
FIFO packet assembly now resumes presence checks at the first missing word,
avoiding rescanning already-present prefixes. Header rewrites/read-pointer
changes/resets retain invalidation. ASan/UBSan device tests pass, including
out-of-order payload arrival and header rewrite after partial arrival. Native
speed and bounded state comparison remain pending.

FIFO prefix optimization full-rate `run.kdSiTb`: guest20-to21 elapsed3922217us
versus lookup-only4137840us (1.055x); packets31,248, triangles76,222,
presents29, split277698us. Final console PASS phase4 step11 substate5
hash9730789c. This run is the full-rate baseline for the requested half-rate
comparison.

Optional `VIPER_WII_GX_FRAME_DIVISOR=2` skips GX triangle/clear callbacks and
scanout on alternate guest presents, retaining the previous displayed image.
Guest simulation, FIFO processing, vblank and input scheduling remain at the
original rate. The display clock is57.5Hz; subsequent work-count checks show
29 guest presents per second in the measured scenes. The comparison therefore
renders29 versus15 frames in that interval, not literal60 versus30 FPS.
Half-rate `run.Nzn1r6` is running; native timing,
rendered comparison guest20-to21:3299315us versus full-rate3922217us (1.189x).
GX output copies15 versus29, renderer triangle callbacks591703us versus
1214489us; guest packets31,248, triangles76,222 and presents29 unchanged.
Half-rate console PASS phase4 step11 substate5 hash9730789c matches full-rate.
This validates bounded guest-state preservation, not complete skipped-frame
visual correctness or physical Wii speed. An unintegrated idle-worker helper
is prepared in runtime/idle_worker.h; equivalence tests must pass before use.

Idle-worker batching is opt-in via VIPER_WII_IDLE_BATCH. The emitter restricts
it to f_g_00045dd8 with the exact65-instruction SHA256 and cycle scale1;
trace/audit configurations omit the hook. It batches complete94-cycle equal
status rounds within the current slice, reconstructs GPR/CR/XER state, and
retains the original residual loop/checkpoint. Differential tests extract the
actual emitted routine and compare complete context/checkpoint PC in3500
cases under ASan/UBSan: PASS. Each pending-record position and RAM bounds
falls back without changing context. Astra read-only review finds no blocker
for current MAX_SLICE20000 and synchronized input design. Native paired
interrupt/task-switching, performance and full scripted hash checks remain
pending; the experiment build is compiling.

Idle batching native build completed; full-rate `run.0PoY4O` is running against
the FIFO-optimized full-rate baseline. Keep the half-rate comparison separate
until the bounded race state/hash gate verifies this CPU change.

Full-rate idle-batching `run.0PoY4O` guest20-to21 elapsed2867324us versus
FIFO baseline3922217us (1.368x). Packets31,248, triangles76,222, presents29,
children92,514 and split277699us match. Final console PASS phase4 step11
substate5 hash9730789c matches the baseline. This covers the bounded scripted
interrupt/task-switching sequence; later gameplay remains unverified.

Matching half-rate with idle batching `run.XzkbES`: guest20-to21 elapsed
2244421us versus full-rate2867324us (1.278x). Guest packets31,248,
triangles76,222 and presents29 match; GX copies15 versus29 and children44,637
versus92,514. Final half-rate console PASS phase4 step11 substate5
hash9730789c matches full-rate. Native VRAM
assembly inspection confirms existing bytewise LE source already compiles to
lwbrx/stwbrx word accesses, so rewriting it is not justified by current evidence.

Post-optimization native samples `run.AhT4xl`:2876 samples, overflow0,
unmapped0,279 boundary-ambiguous. Worker polling drops to6 unambiguous samples
(0.21%); triangle14.50%, fifo_run10.08%, vram_write7.61%,
voodoo_reg_write6.33%, software sqrt5.63%, projective matrices5.15%,
split5.01%. These sampled proportions guide further work; do not add them to
inclusive callback timers or infer physical Wii speed. Final gate pending.

Post-optimization sampling `run.AhT4xl` final console PASS phase4 step11
substate5 hash9730789c. Disabled MMIO logging still accounts for2.05% of
unambiguous samples. The logging macro now bypasses range-check helper calls
when the initialized limit is0, retaining lazy environment initialization,
enabled logging/range and decrement behavior. Matching timing/race comparison
is running as `run.u0cYyu`. Updated interactive full-rate build is compiling
for sustained accelerator/steering validation.

Disabled-logging optimization `run.u0cYyu`: guest20-to21 elapsed2792050us
versus2867324us (1.027x), same31,248 packets/76,222 triangles/29 presents.
Final console PASS phase4 step11 substate5 hash9730789c. Guest70-to71 actual
race interval3353370us,32,773 packets/87,151 triangles/29 presents. Earlier
half-rate run.XzkbES race interval2690788us has matching guest counts; its
logging path differs, so this is not a fully isolated frame-divisor comparison.
Interactive optimized full-rate build completed and is now launched.

Interactive `run.RXdQ03` reached a later attract transition guard:
cmd01402d0b cp15024100 fbz2132b alpha0c045109 fog40 tmu10241ac7/10241ac7.
Dreamcast already handles this exact no-blend untextured state with a primitive
alpha/chroma proof. Wii now mirrors that proof: all alpha<=12 rejects; all
alpha>=13 plus a common positive RGB channel permits varying color; wholly
black rejects, mixed coverage still guards. Live Start remains unproven in
this run (no consumed input in extracted log). Rebuilt live validation launched.

Live `run.BUqFWZ` visually reached car selection and a rendered driving race
(red Mini, timer64, elapsed6.580,0km/h,AT1), clearing the preceding transition
guard. Repeated focused E keys produce INPUT START consumed; keyboard2 yields
WII_IN_ACCEL edges (mask40). This proves delivery, not sustained throttle or
steering response. User sustained-input observation is pending; game stays live.
Idle differential tests now also mutate each of six records after a preserved
checkpoint and compare the first dispatched job target plus complete context:
PASS under ASan/UBSan, in addition to3500 existing cases.

Live run.BUqFWZ later halted after guest101 on an affine A8 overlay:
cmd10008d0b cp1c484104 fbz2132b alpha4511f fog40 tmu102412c6/102412c6.
The existing A8 path now accepts this captured depth-disabled ALWAYS-alpha
variant with the same texture/color equation and blend settings. Live rerun
is required before claiming later-race coverage. Full-word LFB writes also
avoid the unnecessary old-VRAM read; partial masks retain read/merge behavior,
and FIFO presence/version/invalidation remain shared. ASan/UBSan device tests
PASS; native timing gain is not yet measured.

Live run.FE2Hd2 hit another attract state after guest64:
cmd0100ed0b cp1c482405 fbz213fb alpha4411f fog41 tmucc7/10241cc7.
It exactly matches Dreamcast argb4444_always_additive_state variant1, now
accepted by Wii's existing ARGB4444/chroma/additive/fog path with exact tuple
guards. Review caught the prior A8 edit matched the neighboring ARGB affine
predicate; that unintended expansion is reverted and the A8 change is now
in the intended cp1c484104 predicate. Thus the earlier A8 change is not yet
runtime-validated. Corrected live build completed and relaunched.

Live run.HUR2yG stopped at cmd0100ed0b/cp1c482405/fbz213fb/alpha4421f/fog41/TMUs10241cc7, matching Dreamcast ALWAYS-depth DST_COLOR/ONE variant. Wii now accepts the exact tuple only with zero fog RGB and black key/default range: keyed black stays zero RGB and is a blend no-op, with no depth writes. Nonzero fog retains the guard. Native build passes; live validation restarted as run.YLhG1w, runtime passage remains unverified.

run.YLhG1w reproduced the DST_COLOR/ONE guard at guest64, proving zero-fog admission insufficient for this scene. Exact state now uses a binary filtered-TEXC black predicate (K0 alpha255 / COMP_BGR24_GT / GREATER0), rejecting keyed fragments independently of original texture alpha before coloured fog blending. Source alpha is unused by DST_COLOR/ONE; depth writes remain disabled. Native rebuild passes; runtime and pixel validation pending.

run.TZIfPZ passed the alpha4421f state and reached a new alpha4411f tuple with both TMUs10241cc7 (source-alpha additive variant). Focused coloured-fog/chroma probe run.HXAkTY: visual CHROMA PASS failures=0. Keyed pixel remains RGB32,64,96 versus unmasked control48,72,120; filtered edge and white survivor both match control48,72,120 despite zero original alpha; all depth values unchanged cccccc. This validates GX predicate/blend behavior in the tested samples, not complete Voodoo pixel equivalence.

Native source-alpha additive extension accepts captured TMU0=10241cc7 alongside existing cc7, selecting local_unit (0 versus1) consistently with existing texture combiner mapping. Build passes; run.eEzaJ9 launched for attract validation. Astra read-only review requested for both latest additive cases.

Astra confirms both additive mappings; found strict-build undeclared local_unit outside CHROMA_APPROX. Replaced with exact TMU0==cc7?1:0; strict GX renderer compile and active live rebuild pass. run.eEzaJ9 passed the captured alpha4411f variant, then visually stopped on cmd0180ee0b/cp1d022401/fbz2175b/alpha4221f/fog40/TMUs10241507. New RGB565 DST_COLOR/SRC_COLOR state has depth writes enabled and needs binary keyed-fragment rejection, not an alpha-zero blend shortcut. Raw SD log exported to run directory.

Captured RGB565 cp1d022401/fbz2175b/alpha4221f/fog40/TMUs10241507 now admitted with exact black key/default range. Existing GX decoding selects EQUAL depth, enabled depth writes and DST_COLOR/SRC_COLOR blending; binary postfiltered TEXC predicate rejects black before depth writes. Native build passes; runtime passage and focused blend/depth validation pending.

run.Hm3L3k reached guest87 beyond prior RGB565 guard. Focused double-colour blend/depth probe run.RX7a4v visually PASS failures=0: EQUAL white sample RGB64,128,192 from background32,64,96, keyed black unchanged; nearer LESS draw leaves key depthcccccc while edge/white write19999a. Probe tests GX binary rejection/blend/depth using RGBA8 filtered black/white with alpha0, not complete RGB565/Voodoo precision parity.

Launcher experiment WII_DOLPHIN_INPUT_TEST=1 supplies controller-expression CLI overrides (constant throttle, timer Start) and records the mode without editing saved mappings. sh -n passes. run.JsrFDR reaches guest43 with remote connected but no consumed input events, so effective controller override support is unverified; do not claim sustained-throttle validation from this experiment.

Replaced unverified CLI controller expressions with opt-in per-run private Dolphin profile, copying Config/Load/Wii/WiiSDSync and writing explicit Wiimote1 INI expressions. Default profile remains unchanged, private user_dir recorded with run artifact. sh -n passes; run.qxYVHi launches/rendering and remote connected. Consumed input still unverified after raising/clicking game window.

Input diagnostic build passes; run.9blo22 private-profile test reports544 then1198 main-thread polls with remote connected, ruling out poll starvation. Timer Start produced edge8 and START consumed menu1. Constant Buttons/2=1 still yields no accelerator bit; numeric expression interpretation needs correction before sustained-throttle proof.

Changed automated throttle expression to1.0 to avoid keyboard single-digit bareword fallback (official Dolphin ExpressionParser.cpp CoalesceExpression). Focus-gated run.u5Hv3W still no inputs. Added BackgroundInput=True only to isolated test launch. run.lAmeRf now logs accelerator edge40, snapshot held bit40, then Start consumed menu1 buttons48. This establishes receipt through live WPAD/latch path; sustained car acceleration and steering remain to verify.

Correction: Dolphin background input is Main/Input/BackgroundInput (official MainSettings.cpp), not Core. Previous lAmeRf transient input did not prove background setting applied. Launcher corrected to Dolphin.Input.BackgroundInput=True. run.Phmdr4 now shows accelerator bit40 held in both guest10/20 snapshots (poll544/1198), followed by Start consumed menu1/buttons48. Rendered race motion still pending.

Apple Silicon retest: launcher now accepts explicit WII_DOLPHIN_ARCH=arm64. Current live DOL run.Jsv1mW, installed Dolphin2606 native ARM/core4, visually reproduces JitArm64_RegCache.cpp:412 BindForWrite assertion !will_read || reg.IsInPPCState(), Attempted to load a discarded value, before game boot. Stopped verified owned PID42612; separately stored official2609 launched native ARM/core4 as run.nmKTEH, startup pending. No assertion ignored or macOS protection changed.

User target fixed to Dolphin2606; stopped verified2609 PID42990. Middleware comparison: existing jinks-wii.dol explicitly launched2606 ARM64/core4 as run.0YZ8pF; visually renders bundled Asteroids POWERUPS attract with JITARM64 title, no assertion at boot. This is middleware shell boot evidence, not Beltrunner gameplay confirmation. Middleware shell uses SDL render path and w2c2 translated core, shared PPC750 hard-float ABI; its standard launch leaves architecture/core saved defaults. Viper current native2606 fails before boot at JitArm64 register-cache assertion, indicating workload-triggered ARM JIT incompatibility rather than blanket libogc/DOL failure. Exact triggering PPC instruction remains unlocated.

Returned to Intel Dolphin2606/core1 at user request. Held-throttle live run.kIwrfJ reached new cmd10008d0b/cp1c482405/fbz2132b/alpha4511f/fog40/TMUs10241c06 guard during selection. Exact affine ARGB4444 tuple now admitted with default black key/range; existing SRC_ALPHA/INV_SRC_ALPHA and postfiltered chroma alpha gating apply, with no depth writes. Native build passes; updated Intel live input test launched, passage pending.

Systematic family classifier introduced in render_family.h: supported texture/colour equations now independently combine affine/perspective TMU replacement/pass-through with all8 depth comparisons, depth writes and captured alpha/blend equations, preserving fixed ancillary fbz bits. Binary chroma predicate applies whenever ALWAYS-alpha RGB blend ignores source alpha. SRC_ALPHA+ALWAYS+chroma+depth-write combinations retain guard because replacing alpha would change blend and alpha-zero survivors can write depth. Existing special texture/alpha-mask states retained. Native build passes; classifier coverage tests and runtime validation pending.

Family classifier ASan/UBSan/Werror host tests PASS4480 combinations across5 supported colour/format pairs, affine/perspective packets, TMU replacement/pass-through,8 depth functions, write/key toggles and7 alpha equations. Ancillary unsupported bits, reserved blend, fog mode, packet and null output rejection covered. This verifies classifier decisions, not GPU pixel parity. Intel2606 live family build launched for runtime validation.

Live family run.J1Wcot visually reaches race at51km/h AT2 elapsed2.411 with town course rendered; guest59/substate5 logged. First visual sustained accelerator proof through live WPAD path. Astra review finds no clear new classifier semantic blocker; notes affine ARGB4444 varying-colour guard and existing exact fallback admission, plus modulated-alpha rounding approximation. Family-selected draws now permit varying iterated RGBA through existing TEV equation/interpolation; runtime validation of this follow-up pending. No full-family pixel fidelity claim.

Race run.J1Wcot later stopped at affine AI44 HUD cp1c482405/fbz2132b/alpha4511f/TMUs4c0/102414c0. Added its distinct TMU pass-through layout to render-family classifier across supported depth/blend choices. ASanUBSan/Werror tests now PASS4704 combinations; native build passes. Combined AI44/varying-colour build relaunched Intel2606; passage pending.

AI44/varying family live run.kKSsbE visually passes prior stop: town driving elapsed14.675, speed62km/h AT3, mirror/HUD rendered. Astra read-only review finds AI44 mode0 selection and varying RGBA equation consistent with existing path; interpolation and varying modulated-alpha threshold rounding remain approximations. Continuous steering and longer race completion still unverified.

run.kKSsbE visually still driving at race66.105sec,68km/h AT3, timer25; car/HUD/collision sparks rendered without stop. Straight held throttle rubs wall, so this is sustained rendering/input evidence, not successful course completion. Optional WII_DOLPHIN_STEER_TEST=1 added to isolated input mode, periodic disjoint D-pad left/right expressions; sh -n passes, not launched yet while current race end path is being observed.

Long run.kKSsbE reaches race substate7 then phase4/step14 at guest161; screenshot subsequently shows car/transmission selection again. Periodic Start may skip results, so this proves timeout/end transition and return selection, not actual results-screen pixel correctness or winning course completion. Steering test launched with isolated periodic D-pad inputs, validation pending.

Steering run.AnwqPc logs left/right edges200/400 with held throttle and visually drives yellow car69km/h AT3 at28.144sec race, changed view/orientation; no graphics stop. Mapper/latch ASanUBSan/Werror tests PASS. Added mapped analog triplet to ten-second snapshot diagnostics (native build PASS) for explicit guest steering/throttle observation; current running artifact predates that diagnostic update. Full physical tilt/control feel not verified.

Next CPU target traced: old nativePCprofile run.AhT4xl samples5.63% __ieee754_sqrt; generated gl_000/kernel_001 frsqrte emit1.0/sqrt(double). Wii native reciprocal-square-root estimate is a candidate to replace expensive exact software sqrt while preserving guest instruction intent; cross-PPC estimate/edge compatibility review requested from Astra before implementation. No speedup or equivalence claim yet.

Standalone instruction characterization run.sjrYrp, Intel Dolphin2606/core1,
visually PASS failures=0. Twelve special/boundary operands exercised, including
signed zero, infinities, quiet NaN payload, subnormal and maximum finite.
4096 positive operands maximum relative error0.000183105469 (~0.0183%).
This validates the tested emulated Wii instruction behavior, not physical Wii
or MPC603e estimate bit equivalence. Added rt_frsqrte helper and emitter lowering;
default remains legacy1/sqrt(double). VIPER_WII_NATIVE_FRSQRTE is explicitly
experimental and off by default. Regenerated gticlub2 successfully; gameplay
comparison, host FPSCR effects and native speed gain remain unmeasured.

Fresh default-helper scripted baseline run.JmxKrv visually END PASS at
phase4/step11/substate5, RAM hash9730789c, matching the prior baseline.
Raw SD export stops atguest74 despite visibleguest75 completion; retain
visual endpoint evidence rather than treating missing last log as a hang.
The launcher now refuses to launch when ps reports an existing Dolphin
process; verified rejection with owned baseline PID42171. Restart uses
SIGKILL, since SIGTERM invokes Dolphin quit confirmation and does not prove
process exit. Matched native-estimate build compiled successfully.

Native-estimate run.QDkfiJ visually END PASS phase4/step11/substate5, but RAM
hashd220b09a differs from baseline9730789c: compatibility is not proved.
Matched20→21 interval baseline2.761665s, native2.611981s (~5.42% reduction);
70→71 driving baseline3.314666s, native3.120058s (~5.87% reduction).
At both interval endpoints packet/triangle/present/clear counts match. These
are single-run emulated-Wii timer measurements, not physical hardware speed
or repeatability evidence. Estimate stays opt-in; investigate RAM differences
and geometry before considering default adoption. Existing standalone checks
do not establish MPC603e estimate-bit equivalence.

Scripted endpoint now writes16MiB guest RAM to sd:/viper/ram.bin after timing
and work counters. Extract via read_sd_log.py IMAGE --file ram.bin --output
FILE; compare_ram.py accepts two exact16MiB snapshots and reports changed
bytes/words/64KiB regions. Baseline snapshot run.oHpBB4 visually END PASS,
exported16777216bytes, independently computed FNV9730789c matches display.
Matched native snapshot run launched; byte-level comparison pending.
Astra corrected earlier site analysis: all seven GL estimate sites plus
kernel12584 have three refinement stages. Single-precision rounding can
retain estimate-dependent differences. GL output/transform involvement is
supported by nearby stores; kernel helper callers may include non-graphics.
End-state comparison alone cannot establish earliest divergence or causality.

Native snapshot run.ID8MAX visually END PASS, exported16MiB independently
hashesd220b09a. compare_ram.py finds9635changed bytes/7394aligned words across
22regions. Largest counts:8c0000=3073,8d0000=2688,560000=1898bytes.
Examples interpreted as floats at240c:39.8208542→39.8213882 and107088:
0.758102834→0.758102953; types/purposes are not established by interpretation.
There are also differing integer-looking values. This is not only a final
hash discrepancy and does not establish harmless graphics-only drift.
Full report build/wii/frsqrte-ram-comparison.txt; both snapshots preserved in
their run directories. Native remains off by default. Matched no-GX exact
helper/idle-batch build compiled and launched to measure CPU/device floor.

Current no-GX exact-helper run.aYdp1Y reachesguest75/race substate5 and exports
RAM byte-identical to rendered baseline run.oHpBB4 (SHA256
545d1ce1f4dc8120c85c25d73ab8b9ce129bea820e145e94772f5061165f2253).
20→21 interval1.565940s vs rendered2.761665s;70→71 driving1.890042s vs
3.314666s. Driving work matches:32773packets/87151triangles/29presents,
no new clears. Omitting GX saves42.98% of measured interval; CPU/devices
still occupy57.02% and cannot reach real time alone in this workload.
These are Dolphin emulated-Wii timer intervals, not host wall-time FPS or
physical-Wii measurements. Fresh half-cadence current-backend build started
to complete the matched full/half/no-GX comparison.

Astra low native2606 ARM diagnosis: same scripted baseline DOL SHA256
0201f8ec8d4a738870a8a47804381ff6fe7bf06a1ece10fa12c56797e4cceb68
reproduces BindForWrite412 assertion in default core4. Register-cache-off
instead asserts FlushRegisters262; load/store-off retains BindForWrite412.
Integer-JIT-off alone (-C Dolphin.Debug.JitIntegerOff=True) visibly renders
car/course selection then Town race lap5.443sec,71km/h,AT3 in JITARM64.
This establishes a broad emulator-side workaround and implicates integer
compilation/bookkeeping, not a specific instruction or register. Cache also
contains CR entries. No assertions ignored or DOL/app/security changes.
LLDB attach denied; compiler frames not recovered by sampling. No final END
gate, RAM equivalence or performance measurement claimed for this run.
Owned PID79940 force-stopped and absence verified. Exact settings/evidence:
build/wii/runs/arm2606-integer-off/diagnosis.txt. Launcher opt-in
WII_DOLPHIN_JIT_INTEGER_OFF=1 adds and records this option; sh -n PASS.

Launcher ARM validation run.AW5edL (core4/integer-JIT-off/2606, same baseline
DOL) visually SCRIPTED END PASS phase4/step11/substate5 hash9730789c.
Extracted16MiB RAM byte-identical to Intel run.oHpBB4, zero changed words;
SHA256545d1ce1f4dc8120c85c25d73ab8b9ce129bea820e145e94772f5061165f2253.
Full comparison build/wii/arm-checkpoint-ram-comparison.txt. This establishes
tested final-state equivalence, not all workloads or speed. Verified owned
PID91701 force-killed and absence verified. Astra investigating narrower
addme/subfme r0 implicit-read candidate with a standalone reproducer;
specific original failure trigger remains unproven.

Astra standalone libogc arithmetic probes now reproduce a specific2606 ARM
JIT defect: addme/subfme r0,r3 both assert BindForWrite412; corresponding r5
controls pass10/10result cases each, as do r0 probes with integer-JIT-off.
Handlers at Integer.cpp1220/1497 use will_read=(d==a||d==b), despite mex
instructions having reserved RB=0 and using literal-1. Thus destinationr0
requests a dead old value. Proposed two-line guard (!mex&&d==b) preserved
in wii/diagnostics/arm_integer/dolphin-2606-mex-read.patch. Standalone
defect is reproduced; exact original Viper failing PC remains uncaptured.
README.txt adjacent records six runs and numerical-test scope (result values,
not outgoing XER/record/overflow). Separate fixed2606 build preparation is
underway; installed app remains unchanged, patch not yet build-validated.

Custom ARM64 Dolphin validation (2026-10-05): separate 2606 build with
the two-line mex-read predicate correction passes both r0 arithmetic probes
(20 cases total) and the baseline game scripted checkpoint, run.uAsdly.
Normal integer JIT is enabled; no assertions were ignored. The full 16 MiB
RAM snapshot is byte-identical to Intel baseline run.oHpBB4. The installed
Dolphin remains unchanged. This enables native host testing but does not
prove physical Wii performance or complete game fidelity.

Matched current-backend render-cadence comparison (2026-10-05):
Half cadence run.vXt1Lu visually reached SCRIPTED END PASS, hash9730789c;
its complete 16 MiB RAM snapshot matches baseline byte-for-byte. Native ARM64
custom 2606 uses normal integer JIT. No assertions ignored. Owned run force-stopped.

| Rendering | Guest 20 to 21, modeled seconds | Guest 70 to 71, modeled seconds |
| --- | ---: | ---: |
| Full (~29 presents/guest second) | 2.761665 | 3.314666 |
| Every other frame (~15 rendered/guest second) | 2.145832 | 2.580676 |
| No GX | 1.565940 | 1.890042 |

Half cadence reduces race time by 22.14% (1.284x throughput); no GX reduces
it by 42.98%. CPU/device work alone exceeds real-time budget. These are Dolphin
modeled guest timings, not host wall-clock FPS or physical Wii measurements.
Full and half work counters count incoming game presents; skipping happens
inside the renderer, so those counters do not count actual displayed frames.
All three RAM snapshots are identical within this scripted test scope.

No-GX CPU sampling run.6GUfeh passes the full scripted checkpoint and RAM
comparison (zero changed bytes). Sampling guest20-to21:1571 samples, overflow0,
unmapped0,274 boundary-ambiguous. Unambiguous attribution as fraction of total:
fifo_run18.20%, voodoo_reg_write11.78%, software sqrt8.78%, f_gl_0002adac6.05%,
voodoo_lfb_write5.79%, hw_write_5.47%, hw_read_2.67%. Symbols come from the
exact run ELF. This is a selection interval, not a racing profile. FIFO and
register handling are the next candidates for state-preserving optimization.

FIFO presence range clearing validated in run.MHjtRf: visually SCRIPTED END
PASS hash9730789c; full 16MiB RAM identical to baseline. ASan/UBSan device
contracts pass, including 3072 alignment/length combinations and VRAM-end
neighbor preservation. No-GX modeled selection interval1.549538s versus
1.565940s (1.05% less); race1.872219s versus1.890042s (0.94% less).
The improvement is small and is not a physical-Wii measurement. Packet
consumption now clears interior presence bytes as a range; edge bits remain
masked so adjacent packets retain their presence. Rendered validation pending.

Rendered FIFO range-clear validation run.zDHH8V: visually SCRIPTED END PASS
hash9730789c; complete16MiB RAM byte-identical to baseline. Rendered selection
interval2.745264s versus2.761665s; race3.296844s versus3.314666s (~0.54% less).
This completes the rendered gate for range clearing within scripted scope.
CPU sample windows are now configurable with VIPER_WII_PC_PROFILE_BEGIN/END;
default20/21 remains, racing build70/71 is being prepared.

No-GX racing CPU profile run.2PdzYH guest70-to71:1878 samples, overflow0,
unmapped0,245 boundary-ambiguous. Visually SCRIPTED END PASS and full RAM
byte-identical to baseline. Attributed fractions of total: fifo_run13.79%,
software sqrt8.79%, voodoo_lfb_write6.02%, hw_write_5.96%, f_gl_0002adac4.53%,
f_gl_00028fc4 3.14%, rt_lswi1.92%, rt_stswi1.70%. Register writes only0.75%
in this interval, unlike selection11.78%. FIFO remains next optimization target.

Race FIFO native sample localization: buckets8068f340/380/3c0 contain
115/59/37 samples. Exact disassembly run.2PdzYH/fifo-run.disasm places them
in header decode, cached-prefix resumption, and early-return register restore.
The parser is entered on each FIFO word write; incomplete-packet returns
pay a large register save/restore sequence. Candidate next step: advance the
cached missing-word frontier before entering the full packet parser, while
preserving header invalidation, sparse writes, swaps, and packet side effects.
No implementation or speedup claim yet.

Cached FIFO readiness candidate implemented: full-word FIFO writes advance
the cached missing-word frontier before calling fifo_run, avoiding parser
entry for incomplete packets. Cache use requires no pending swap and matching
read pointer. Header writes/read-pointer changes retain invalidation. Host
ASan/UBSan device tests pass, including all24 permutations of four payload
words, sparse tail/header rewrite, swap behavior, and presence range tests.
An initial permutation test failed because its fixture left FIFO disabled;
explicit FIFO setup resolves it without changing candidate implementation.
No-GX validation launched as run.DPOalh; timing/RAM result pending.

FIFO readiness run.DPOalh visually SCRIPTED END PASS hash9730789c. Complete16MiB RAM matches baseline byte-for-byte. No-GX selection interval1539200us, race1861463us versus range-clear baseline1549538/1872219us. Rendered validation pending.

FIFO readiness rendered rebuild completed and launched as run.xIxL4y;
checkpoint gate pending. Next runtime candidate from racing samples:
rt_lswi/rt_stswi together3.62% attributed. Current helpers loop per byte.
A RAM-only grouped-word path must retain register wrap, partial load zero-fill,
RAM mirroring, MMIO byte access order, and memory-audit/paged-memory behavior.
No implementation or performance claim yet.

Rendered FIFO readiness run.xIxL4y visually SCRIPTED END PASS hash9730789c; full RAM unchanged. Selection2734925us, race3286089us. Both FIFO optimization gates now pass within scripted scope.

Opt-in VIPER_WII_STRING_WORDS RAM string-transfer candidate implemented.
Actual runtime helpers compiled in byte-reference and candidate configurations
pass16896 differential cases for both loads/stores under ASan/UBSan. Checks
cover all starting registers, lengths0..32, unaligned RAM, mirror boundaries,
RAM/MMIO crossings,32-bit address wrap, complete context and MMIO order/values.
Paged memory and memory-audit builds retain the original byte path. Game
checkpoint/timing validation pending; candidate remains off by default.

Opt-in string-word run.mTynQu visually SCRIPTED END PASS hash9730789c; full RAM unchanged. No-GX selection1519385us, race1806797us versus byte baseline1539200/1861463us. Rendered gate pending; remains opt-in.

Rendered string-word run.imRHIW visually SCRIPTED END PASS hash9730789c; full RAM unchanged. Selection2715110us, race3231423us versus byte baseline2734925/3286089us. Both scripted gates pass; remains opt-in until live validation.

Astra read-only review recommends the Voodoo bus-to-FIFO write chain;
combined racing attribution~26% includes FIFO/LFB/hardware write functions.
Native run.imRHIW lfb-write.disasm confirms efficient lwbrx conversion already
and an out-of-line vram_write call per word. Opt-in
VIPER_WII_INLINE_VRAM_WRITE forces that bookkeeping helper inline; ASan/UBSan
device contracts pass. No bus ordering/device semantics changed. Matched game
build in progress; no performance claim and default behavior unchanged.

Inline-VRAM candidate built and launched as run.xxa9q0. Native
lfb-write.disasm confirms no vram_write call and direct stwbrx stores.
No-GX timing/RAM gate pending; opt-in remains disabled by default.

Inline-VRAM run.xxa9q0 visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. No-GX selection1400697us, race1754363us versus string-word baseline1519385/1806797us. Rendered gate pending.

Inline-VRAM rendered candidate built and launched run.A1aamk. Optimized live
input build started in gticlub2-optimized-live for extended driving/steering
validation after the rendered checkpoint gate. Includes exact math, resident
textures, idle batching, string words and VRAM inlining. Physical Wii tilt
input remains unverified; code already polls WPAD accelerometer orientation.

Rendered inline-VRAM run.A1aamk visually SCRIPTED END PASS hash9730789c; complete16MiB RAM identical. Selection2596423us, race3178988us versus string-word baseline2715110/3231423us. Both scripted gates pass; live validation pending.

Optimized live run.qOnyPI native ARM64 custom2606 launched with isolated
held-throttle/periodic-start/alternating-D-pad steering profile. Visually
reached car selection. Provisional fresh input log confirms WPAD init/format0,
remote connection, enhanced-menu Start consumption, mapped throttle200 and
steering200. Sustained racing/end-transition gates remain pending; input
periodic Start may skip results, so results fidelity needs separate evidence.

Optimized live run.qOnyPI progressed through race substate7 and a second
race with Mountain/AT visible. Throttle/steering mapping verified; successful
course completion/results fidelity not proven. Time-limited Start automation
added as WII_DOLPHIN_START_TEST_SECONDS (0 default retains periodic behavior).
New run.6o8deN uses60 host seconds; menu Start/car selection observed.
Selective DEVICE_LTO=1 experiment builds runtime/platform objects with-flto
and links at-O2, keeping generated code and floating-point flags unchanged.
Use a fresh GAME_OUT when switching this option because Make does not track
flag changes. Default remains0; checkpoint/timing validation pending.

Limited-Start live run.6o8deN visually reaches Town race56.701sec at127km/h
AT4, guest138 in provisional log, advancing scenery/effects without a stop.
Not successful course completion or reference-pixel proof. Selective LTO
build completed; native rt_mmio_w32 tail-calls generic hw_write_, so full
constant-size bus specialization is not established by this build. Runtime
checkpoint/timing experiment remains pending until current race finishes.

Selective DEVICE_LTO=1 no-GX run.kFglTQ visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical to run.oHpBB4 (zero changed bytes/words). Selection1383813us, race1726651us versus inline-VRAM baseline1400697/1754363us: 1.2%/1.6% less modeled time. Rendered gate pending; default remains off. Limited-Start live run.6o8deN reached guest275 and visually returned to enhanced menu after racing; successful course completion/results fidelity remains unproven. Final SD log preserved in its run directory.

Rendered DEVICE_LTO=1 run.yA9206 visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection2587506us, race3156682us versus inline-VRAM rendered2596423/3178988us: 0.34%/0.70% less modeled time. Small benefit, default remains off. Added opt-in VIPER_WII_DIRECT_LFB_WORD to specialize full-word LFB MMIO writes only after logging initialization disables logging; other devices/logging use original bus path. Host ASan/UBSan wrapper contract passes300 address/value/log-state combinations. Game checkpoint/performance gate pending.

Direct-LFB no-GX run.q1RY2L visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection1351495us, race1691590us versus LTO-only1383813/1726651us: 2.34%/2.03% less modeled time. Rendered gate pending; option remains off by default.

Rendered direct-LFB run.EE028s visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection2560371us, race3127292us versus rendered LTO-only2587506/3156682us: 1.05%/0.93% less modeled time. Added opt-in VIPER_WII_WDEPTH_INTERIOR bypass for triangles strictly inside one slab; boundary cases retain original splitter. ASan/UBSan differential test100000 triangles returns byte-identical output, counts and success status including zero capacity/degenerate/boundary cases. Candidate build gticlub2-wdepth-interior pending; defaults unchanged.

W-depth interior run.c9Uryj visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection2479447us, race3051744us versus direct-LFB2560371/3127292us: 3.16%/2.42% less modeled time. Host geometry differential already byte-identical100000 cases; game RAM match alone does not prove pixels. GX setup/plane profiling build started in gticlub2-gx-profile to identify remaining renderer pressure.

GX instrumented run.GPH7Ae visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Race70->71 modeled3090945us (instrumentation adds overhead). Setup0 projection/clip38421us(1.2%); setup1 framebuffer50499us(1.6%); setup2 texture/TEV337259us(10.9%); setup3 fog/depth393046us(12.7%); setup4 submission89101us(2.9%). Split196022us(6.3%),92467 children; conversion/upload0us. Nested plane0/1/2 times89255/85624/84127us must not be summed with setup. Analyzer wii/analyze_gx_profile.py preserves cumulative-counter delta semantics; saved report gx-race-profile.txt. Next experiment: reuse fixed depth/fog texture descriptors, preserving per-child matrices and GX load/order.

Opt-in VIPER_WII_GX_LOOKUP_OBJECTS preinitializes64 depth and64 fog texture descriptors after table allocation. Each draw copies its descriptor locally before GX_LoadTexObj, preserving load-time mutation isolation, per-child matrices and order. Build gticlub2-lookup-objects succeeded; run.3wBcGJ launched for checkpoint/timing gate. Existing W-depth tests,100000-triangle differential,300-case direct-LFB and16896 string-helper checks pass under ASan/UBSan. Descriptor game gate pending; defaults unchanged.

Lookup-object run.3wBcGJ visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection2459907us, race3011903us versus depth-interior2479447/3051744us: 0.79%/1.31% less modeled time. Combined rendered race improvement9.13% less than exact baseline3314666us. Matching alternate-frame build gticlub2-optimized-half started; divisor2 keeps simulation/input unchanged. Live pixels and physical Wii remain unverified; opt-ins remain disabled by default.

Optimized alternate-frame run.rwphJj visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection1901500us, race2342854us versus optimized full2459907/3011903us:22.70%/22.21% less modeled time. Same simulation and input timing; scene presents approximately29 versus15, not60 versus30. Latest CPU-only race1691590us remains below real-time. Live optimized-v2 build started with full rendering and all validated opt-ins for extended input/visual checks.

Optimized live-v2 run.6L9R2E visually reached Town race9.335sec at62km/h AT3, textured course, HUD and traffic visible. Isolated held-throttle/Start limited60hostsec, normal ARM JIT. Sustained gameplay/end-state and pixel fidelity remain pending; no successful course claim. Updated no-GX race70->71 PC-profile build gticlub2-cpu-profile-v2 started to remeasure CPU distribution after optimizations.

Optimized live-v2 run.6L9R2E visually sustained Town52.808sec at113km/h AT4 with advancing textured scenery/traffic/HUD. Final log preserved, then test intentionally force-stopped for refreshed CPU profiling; no course completion/results claim. Updated CPU profile run.vzIqBz launched with noGX, normal ARM JIT, profile70->71 and validated LTO/direct-LFB/string/VRAM/FIFO optimizations.

CPU profile-v2 run.vzIqBz visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical.1698 samples,overflow0,unmapped0,boundary-ambiguous240. Top attributed symbols: voodoo_lfb_write225(13.25%),__ieee754_sqrt179(10.54%),fifo_run140(8.24%),f_gl_0002adac87(5.12%),f_gl_00028fc466(3.89%),memcpy32(1.88%). Inlining changes attribution; lfb share is not evidence of regression. Matching symbols and native lfb disassembly preserved; next investigate sampled instruction buckets before selecting another FIFO change.

Native lfb-write profile bins:8068e100=121samples(readiness return path),8068df40=85(VRAM/cache preamble),8068e0c0=12,8068df80=7. Added opt-in VIPER_WII_FIFO_FRONTIER_WRITE: cached missing frontier can advance only when current aligned write is that word; fifo_mark already proves its presence. Preserve swaps/header invalidation/read-pointer guards, packet timing and payload order. ASan/UBSan device contract passes including24 payload-order permutations and header/reset/sparse-write cases. NoGX candidate gticlub2-fifo-frontier building; game/timing gate pending.

FIFO frontier noGX build completed, run.gzkB3Q launched. Matching rendered gticlub2-fifo-frontier-render building. Existing Astra low profiling subagent requested read-only review of latest native sample attribution and shortcut invariants plus substantial exact CPU targets. Full-game gate pending.

FIFO frontier noGX run.gzkB3Q visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection1342498us,race1682026us versus direct-LFB1351495/1691590us:0.67%/0.57% less modeled time. Small benefit; rendered gate and Astra review pending, default off.

Rendered FIFO frontier run.5kLkEq visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection2452226us,race3003741us versus lookup objects2459907/3011903us:0.31%/0.27% less modeled time. Astra read-only review supports cached-frontier invariants; next target dominant triangle packet layouts with complete vertex/callback differential validation. Added optional FIFO_FORMAT_PROFILE census (512 format+packed bins, packet/vertex counts, independent/start/continue/fan totals) with reset handling and per-checkpoint logs. Census build gticlub2-fifo-formats pending.

Format census run.j49j8T visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Race70->71 format59 unpacked31729 packets/147883vertices,packed format35(key291)1044/4176; format11zero. Added opt-in FIFO_FORMAT59 straight-line decode for measured dominant unpacked layout; generic fallback and strip/fan/callback order unchanged. Host actual-device differential3072 packets/12288 callbacks matches complete vertex streams/final strip state (digest e73af8a8bd15d537), ASan/UBSan. Initial fixture reused freed VRAM; fixed by new allocation after device contract, implementation unchanged. Candidate gticlub2-format59 building; game/performance pending.

Format59 noGX run.Km6AbZ visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection1332685us,race1671455us versus frontier1342498/1682026us:0.73%/0.63% less modeled time. Strengthened host decoder test compares all2691072 callback/strip stream bytes directly, not only digest; same3072 packets/12288 callbacks pass ASan/UBSan. Rendered gticlub2-format59-render building; gate pending, default off.

Rendered format59 run.4pgWhl launched; visually textured Town7.751sec at97km/h AT3. Full checkpoint/timing pending. Added GEN_OPT Make option(default-O2) limited to generated objects; candidate gticlub2-generated-o3 building with-O3, exact floating flags retained and device/runtime optimization unchanged. Fresh GAME_OUT required when switching flags.

Rendered format59 run.4pgWhl visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection2447902us,race2999113us versus frontier2452226/3003741us:0.18%/0.15% less modeled time. Small decoder gain; latest exact rendered remains about3 modeledsec/game sec. Generated-only-O3 experiment builds pending; math/rounding unchanged, defaults-O2.

Generated-O3 build completed; DOL10486728bytes versus format59-O2 7677768bytes(+36.6%). Run.QnqtLa launched noGX with exact floating flags and unchanged device/runtime optimization. Game RAM/timing gate pending. Added code footprint makes physical cache behavior a required later hardware check even if modeled Dolphin timings improve.

Generated-O3 noGX run.QnqtLa visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection1326577us,race1652943us versus generated-O2 format59 1332685/1671455us:0.46%/1.11% less modeled time. DOL+36.6%code footprint; keep default-O2 pending physical cache/performance evidence. Matching rendered O3 build started. Asked whether physical Wii Homebrew Channel is available while continuing Dolphin validation.

Rendered generated-O3 build completed, run.HkmLJw launched for full-RAM/timing gate. Inspected profiled plane solver: constant depth/fog rows repeatedly divide signed-zero numerators by finite nonzero determinant. Added optional VIPER_WII_ZERO_PLANE_QUOTIENT replacing only zero quotient with correctly signed zero, generic nonzero divisions preserved. Existing projective numerical tests pass ASan/UBSan with exact math flags. Bitwise differential across rounding modes and game/timing gates pending; default off.

Rendered generated-O3 run.HkmLJw visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection2441798us,race2980600us versus generated-O2 2447902/2999113us:0.25%/0.62% less modeled time. Keep default-O2 given code-size cost and unverified physical performance. Zero-plane quotient differential200000 cases/four IEEE rounding modes byte-identical output and status, including signed-zero/degenerate/invalid inputs, ASan/UBSan. Rendered zero-plane candidate builds with generated-O2 for comparison to run.4pgWhl.

Zero-plane rendered run.RL0wA8 launched. Added reproducible live preset wii/build_live.sh [1|2], generated-O2/exact math with checkpoint-tested optimizations including frontier/format59; no pending zero-plane shortcut. Full and alternate outputs live-f1/live-f2. sh-n passed; full live preset build started for sustained decoder validation. README documents current preset scope and original simulation timing.

Zero-plane run.RL0wA8 visually SCRIPTED END PASS hash9730789c; full16MiB RAM identical. Selection2444526us,race2988222us versus format59-O2 2447902/2999113us:0.14%/0.36% less modeled time. User requested Astra low tricky-optimization review; delegated read-only plane/FIFO/descriptor/interior correctness and substantial exact next target. Zero-plane remains optional and excluded from live preset pending review/live scope.

Live run.10Kw7i stopped on cmd0180edcb/cp1d022401/fbz2175b/alpha4511f/fog40/TMUs10241507. Astra low confirms EQUAL-depth with source-alpha/inverse-source-alpha blending admits an observational zero-alpha rejection: a passing zero-alpha survivor changes neither RGB nor stored Z. Added narrowly restricted classifier flag and GREATER0 chroma-gated original alpha; other depth comparisons retain the guard. Classifier ASan/UBSan/Werror PASS4704 cases. Native chroma probe extended to all256 alphas at matching/nonmatching Z (512 cases); run.DF5r1X running, result pending. Live-f1 builds successfully with the new path; not yet live-validated. Unsupported primitive logging now includes vertex RGBA/XY/W. Diagnostic run.w104ZI intentionally force-stopped for focused validation.

EQUAL source-alpha native validation run.fVQrso, official Dolphin2606 Intel/core1: persisted CHROMA END PASS failures0 equal_cases512 equal_failures0. All256 source-alpha values at matching/failing depth; nonblack colour compared to ordinary blend, black key unchanged, stored Z unchanged. Earlier blank console/partial SD outputs were inconclusive; forced host-buffer output made the end result readable. Updated probe links libfat and writes progress/end evidence. Added source-derived colour-path decoder with all21 reference bitfields; million-state ASan/UBSan comparison passes. Decoder currently drives unsupported-state diagnostics, not yet a general TEV compiler. Live-f1 rebuilt successfully.

Live Intel2606 run.8lyJo3 visually Town1:05.760,95km/h,AT3; guest115 logged with no stop. Original later unsupported point not yet proven passed. Added integer FBI colour evaluator with separately clamped input bytes. Differential harness extracts actual MAME combine_color body and uses actual rgbaint_t arithmetic; million comparisons ASan/UBSan PASS. Depth-clamping adapters supply postclamped bytes; no claims for sampling/rejection/fog/blend. Field-decoder million-state comparison still passes after C++-compatible explicit casts. Coverage document updated with exact scope.

Desktop optional RT_VOODOO_STATELOG implemented, mutex-protected distinct normalized FBI/TMU+LOD/chroma catalogue,8192 cap with explicit overflow. Desktop build completed. Bounded headless/enhanced scriptedinput capture180 emulatedseconds exited0;233 configurations/five normalized colour paths/nooverflow. Added JSON analyzer; raw normalized state IDs are not Wii compatibility verdicts. Live Intel run.8lyJo3 observed enhanced start/menu after Town; noresults captured or successfulcourseclaim. Translator signedPREV-D design reviewed by Astra; rounding remainsapproximate and constantregisterlifetimes/stagebudgets needexplicittracking.

Live run.8lyJo3 later visually stopped at cmd0180ee0b/cp1c482405/fbz2175b/alpha4411f/fog40/TMUs10241c07. Extended the semantic EQUAL rule to all admitted colour equations and both SRC_ALPHA/ONE and SRC_ALPHA/INV_SRC_ALPHA, retaining alpha-plane and chroma guards. Classifier4704cases PASS; native1024case validation pending. Implemented isolated general FBI TEV compiler gx_color_equation.h with signed PREV only as D, constantREG1/REG2 lifetimes,1/3stage arithmetic plus optional inversion. Native run.9dqxn8 visual+persisted EQUATION END PASS512cases failures0 maxerror2 tolerance3. This is approximate arithmetic validation, not fullpipeline-equivalence; compiler not integrated into game yet.

Expanded EQUAL source-alpha rule native1024cases (INV_SRC_ALPHA and ONE destinations) PASS failures0 on official Intel2606 run.OrWViZ. User requests fixed2606 ARM64JIT consistently; switched to patchedbinary/core4/arm64. run.JfwcLn SD CHROMA END PASS1024cases failures0; title verified2606-dirty JITARM64 SC Metal HLE. All subsequent port runs use fixedARMJIT unless explicitly diagnosing another backend.

User confirms fixed2606 ARM64JIT. Launcher default now fixedbinary/core4, explicitalternatebinary keepscore5defaultunlessoverridden; sh-n PASS. General equation probe fixedARM run.5wEGFk persisted END PASS512cases maxerror2 tolerance3, matching Intel. Hostsample shows coldMetalshadercompilation/EFBreads during slowprobe, not an emulation hang; no restart was performed for that wait. Completedprobe force-stopped and liveportrelaunched via newdefaults. Generalcompiler remains isolatedprobe pending fullpipelineintegration.

Integrated optional VIPER_WII_GX_COLOR_EQUATION into textured game pipeline; dynamic downstream stageindices preservechroma/mask/fog/depth layout. Reservesmaximumpoststagesbeforeemission. Additional semantic admission for decodedequations with clamp, knownsingleTMUlayout, no chroma/alpha mask and supportedtextures; override/Z-Walpha stillguarded. Default compilerOFF build passes; freshscriptedcandidatebuild passes and fixedARMgate launched. Live run.zTKfez later stopped visually cmd01c02d0b/cp15024100/fbz217b/alpha4511f/fog40/TMUs10241c07: varying untextured colour plus alpha-mask requires perfragmentiteratedalpha parity/chroma, not uniformvertex rejection. Saved latestSDlog beforeforcekill. No successfulcourse/resultsclaim.

Integrated optional compiler run.Fmr1nR fixedARM2606 visually SCRIPTED END PASS phase4step11substate5hash9730789c. Full16MiB RAM SHA545d1ce1f4dc8120c85c25d73ab8b9ce129bea820e145e94772f5061165f2253,changedbytes0/words0 vsIntelbaseline. Modeledselection2504771/race3059572us vsformat59default2447902/2999113us (~2.02%raceoverhead). No pixel-equivalence or livegeneralcoverageclaim. Completedprobe force-stopped. Nextwork perfragmentiterated-alpha parity/chroma for latestliveuntexturedstate.

Early culling with the separate parser passes exact full RAM and framebuffer colour/depth comparisons plus live Town racing/steering and return to attract. It now defaults on; WII_LIVE_EARLY_CULL=0 disables it. Combined scene time is9.57% below the original full-TMU baseline; remaining graphics fidelity and physical Wii speed are still unproven.
