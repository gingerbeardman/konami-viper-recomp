# Wii graphics coverage

Source audit, 2026-10-05. Reference: runtime/voodoo/voodoo_render.cpp and voodoo_regs.h. Target: gx_renderer.c and render_family.h. This is a capability inventory, not proof of game-wide coverage.

There is no measured count of unseen GTI Club 2 state combinations. Register values combine independent operations; counting unknown tuples would overstate missing implementations. The default target now compiles decoded FBI equations and shared rejection policies with established single-TMU sampling. Full Voodoo-to-TEV translation remains incomplete.

| Capability | Current target | Remaining work |
|---|---|---|
| FBI colour/alpha equations | Decoded sources, constants, subtract, multiplier, add and inversion | Texture-alpha local override, Z/W alpha and nonclamped input modes |
| TMU equations | Live replacement/pass-through; standalone general compiler GPU-tested | Integrate true two-texture combination and spatial LOD factors |
| Texture sampling | Common converted formats and fixed mip | Variable LOD, LOD dither, NCC formats and additional layouts |
| Chroma rejection | Filtered black predicate | Arbitrary key/ranges and rejection independent of blend alpha |
| Alpha operations | Selected tests/blends; narrow parity mask | General source mask, destination-alpha factors and alpha-plane storage |
| Depth | Compare/write, approximate W lookup | Z source, bias and alternate modes; reference precision |
| Fog | Approximate mode41 table path | Other sources and constant/add/multiply modes |
| Raster/framebuffer | Basic triangles, clipping, clear and scanout | Stipple and ancillary modes; subpixel/interpolation/dither precision |

## Proactive implementation plan

1. Decode registers into an operation plan using the reference field definitions. Describe equations rather than matching complete hexadecimal tuples.
2. Implement an integer CPU evaluator following the reference order: texture sampling/combining, source selection, chroma/mask rejection, colour equation, alpha test, fog, blending and writes. Confirm the exact order against the reference raster loop before coding.
3. Compile supported equations into GX TEV stages, tracking registers, texture inputs and stage limits explicitly.
4. Differential-test equation boundary values against the reference, then test actual EFB colour/depth output with native probes. Classifier admission tests alone do not establish GPU correctness.
5. Record normalized game states on the desktop reference across selection, every course/car, effects and results. Report unique observed states, translated states and explicit gaps; observed coverage is not exhaustive game coverage.

Label each operation separately as implemented, approximate and GPU-validated. Do not classify a family as exact because a boot checkpoint passes. Some register bits (including marked antialias/trilinear) are explicitly unimplemented in the reference too.

## Implementation evidence

`voodoo_color_plan.h` decodes all 21 FBI colour-path fields. `test_color_plan.py` compares a million states with bit definitions extracted from the reference header. Unsupported-state diagnostics now log these fields.

`voodoo_color_eval.h` evaluates the FBI colour equation on already-clamped byte inputs. `test_color_eval.py` extracts the actual MAME combine_color function body and compares a million inputs under ASan/UBSan. It covers source choices, texture-alpha local override, subtract, multiplier selection/inversion, add, clamp and output inversion. Depth source clamping, chroma/mask rejection, sampling, fog and framebuffer blending are outside this evaluator's current scope. It is a CPU test oracle, not yet the GX translator.

## Latest stop

cmd0180edcb / cp1d022401 / fbz2175b / alpha4511f / fog40 / TMUs10241507. This uses an existing colour equation, EQUAL depth, chroma and source-alpha blending. It exposed a rejection/depth interaction rather than a new texture format. The restricted EQUAL-depth relaxation is implemented and classifier-tested. Native probe run.fVQrso on Intel Dolphin2606 passes 512 alpha/depth cases with zero failures. Live run.8lyJo3 has rendered Town at race time1:05; passage beyond the original later halt is still pending.

## Desktop state catalogue

Enable `RT_VOODOO_STATELOG=1` in the desktop reference. Each new configuration logs the MAME-normalized FBI/TMU states plus LOD and chroma parameters. Constants and alpha-reference values are not part of the catalogue key. The bounded catalogue reports overflow explicitly. Run `python3 wii/analyze_graphics_states.py LOG` for a JSON inventory; feature counts do not imply unsupported states.

First bounded capture: `build/wii/desktop-state-catalog.log`, completed180 emulated seconds with scripted Start/accelerator inputs. It records233 distinct configurations and five normalized colour paths (including00000000); no overflow. `build/wii/desktop-state-catalog.json` contains the inventory. No frame capture was taken, so the gameplay path and complete-course coverage are not established. MAME normalizes texture formats/equations and can discard unused TMUs; compare semantics rather than feeding normalized hexadecimal values directly into the Wii raw-state classifier.

## Optional GX equation compiler

`VIPER_WII_GX_COLOR_EQUATION` activates `gx_color_equation.h` for textured FBI equations. Sources include iterated/texture/constant colour and alpha, independent multipliers, subtraction/addition, and inversion after clamping. Signed intermediates remain in PREV as D. Local texture-alpha override and Z/W-derived alpha are guarded. Sampling remains limited to established replacement/pass-through TMU layouts; this does not implement true two-texture combination. Generic admission includes original-source black chroma and alpha parity under the shared rejection policy; unsafe zero-alpha colour/depth survivors remain guarded.

The post-equation pipeline uses dynamic stage indices and reserves stage capacity for chroma, mask, fog and depth. REG1/REG2 initial constants must remain unchanged through draw submission; later GPU stage outputs may reuse them after the equation consumes them. Fog now uses separate K1 and REG0 RGB storage. Native fixedARM2606 probe run.5wEGFk passes512 RGB/alpha cases with observed maximum error2/255 and tolerance3; this is approximate, not pixel equivalence. Scripted integrated run.Fmr1nR visually END PASS and full16MiB RAM identical to baseline. Modeled race time3059572us vs2999113us baseline (~2% more); The default live preset now enables the compiler after broader predicate/fog validation and deterministic RAM equivalence; WII_GX_PIPELINE=0 retains the older preset.

## Varying iterated colour and rejection

Latest confirmed stop, persisted in run.FAfEsw: cmd01c02d0b/cp15024100/fbz0002177b/alpha0004511f/fog40/TMUs10241c07, key00000000/range10000000. It combines varying iterated RGBA, black chroma and LEQUAL depth writes without an alpha mask. Earlier wrapped screenshots were misread as fbz0000217b; this caused a valid parity-mask implementation to be tested against the wrong live problem. Archived disassembly and the persisted fatal log resolved the discrepancy. No ARM64 JIT error was established here.

The iterated-chroma family is admitted semantically. Per-triangle checks reject wholly black coverage, bypass the key when one RGB byte channel stays positive across all vertices, or encode chroma rejection when every vertex alpha byte stays positive. Mixed chroma/zero-alpha depth survivors remain guarded. Native run.RLZkO9 passes1020 positive-alpha chroma/depth cases,2048 mask cases,256 gradient samples and a volatile-input runtime classifier check. Visual END PASS and the exported SD result agree. Live candidate run.CBQD5K tests the integration on patched2606 ARM64 JIT; passage remains unproven.

Astra reviewed source/register lifetime, stage capacity and primitive proofs. The proof is valid for submitted GX byte colours, not full Voodoo interpolation equivalence: clamping/quantizing before interpolation can differ from fixed-point interpolation followed by clamping, especially around negative, fractional and over-range values. Split children recompute their proof. REG0/REG1 alpha are helper scratch registers; later fog/depth stages need separate capacity reservations. Antialias/reserved modes are guarded.

The separate parity family remains restricted to source-alpha blending without depth writes. Original predicate sources are preserved independently of the final equation. Future source-derived rejection should carry source dependencies, scratch ownership and an explicit alpha/depth policy. Launcher snapshots preserve renderer source at launch for context; archived ELF/DOL remain authoritative for compiled behavior.

## Latest integrated validation

Generic live run.JtgYiP reached 213 guest seconds, rendered Town gameplay and returned to attract mode without a graphics halt. This is bounded path coverage, not proof of every course, effect or results screen. Deterministic run.2rhumJ visually reports END PASS phase4 step11 substate5 hash9730789c; its full16MiB RAM SHA-256 is545d1ce1f4dc8120c85c25d73ab8b9ce129bea820e145e94772f5061165f2253, identical to the established baseline.

Native run.FxmPnd passes1024 general TMU equation cases with zero failures and maximum error2/255 (tolerance3), including detail/fraction constant factors and PREV/REG0 output lifetimes. This compiler is not yet integrated with live sampling. Generic admission now uses the converter capability list for formats0,2,3,4,5,8,10,11,12,13,14; NCC, RGBA palette and reserved formats remain guarded.

Generic untextured equations are now admitted by decoded source dependencies, with no observable TMU input; packet0b tuple shortcuts are bypassed. Native mixed suite run.llIVRW passes512 equations,2048 predicate/depth and512 blended-policy cases (half equations untextured), maxerror2/255.

A later live halt at guest246 (run.orPKNJ, cp1c482405/fbz2177b/alpha4411f) exposed valid zero-alpha depth survivors under SRC_ALPHA/ONE blending. Shared SPLIT_DEPTH policy now draws each triangle colour-first with no depth writes, then binary original predicates with colour disabled and original depth compare/write. Native run.4o4Lid passes2304 comparisons/alpha/rejection/write-mask cases and later geometry. Deterministic run.Y4XejQ preserves baseline fullRAM; the new longer live run will validate textured/projective W-depth integration.
