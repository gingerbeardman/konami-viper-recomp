# Konami Viper: static recompilation

Goal: native executables for macOS, Windows and Linux (x86-64 and arm64) that run Konami Viper
games without emulating the CPU. The PowerPC code is translated to C at build time. The board
hardware (Voodoo3, EPIC, CF, audio, I/O) is reimplemented by a runtime shared by every game.

The reference game is Thrill Drive 2 **ver EBB**: most of this document was reverse engineered
on it. Other games and versions run on the same board with the same BIOS and have profiles in
`games/`:
- **Other Thrill Drive 2 versions:** `thrild2j` (ver JAA) and `thrild2a` (ver AAA) share the
  CF card `a41a02`; both boot and race (see section 6). Ver EAA (`thrild2c`) is not supported:
  MAME marks its only CF dump bad.
- **GTI Club 2**, also known as *GTI Club: Corso Italiano* and *Driving Party: Racing in
  Italy*: MAME sets `gticlub2` (ver JAB) and `gticlub2ea` (ver EAA). Both boot and race (see
  section 6).

## 1. Input assets (verified against MAME)

The repository contains none of these files and nothing derived from them. `roms/` (except its
README and placeholders), `work/`, `generated/`, `build/`, the executables and the
`*_nvram.bin` files are all in `.gitignore`. Users put their files in `roms/<MAME set>/`, as
described in the [README](../README.md). The README also lists the full SHA1s.

| File | Role | Notes |
|---|---|---|
| `roms/thrild2/a41b02.chd` | 32 MB CF card | SHA1 `0426f4bb…9aa3` = MAME `thrild2` (ver EBB) |
| `roms/kviper/941b01.u25` | BIOS (29F002, 256 KB) | mapped at `0xFFF00000`; shared by every game |
| `roms/kviper/ds2430.u3` | DS2430A 1-Wire (40 B) | generic dump (same SHA1 in `kviper`, `thrild2`, `gticlub2`); TD2 is not protected |
| `roms/thrild2/a41ebb_nvram.u39` | M48T58 timekeeper (8 KB) | identical in every copy checked |

### Game profiles (`games/<id>/game.json`)

Everything game-specific lives in one JSON file per MAME set.
- **Inheritance:** `"inherits": "<id>"` deep-merges the profile over another one. Objects
  merge key by key, anything else replaces the inherited value, and `null` removes it. The TD2
  versions inherit `thrild2`, and `gticlub2ea` inherits `gticlub2`.

The profile fields:
- **Metadata:** title, other names, version, year and status.
- **`files`:** name, `roms/` subfolder and SHA1 of each required file.
  - `tools/game.py check` verifies them; for a CHD it compares the internal SHA1 reported by
    `chdman info`.
  - `dir` can be a list of subfolders, for a card shared by two sets.
  - `optional: true` marks a file with no known dump. A missing NVRAM starts empty, as the
    erased region does in MAME.
  - `tools/game.py roms-readme` writes a `README.txt` into each `roms/` subfolder, listing the
    files expected there.
- **`modules`:** the `prog.zin` files to recompile, with symbol prefix and header layout
  (`app`/`lib`). The load address comes from the game file's directory, so a module needs a
  `base` entry only if the directory's address has to be overridden.
- **`hints`:** extra entry points and `local_indirect` dispatchers per module (section 4).
- **`inputs`:** `defaults` (IN0–IN7, including DIP switches), `analog_rest` (AN0–AN3 at rest)
  and `handbrake`.
- **`calibration`:** the scripted first-run calibration (`RT_INPUT` syntax) and its length in
  emulated seconds, or `null`. `nvram_set` (`{"0xADDR": "0xVALUE"}`) lists NVRAM bytes written
  after it, for settings TEST MODE cannot change (TD2 JAA's currency) or defaults of the port
  (GTI Club 2's promotion mode); the option block checksum is then fixed.
- **`nvram_options`:** the TEST MODE option block of the NVRAM, `start` and `checksum` (section
  5a), used by `calibration.nvram_set` and by the enhanced mode.
- **`enhanced`:** data for the enhanced mode (section 5a), or `null` where it is not
  available (every current profile has it):
  - `setup`: a scripted TEST MODE pass run on first launch after the calibration;
  - `blank_strings`: game strings to empty in RAM (`addr`, `text`);
  - `hooks`: `{module: {"0xADDR": name}}`. The recompiler inserts `rt_hook(c, addr)` before the
    instruction at each address; `GAME_ENH_HOOK_<NAME>` gives the runtime the address;
  - `font`: the game font for the menus. `file` is in `work/<id>/fs/`. `pages` lists the offsets
    of A8 textures in that file (`width` × `page_height`), stacked vertically into one atlas.
    `sizes.large|medium|small` each give a `cell` grid, a `scale` and `rows` as
    `[y, characters]`, where a space marks an unused cell;
  - `name_entry`: the rankings' name entry (section 5a). `chars` are the characters of the
    game's wheel from index 0, followed by DEL and END; `index_reg` is the register holding the
    wheel index at the `name_index` hook, `index_field` (optional) a 16-bit copy of it at
    `offset` from register `reg`, and `confirm_reg` the result of the confirmation check at the
    `name_confirm` hook.

`recomp.py` turns the profile into `generated/<id>/game_config.h` (`GAME_*` macros). The runtime
is compiled once per game against it; there is no runtime game switch. The other TD2 versions
inherit TD2's module list, hints and enhanced mode. The differences MAME documents (K-type 8-bit wheel
centred at 0x80, handbrake on AN3, DIP SW:3 on for EAA) are already in the profiles.

What GTI Club 2 turned out to need:
- **Shared modules:** JAB and EAA have identical kernel, `gl`, `grphinit` and `sound`; `game`
  and `fpga` differ.
- **`gl` hints:** the same hand-written command-list dispatcher as TD2, moved: entry `0x210A8`,
  slots from `0x20720`, continuation `0x21080` (section 4). TD2's hints do not apply. The hints
  are in `gticlub2`, and `gticlub2ea` inherits them.
- **Inputs:** the game reads the same ADC channels as TD2 (`0x10`/`0x14` steering,
  `0x15`/`0x16`/`0x17` accelerator, brake, handbrake), so the differential model in section 5
  applies unchanged; MAME's 8-bit "K-type" description is not needed.
- **Calibration:** the TEST MODE menu and the CALIBRATION flow are the same as TD2, so the
  profile reuses TD2's script.
- **Handbrake:** the game knows which controls the cabinet has from a table of 7 cabinet
  configurations at `0xBADDC` (7 bytes each: type, steering, accelerator, brake, handbrake, …).
  The entry is chosen by the version code at the start of the NVRAM (`GK941 EAA`, `GK941 EBA`,
  `GM941 JAA`, `GE941 JAA`, `GM941 AAA`, `GE941 AAA`, `GK941 UAA`), and the flags are copied to
  NVRAM `0x74` and to RAM `0x7F6341`. Only the two `GE941` entries have a handbrake. The EAA
  NVRAM is `GK941 EAA`, so this version has no handbrake: no HAND BRAKE item in CALIBRATION or
  I/O CHECK, the same as in MAME. The game still polls ADC channel `0x17`. The JAB NVRAM is
  `GE941 JAB` (flags `01 01 01 01 01`): handbrake present, and type byte 1 = MOTOR TYPE
  K-TYPE (0 = NOT INSTALLED).
- **K-type force-feedback motor** (JAB): see the motor entry in section 5. With the motor
  declared, the steering calibration becomes centre / left / right followed by a motor test
  ("DO NOT TOUCH THE STEERING WHEEL WHEN THE MACHINE IS BEING INITIALIZED", about 50 s).
- **JAB calibration script:** TD2's script, with every step after the steering centre moved
  50 s later (the motor test), plus HAND BRAKE (rest, pulled fully, released) before SAVE AND
  EXIT. Timing note: the Voodoo produces about 57.5 frames per emulated second, so frame
  numbers from `--frames` are not seconds × 60.

## 2. Boot chain (reverse engineering)

1. **BIOS stage 1** (`0xFFF00100`):
   - initialises SDRAM, BATs and PCI (MPC8240);
   - decompresses stage 2 from `0xFFF01804` to `0x00FE0000`, using the LZSS routine at
     `0xFFF01708`.
2. **BIOS stage 2** (`0xFE0000`): ATA/PCMCIA driver plus FAT16. The CF card has an MBR with a
   FAT16 partition at LBA 32. It holds **a single file**, `GMA41B02.BIN` (30 MB).
3. **Boot block** (sector 0 of the file):
   - a 9-word signature, followed by LCG/LFSR padding;
   - at `+0x100`, the descriptor `{?, ?, sector, length, byte sum, load|0x80000000
     (=compressed), entry, halfword sum}`;
   - the payload (sector `0x10`, 0x7248 bytes of LZSS) is the **resident kernel**. It is
     loaded into RAM at 0 and started at `0x10`.
4. **Kernel** (78 KB including BSS, `0x0`–`0x16CF4`): a Konami mini-RTOS.
   - It provides tasks, syscalls (`sc`, through stubs of the form `li r5,N; sc`), IRQs through
     the EPIC, a CF/FAT driver and the internal filesystem.
   - At runtime it builds export stubs in RAM (`mflr/…/bl F/…/blr`) for the loaded modules.
5. **Modules**, loaded by the kernel in this order:
   - `fpga/prog.zin`, which configures the FPGA;
   - `game/prog.zin`, which in turn loads the others;
   - `grphinit/prog.zin` and `gl/prog.zin`, overlays at the **same address, 0x20000**;
   - `sound/prog.zin`.

## 3. Internal filesystem of GMA41B02.BIN

- The directory starts at `0x200`. Each entry is 6 BE words `{hash, sector, size, load,
  bytesum, unpacked_size}`, and entries are sorted by hash. An unpacked size of 0 means the file
  is not compressed.
- Name hash: CRC-32 (poly `0x04C11DB7`) fed 6 bits per character, LSB first (kernel `0xE9B8`).
- `.zin` files use **Konami LZSS**:
  - flag bytes are read LSB first; 1 = literal;
  - 0 = pair `b0 b1`, with `len=(b0&15)+3` and `dist=((b0&0xF0)<<4)|b1`;
  - `dist=0` ends the stream.
- There are 140 entries, and 139 pass their checksum.
- 68 names were recovered from strings. The rest are built at runtime; they are not needed,
  because the kernel looks files up by hash.

### Executable modules

| Module | Load | Size | TOC | Functions |
|---|---|---|---|---|
| kernel | `0x000000` | 78 KB | `0x1323C` | 228 |
| game/prog | `0x038040` | 680 KB | `0xDD690` | ~2040 |
| gl/prog | `0x020000` | 52 KB | `0x2CC44` | ~216 |
| grphinit/prog | `0x020000` | 6 KB | `0x218E0` | ~26 |
| sound/prog | `0x9A0000` | 63 KB | `0x9AF798` | ~144 |
| fpga/prog | `0x040000` | 13 KB | `0x432D0` | 6 |

The modules use the PowerOpen/AIX ABI: function pointers are descriptors `{code, TOC, env}`, and
`r2` holds the TOC. There are two header formats:
- "app" (game, fpga): `{descriptor(3), text_start, text_end, data_start, text_len, data_len,
  bss, toc}`;
- "lib" (gl, grphinit, sound): a descriptor followed by glue code.

## 4. Recompiler (`recomp/`)

- `ppc.py`: decoder for the 603e subset (user and supervisor).
- `analyze.py`: finds functions by recursive descent.
  - Entry points come from:
    - PowerOpen descriptors and pointers to prologues;
    - the **`0x8000xxxx` trailer word** the compiler emits after every function;
    - prologues that follow an unconditional branch;
    - **glue stubs** (`mflr r0; stw r2,12(r1); stw r0,16(r1)`), which are the entry points the
      libraries export;
    - `bl` targets, exception vectors and the profile's `hints`.
  - Jump tables (`bctr` **and** `mtlr rX; blr`) are resolved by path-sensitive symbolic
    slicing over the CFG. Calls clobber the volatile registers, and the index is any `rlwinm`
    whose mask ends at bit 29.
  - Code may be duplicated across several functions; this is always correct.
- `emit.py`: emits one C function per PPC function.
  - Local branches become `goto`, `bl` becomes a direct call, and indirect calls go through
    `rt_call()`.
  - If the body includes code below the entry point, the function starts with a `goto` to the
    entry.
- `hints` (in the game profile): covers hand-written assembly.
  - For now the only case is the command-list dispatcher in `gl/prog` (`0x211C8`). It uses a
    table of 32-byte slots at `0x20760`, and uses LR as a "continue" back into the loop at
    `0x211A0`.
  - The `local_indirect` hint turns the slots into local labels. Every `bctr`/`blr` and every
    return from a tail call then continues with a `switch` on CTR/LR.
- **CPU state**: everything lives in `PPCContext`.
  - RAM is big-endian, and the accessors byteswap.
  - CR has 8 fields, and XER is kept as separate fields.
  - The FPU uses `double`, with `fma()` for the fused instructions.
- **Virtual time**:
  - Every basic block subtracts its instruction count from the budget. One instruction is
    about one cycle at 203.2 MHz.
  - `CHK()` checkpoints at function entries and on back-edges check whether the budget has run
    out, and deliver interrupts.
  - An earlier version charged back-edges with the address span of the loop instead. On large
    loops containing a `switch` this overestimated the cost many times over, and in-game the
    frame rate dropped to 7–8 virtual fps. It now runs at 30 fps, like the hardware.
- `coverage.py`: reports coverage gaps and unresolved `bctr`s. It applies the profile's
  hints like `recomp.py` does (`apply_hints()`), and does not count the `bctr`s of a
  `local_indirect` function, which become a `switch`.
- Debug probes: `RECOMP_PROBES=addr,…` inserts trace/breakpoint points before specific
  instructions (use with `RT_BP`).

## 5. Runtime (`runtime/`)

- **Tasks and `rfi`** (`cpu.c`):
  - Every guest task runs on a host fiber. Fibers are threads that pass a baton, so only one
    runs at a time.
  - Every exception taken at a checkpoint records the key `(srr0, r1)`. An `sc` also records
    `(LR, r1)`, because the kernel returns to the stub's LR.
  - What an `rfi` does depends on its target key:
    - a key of the current fiber: unwind the host frames back to that checkpoint;
    - a key of another fiber: hand control to that fiber;
    - an unknown key: start a fiber (or recycle a free one).
  - The host main thread stays free for the frontend.
- **Dispatch**: a hash map from address to function. `grphinit` and `gl` are overlays at
  `0x20000` with identical entry stubs. When two modules share an address, the one whose
  **signature** (the first 8 words of its image) is present in RAM wins.
- **Kernel runtime stubs**: recognised by pattern and executed in C.
- **BIOS**:
  - The 941B01's uncompressed exception vectors are recompiled as the `bios` module. For
    example, a decrementer interrupt with no handler ends up in the BIOS `rfi`.
  - The CPU state at kernel entry replicates what the BIOS leaves behind (verified against
    MAME): GPR/LR/CR/SPRG = `0xDEADBEEF`, FPR = `0x7FF5BEEF4AFC0721`, MSR = `0x2070`,
    **r31 = `0x63FFFFFF`**.
  - r31 is a boot-parameter word that the kernel saves at `0xFC`. The game derives the state of
    the boot-time switches from it; with a wrong value it believes TEST is held and
    reinitialises the NVRAM.
  - Its top byte is the IN2 byte (DIP switches, DS2430 line) as the BIOS read it:
    `0x63ffffff` for thrild2 (IN2 `0x43`), `0x61ffffff` for gticlub2ea (IN2 `0x41`), checked
    against MAME. The runtime builds it from the profile's IN2 (`hw_boot_param()`).
  - gticlub2ea copies it to the halfword at `0x826`. If bit `0x2000` (DIP SW:3) is clear, the
    check at `0x54628` falls through to a rental/expiry lock ("GAME MODE LOCKED! PLEASE SET THE
    PASSWORD", with an "EXPIRY DATE" in the NVRAM). With a hard-coded TD2 value the game always
    showed the lock screen.
- **Devices** (`hw.c`, behaviour taken from MAME `viper.cpp`):
  - EPIC (IRQs + 4 global timers);
  - I2C with an **ADC0838 in differential mode** for steering and pedals:
    - The address byte the game sends is the ADC mux word (bit3 SGL/DIF, bit2 ODD/SIGN,
      bits1-0 SELECT). `0x10+k` returns max(0, V⁺−V⁻) and `0x14+k` returns max(0, V⁻−V⁺).
    - The game rebuilds `0x100 ± d` and scales it to 16 bits as `(v<<7)|(v>>2)`.
    - MAME treats the ODD bit as a byte select. That is why the steering reads as turned right
      in MAME, and calibration fails there.
    - Inputs are signed positions in −255..+255 (`g_analog`). Steering spans −200 to +200,
      centred on 0. The pedals span −200 (released) to +200.
    - The supply voltage channel (`0x1C`) reads `0x80` (5.0 V);
  - PCI config (MPC8240 + Voodoo3 on dev 12);
  - CF/ATA PIO on the raw image, already in IDE mode as the BIOS leaves it;
  - **M48T58** (a port of `timekpr.cpp`: counters from the host clock, one tick per second,
    R/W bits);
  - I/O ports at `0xFFE10000`;
  - DS2430A 1-Wire (ported from the MAME device; TD2 does not use it);
  - K-type force-feedback motor at `0xFFE20000` (MAME ignores it). A byte write sets bit 7 =
    motor on, bit 4 = direction (1 = right), bits 3-0 = torque.
    - The motor test ramps the torque by one step per second: right `0x91`→`0x9F`, left
      `0x81`→`0x8F`, right again, then it centres the wheel (`0x88`→`0x80`). It reads the
      steering ADC throughout, and a wheel that never moves ends in "STEERING WHEEL : ERROR".
      A real cabinet without the motor fails the same way unless the wheel is turned by hand.
    - With `RT_FFB_WHEEL=1`, which the first-run calibration sets, the virtual wheel follows the
      motor. Above a breakaway torque of 3 it turns at 60 units/s per extra step, up to the end
      stops at ±200. Without it the motor is ignored and the frontend alone positions the
      wheel;
  - **K056230 LANC** (registers and 8 KB of RAM, needed by the boot self-test);
  - 16552 UART (output goes to the log);
  - serial port at `0xFF300000`;
  - audio IRQ3 every 256 samples at 44.1 kHz (RAM buffers at `0xFFF000`/`0xFFF800`).
- **Multithreaded rendering**: the `poly.h` work queue runs on a thread pool. The default is
  min(4, cores/3), and `RT_RENDER_THREADS` overrides it. With more than 4 threads, lock
  contention makes performance worse.
- **Voodoo3** (`runtime/voodoo/`): the **MAME core**, almost unchanged (`voodoo.cpp`,
  `voodoo_2.cpp`, `voodoo_banshee.cpp`, `voodoo_render.cpp`, `poly.h`, `rgbutil`; BSD-3).
  - **Fix: multibase texture addresses** (`rasterizer_texture::recompute`).
    - With `tmultibaseaddr` set, LOD 1, LOD 2 and LODs 3-8 each take their base from their
      own register. The hardware still adds the LOD's offset within the mipmap chain (the sizes
      of the owned LODs before it); Glide's `_grTexCalcBaseAddress` subtracts that offset when
      it computes the registers. MAME used the registers as absolute addresses.
    - The Viper games use multibase for every texture. They clamp the LOD to a single level
      (`lodmin = lodmax = N`), put the real address in level N's register, and set the
      registers of the other levels (the TMUs run in even/odd split mode) so that, once the
      hardware adds the offset, they point to address 0. That is why their base addresses look
      negative, e.g. `0xFF0000` = −64 KB, the size of LOD 0 of a 256×256 8-bit texture.
    - Without the offset, those levels were read from unrelated VRAM. This is the "noise"
      texture corruption that MAME also shows, on walls, headlights, fog and lens flares. MAME's
      TODO in the same function noted that Viper "seems to expect relative offsets".
  - Debug: `RT_VOODOO_TEXLOG=1` prints each new texture setup (format, LOD range, split and
    multibase flags, base registers).
  - The `emu.h` shim underneath provides:
    - `attotime` on virtual time;
    - `emu_timer` on the runtime scheduler;
    - a virtual screen whose timing comes from the CRTC/PLL registers;
    - `delegate`, `devcb`, and the `poly.h` work queue.
  - `voodoo_bridge.cpp` exposes the BAR0/BAR1/IO spaces to C (word offsets, LE values, byte
    masks). The vblank generated by the core publishes a frame (512×384) and raises EPIC IRQ0.
  - The Voodoo3 is a Banshee with a second TMU and higher clocks; in MAME, `voodoo_3_device`
    derives from `voodoo_banshee_device`.
  - All graphics go through the **Command FIFO** in VRAM, which the game writes through the
    linear framebuffer (BAR1).

- **SDL2 frontend** (`frontend_sdl.c`):
  - The main thread handles the window, events and vsync presentation. The window is 512×384,
    scaled 2× by default and resizable; F11 toggles fullscreen.
  - The guest thread syncs to real time at every Voodoo vblank (`rt_pace_vblank`).
  - Audio:
    - At every IRQ3, a 0x800-byte block in RAM holds 256 stereo frames of
      `[L int32 BE][R int32 BE]` at 44.1 kHz, as in MAME's `dmadac`.
    - Levels are low (peaks around 1.5 % of full scale), because the cabinet has an LA4705
      amplifier. The default gain is therefore ×16 (`--volume`).
  - Persistent NVRAM in `<executable>_nvram.bin` (`td2_nvram.bin` for TD2); the original dump is never modified.
  - Default paths (`work/<id>`, `roms/…`, the saved NVRAM) are resolved against the
    executable's directory (`_NSGetExecutablePath` on macOS, `/proc/self/exe` on Linux), so a
    launch from Finder, where the current directory is `~`, works. Command-line paths stay
    relative to the current directory.
  - **First-run calibration**, which replaces the calibration done on a cabinet when it is
    first switched on:
    - If that file is missing and the profile has a `calibration` script, the executable re-launches itself headless.
    - It drives TEST MODE → CALIBRATION on its own with scripted inputs (about 6 s).
    - It then saves the NVRAM with steering and pedals calibrated to the same values the
      frontend produces.
    - At "RELEASE THE STEERING WHEEL" the game waits for the wheel to come back to the centre,
      as the force-feedback motor does on a deluxe cabinet. On a real cabinet without the motor
      this step fails unless the wheel is brought back by hand; the script sets the steering to
      0 at that point, so it passes ("MOTOR TYPE: NOT INSTALLED" is shown either way).
  - FPU rounding: `FPSCR[RN]` is applied to the host FPU (`fesetround`), including across fiber
    switches. The generated code is compiled with `-frounding-math`.

### Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Steer | ← → or A D | left stick |
| Accelerator / brake | ↑ / ↓ or W / S | R2 / L2 (or A / B) |
| Shift up / down | E / Q | R1 / L1 |
| Coin / Start | 5 / 1 | Back / Start |
| Test / Service | F2 / 9 | — |
| Fullscreen / Quit | F11 / Esc | — |

In the enhanced mode the rankings name is typed on the keyboard (section 5a).

### Memory map (from MAME)

```
00000000-00FFFFFF  RAM 16 MB (mirrored at 01000000)
80000000-800FFFFF  MPC8240 EUMB: I2C 0x3000, EPIC 0x40000-0x7FFFF (LE registers)
82000000-83FFFFFF  Voodoo3 BAR0: io/cmd(0x80000)/2D(0x100000)/3D(0x200000)/tex/3D LFB
84000000-85FFFFFF  Voodoo3 BAR1: linear framebuffer (16 MB VRAM)
FE800000-FE8000FF  Voodoo3 I/O
FEC00000 / FEE00000  PCI CONFIG_ADDR / CONFIG_DATA
FF000000 / FF200000  CF: data / task-file registers (16-bit lane at +4 every 8 bytes)
FF300000           unknown serial device
FFE00000 UART, FFE10000 I/O, FFE30000 NVRAM, FFE70000 DS2430, FFE98000 LANC
FFF00000-FFF3FFFF  BIOS
```

## 5a. Enhanced mode (`--enhanced`, `runtime/enhanced.c`)

An optional layer on top of the faithful port, in development. Everything is gated on
`g_enhanced`, and the default mode is unchanged.
- **NVRAM:** the mode keeps its own NVRAM, `<binary>_enhanced_nvram.bin`.
- **First launch:** when that file is missing, the calibration pass runs, followed by the
  profile's `setup` pass. `run_scripted_pass()` chains the passes: each one loads the NVRAM
  saved by the previous one.
  - The setup of Thrill Drive 2 and GTI Club 2 is the same script: TEST MODE → COIN OPTIONS →
    START on FREE PLAY, which toggles it directly (the menu then shrinks to FREE PLAY / FACTORY
    SETTINGS / SAVE AND EXIT / EXIT) → two SHIFT UP → START on SAVE AND EXIT.
  - Pressing START on FACTORY SETTINGS resets the coin options, so the cursor must skip it.
- **Hidden captions:** `enh_on_frame()` runs at every published frame, on the guest thread. For
  each `blank_strings` entry, if the RAM still holds the original text, it writes a NUL at the
  first byte, so the game draws an empty string. The check repeats every frame, which also
  covers a reload of the module.
  - Thrill Drive 2: `0xB8EE8` "FREE PLAY".
  - GTI Club 2 JAB: `0xBAEA0` "FREE PLAY", `0xC1B30` " PRESS START BUTTON".
  - GTI Club 2 EAA: `0xBAEC0` "FREE PLAY", `0xC1B50` " PRESS START BUTTON".
  - Only these exact strings are blanked. The TEST MODE strings ("PRESS START BUTTON =
    CONTINUE", the FREE PLAY option name) and "PRESS START BUTTON TO DECIDE" are separate.
  - `RT_ENH_BLANK="addr:text,…"` blanks extra strings at run time, to find new ones.
- **TEST MODE locked:** the frontend does not pass Test, Service and Coin to the game. Scripted inputs
  (`RT_INPUT`) still reach every port, so the setup passes and the tests keep working.
- **Attract detection:** the hook `attract` sits on the free-play branch of the credit display.
  That routine is shared by the games (TD2 `0x7DD40`, GTI Club 2 JAB `0x61B4C`, EAA `0x61B70`),
  and the game calls it only while no game is in progress. It does not run during the Konami
  logo (about 4 s), so the menu allows a 300-frame gap. A RAM diff between attract and play gave
  no clean mode variable, which is why the hook is used instead.
- **Attract menu** (Thrill Drive 2, GTI Club 2): START GAME, OPTIONS (GAME, SOUND, DISPLAY),
  CREDITS, QUIT, in a compact panel at the lower left.
  - The frontend draws it over the frame (`enh_draw_overlay`) with the game's own font, and
    sends the actions (`enh_menu_action`). While the menu is on screen the game gets no input.
  - START GAME holds START for 12 frames. This is done on the guest thread (`menu_tick`), so it
    also works headless. The menu stays hidden until the game has started; if START is ignored,
    it comes back after 4 s.
  - The font is the A8 texture of the attract captions ("PRESS START BUTTON"). It sits in VRAM at
    `0x6E400` and comes from `_unk/21ae0c7d.bin` at `0x6A0C0` (identical in JAB and EAA). It has
    three sizes on fixed grids (24×40, 16×32, 12×24) with proportional glyphs, and holds only
    uppercase letters and punctuation. `enh_init` measures the ink width of each glyph.
  - Thrill Drive 2 uses its stencil font: two A8 textures (VRAM `0x6C000` and `0x7C000`), from
    `_unk/76278279.bin` at `0x14` and `0x10024`. It is one size on a 32×48 grid, with digits,
    A–Z, a few symbols and kanji, drawn at scales 0.85, 0.6 and 0.45 with bilinear sampling.
  - Found by logging the textures used on the WARNING screen (`RT_VOODOO_TEXLOG=1`, which now
    prints timestamps) and dumping the VRAM (`RT_VOODOO_VRAMDUMP=path:frame`).
  - GTI Club 2's letters texture has no digits. The digits in the same style (the SELECT A CAR
    countdown) are a second texture of the same file (VRAM `0x5E400`, file offset `0x5A0B4`), with
    rows matching the three letter sizes; the profile loads it as a second page.
- **Port settings:** `<binary>_settings.ini` next to the executable (`--settings FILE` to
  override), with `fullscreen` and `show_fps`. These are options of the port, not of the game;
  the game's settings stay in its NVRAM. The OPTIONS page edits them (left and right, or OK), and
  the frontend applies the display mode. F11 also updates the setting.
- **Game settings (NVRAM):** the TEST MODE options are a block of the NVRAM with a checksum.
  The big-endian 16-bit words from `nvram_options.start` up to the checksum word, included, sum
  to `0xFFFF`. The block is TD2 `0x88`–`0x125` and GTI Club 2 `0x84`–`0x121` (JAB and EAA); the
  rule also holds on the dumped NVRAMs.
  - The fields were found by changing one TEST MODE item at a time and diffing the saved NVRAM.
    START steps a value; START held with SHIFT UP or SHIFT DOWN changes a volume.
  - TD2 fields: difficulty `0x8F`/`0x90`/`0x91` (0–7); `0x88` bits 7–6 language (1 English,
    2 Italian: JAPANESE is not offered by EBB), bit 5 speedometer, bit 2 record saving;
    `0x89` currency (0 Japanese yen, 1 U.S. dollar, 2 U.K. pound, 3 euro, 4 lire, 5 H.K. dollar;
    6 and 7 break the boot); `0x94` bits 6–5 attract sound (0 all the time, 1 once every 4 cycles,
    2 complete off) and bits 4–0 music volume (0–30); `0x95` bits 7–3 effects volume;
    `0x97` bit 6 music in game, bit 5 scream, bit 4 siren.
  - GTI Club 2 fields: difficulty `0x8A`–`0x8D` (TOWN, COAST, MOUNTAIN, PROMOTION); `0x84` bit 5
    speedometer, bits 4–3 motor power, bit 2 record saving; `0x85` bit 7 promotion mode (on by
    default in both modes: set after the first-run calibration; ver EAA's starting NVRAM already
    has it), bit 6 Internet ranking, bits 5–4 bonus credit; `0x90` bits 6–5 attract sound and bits 4–0 music
    volume; `0x91` bits 7–3 effects volume; word `0x92` bits 8–7 voice language (1 English,
    2 Italian); `0x93` bit 6 music in game.
  - The profile's `game_options` lists what the OPTIONS pages show: `page` (game or sound),
    English and Italian `label`, `addr`, `size` (1 byte or 2 for a big-endian word), `shift`,
    `bits`, `min`, `max`, English|Italian `values` (or a named set, or none for numbers), and
    `language` on the field the menus follow.
- **Applying:** the game reads its settings only at boot. Changing them live does nothing: a
  language poked into the NVRAM during the attract mode shows no effect, and neither does
  leaving TEST MODE through GAME MODE. So on leaving OPTIONS with changes, the guest thread
  writes the staged fields, fixes the checksum, saves the NVRAM file and asks for a restart. The
  frontend then leaves its loop, and `main` re-executes the program with the same arguments.
  macOS does not reactivate the re-executed program, so `main` sets `RT_RESTARTED` and the
  frontend raises the new window (`SDL_RaiseWindow`, with the `SDL_FORCE_RAISEWINDOW` hint that
  SDL3, under sdl2-compat, needs to activate the app).
  Headless runs stop instead (`RT_NVRAM_POKE="seconds:addr=value"` pokes bytes for tests).
- **Fast boot:** until the attract hook first runs (60 emulated seconds at most) and while
  applying settings, `enh_turbo()` makes the frontend skip real-time pacing and drop the audio,
  and the overlay covers the screen with LOADING or APPLYING SETTINGS. The boot to the attract
  mode takes 2–3 s instead of 10–20.
- **Pause** (Esc, or the gamepad's Guide button, during a game; `enh_escape()`):
  `rt_pace_vblank` holds the guest thread at the next vblank while paused, so virtual time,
  timers and the RTC stop with it. The audio callback outputs silence. The frontend redraws the
  overlay every loop over the last game frame, so the pause menu responds while the game is
  frozen.
  - RESUME continues. MAIN MENU returns to the attract mode the way a cabinet does. TEST opens
    TEST MODE; after 12 s the script moves to GAME MODE (profile `test_menu_game_mode`: TD2 EBB
    and GTI Club 2 EAA 13; GTI Club 2 JAB and TD2 JAA and AAA 12) and presses START; it ends when the attract hook runs again.
  - The whole return runs fast-forwarded behind the loading screen: about 25–30 emulated
    seconds, a few real ones. While it runs, the enhanced layer owns IN3 and IN4
    (`enh_inputs_owned()`), and the frontend does not overwrite them.
  - If the attract mode is not back within 60 s, the program restarts instead.
- **Rankings name entry:** on a cabinet the steering wheel picks each initial and a pedal
  confirms it. In the enhanced mode the keyboard types it (`SDL_TEXTINPUT`, so the keyboard
  layout is followed; SDL's text input is on only during the entry, since on macOS it opens the
  accent picker on a held letter key, e.g. WASD while driving); Backspace is DEL, Enter is END, Left/Right step through the wheel, and the
  game's own confirmation (a pedal) still takes the letter shown, which keeps a gamepad usable.
  - Two hooks per game. `name_index` sits where the game has turned the wheel position into an
    index on its wheel of characters; the hook replaces it with the chosen index (on the first
    call of an entry it adopts the game's, so the entry starts on the same letter).
    `name_confirm` sits on the result of the confirmation check (a pedal past 75%, or a button
    bit at `0x82C`), and the hook makes it succeed once per typed key. The game then does what
    it does on a cabinet: stores the letter and moves on, DEL steps back, END fills the rest
    with spaces, and the third letter ends the entry.
  - The frontend queues the keys (`enh_name_type`, `enh_name_step`), and the guest thread takes
    one typed key per call. The entry counts as active while `name_index` ran in the last 10
    frames; meanwhile printable keys, Left/Right, Backspace and Enter do not reach the game
    (WASD, E/Q, Space and 1 would otherwise steer, shift, brake or press START), and the steering is held at
    the centre.
  - Thrill Drive 2 (EBB, JAA, AAA: same addresses): wheel `A–Z 0–9 . space DEL END` (40
    entries, clamped). `0xB5874` maps the steering to the index, which leaves it in r4 at its
    exit `0xB59C4` and keeps it at `r31+0x34`, where the wheel on screen is drawn from (the hook
    writes both). The confirmation is r3 at `0xB510C`, after the pedal check `0xB59E4`. The entry
    (`0xB4FC8`) runs after the RESULT when the score ranks in the top 25.
  - GTI Club 2: wheel `A–Z 0–9 . & ? ! space DEL END` (43 entries; the index wraps around). The
    routine is `0x8388C` (EAA `+0x24`): the index is in r24 at `0x839B4` (EAA `0x839D8`), the
    confirmation in r3 at `0x839F8` (EAA `0x83A1C`), after the pedal check `0x84350`. It is part
    of the ranking screen (`0x832CC`, mode 0), and asks for a name when the race result flag
    (`+0x18` of the structure at TOC `0x6EC`) is set.
  - Tested headless on Thrill Drive 2 EBB (a forced ranking, `RT_ENH_MENU` `name=` actions);
    GTI Club 2 needs a finished race, which scripted inputs cannot drive.
- **Texts:** English and Italian (`k_text`), chosen by the profile's `language` field. The menus
  switch as soon as the option changes. The fonts have no accented letters, so the Italian texts
  avoid them. A value too wide for its row falls back to the small font.
- **Fps counter:** the frames the game drew per emulated second, counted from the Voodoo buffer
  swaps (`voodoo_swap_count()`). It reads 28–29 (the ~57.5 Hz display halved). Counting distinct
  pictures would undercount static screens and fades. It is drawn with the small game font, on
  top of everything, also in play.
  - Frames dumped with `--frames` in enhanced mode include the overlay.
    `RT_ENH_MENU="seconds:up|down|ok|back,…"` drives the menu in headless tests (`name=TEXT`
    types in the name entry, `<` for DEL and `>` for END), and
    `RT_ENH_LOG=1` logs the attract/game transitions.

## 5b. Research: frame rate and rendering resolution

**Frame rate.**
- **The game paces itself.** It sends `swapbufferCMD` with vsync off and interval 0, about every
  34 ms, so its own main loop sets the 30 fps: one frame every two vblanks of the ~57.5 Hz
  display. The idle part of that loop takes about 64% of the CPU in a race (`0x45E1C` in GTI
  Club 2 JAB, found with `RT_PROFILE`).
- **The logic steps per frame.** A build with a CPU three times slower (`RECOMP_CYCLE_SCALE=3`)
  runs at 14–19 fps, and the race slows down with it: the lap time advanced 5.3 s in 10 emulated
  seconds. Only the countdown follows real time, because it counts vblanks.
- **So 60 fps is not realistic.** Running the frame loop every vblank would run the whole game at
  double speed, unless every per-frame constant of the physics were found and halved. Frame
  interpolation would need a geometry-level (GPU) backend.

**Rendering resolution** (`RT_VOODOO_FBSTATS=1`, 90 s of attract and race).
- **No read-back.** Framebuffer reads are only the boot-time VRAM test. The writes through BAR1
  are the command FIFO (VRAM 0x6xxxxx) and texture uploads.
- **GTI Club 2** draws only into two colour buffers (`0x6DE000`, `0x73E000`, double buffering)
  and uses no 2D engine.
- **Thrill Drive 2** also draws into three off-screen colour buffers (`0x4000`, `0x8000`,
  `0x15B000`), probably textures it renders itself (the rear-view mirror). It uses the 2D engine:
  about 80,000 screen-to-screen blits, which MAME's core leaves unimplemented (`TODO`), and about
  3 million host-to-screen blits.
- **Triangle setup.** Most triangles come through the setup engine (CMDFIFO packet type 3), which
  derives the gradients from the vertices. Scaling the vertex coordinates by N therefore renders
  the same scene at N× resolution.
- **Approach.** Render the main colour and depth buffers into larger shadow buffers, with scaled
  vertices, clip rectangles and fast fills, and scan out the shadow buffer. TD2's off-screen
  targets and blits need care.
- **Cost.** About N² in the rasteriser, which takes ~80% of the host time. At 2× that is roughly
  2× real time on Apple Silicon; 3× is borderline.

**Implemented: scaled render targets** (enhanced mode, DISPLAY → RESOLUTION, 1X = 512×384 or
2X = 1024×768; `render_scale` in the settings file). The Voodoo core supports N = 1–4; the menu offers
1 and 2.
- **Which buffers.** A colour buffer that has been displayed (`update_common` records the front
  buffer) gets a render target N× wider and taller in host memory, with its own depth buffer.
  TD2's off-screen targets, which it uses as textures (the rear-view mirror), are never displayed
  and stay in VRAM at the native resolution; they keep working.
- **Drawing.** Triangles and fast fills aimed at a scaled target (`hires_scale_poly`) get their
  vertices, start position (`ax`, `ay`, now 32-bit) and clip rectangle scaled by N, and every
  per-pixel gradient (colour, Z, W, S, T) divided by N.
  - The start values move by −(N−1)/2N native pixel, so the N sub-pixels are centred on the
    native sample. Without it, the right edge of HUD sprites sampled past their last texel and
    wrapped to the other side of the texture (a one-pixel line next to TD2's rev counter).
  - The rasterizer measures every parameter from the whole pixel holding vertex A (`ax >> 4`).
    Scaled, that pixel is `floor(N·ax)`, which lies fx = `floor(N·ax) − N·floor(ax)` scaled
    pixels past the native one, a different amount for each triangle. The start values also
    move by fx/N (and fy/N) of a gradient, so every scaled pixel gets exactly the native value
    at its position. Without it, coplanar decals (zebra crossings, GTI Club 2's start line) got
    a slightly different depth from the road and flickered as the depth test flipped.
  - `poly_data` now carries the stride, the Y origin and the buffer size per polygon. Scaled
    polygons get a clip rectangle equal to their buffer.
- **Timing.** The triangle and fast-fill costs returned to the emulated pipeline are divided
  by N², so the emulated timing is that of the native picture. The first version did not, and
  GTI Club 2 crashed in the race when the Voodoo looked four times slower.
- **Display.** The display converts the scaled front buffer (`hires_frame`), and the bridge
  publishes that picture instead of the native one.
- **Overlay.** The enhanced-mode overlay is laid out in logical 512×384 units and drawn at the
  frame's scale (`g_ui`).
- **Scale changes** take effect at the next buffer swap; the scaled buffers are dropped.
- **Checked.**
  - The default mode is bit-identical to the previous build: 91 TD2 and 90 GTI Club 2 frames
    compared over attract and race.
  - Speed: 70 emulated seconds take 15.7 s at 2× on GTI Club 2 and 20.6 s on TD2, against
    about 7 s at 1×.
  - TD2's 2D host-to-screen blits and the unimplemented screen-to-screen blits still act on VRAM
    only. No missing element was seen at 2×, but this was not checked in depth.

## 5c. Widescreen (enhanced mode, DISPLAY → ASPECT RATIO)

4:3, 16:10, 16:9 or 21:9 (`aspect` 0–3 in the settings file), at 1X and 2X. The picture is
`512 + 2M` native pixels wide at 384 lines: M = 51, 85 or 199.

**The gl library's state.** Konami's `gl` module (the same code in both games, at different
addresses) keeps its projection and viewport at fixed low-memory addresses, found by searching
a RAM dump for 256.0/192.0 and following the code that reads them:
- two **projection slots** (current one: byte `0x3452` in GTI Club 2, `0x3460` in TD2). Slot 0
  is the 3D perspective (frustum ±0.14 × ±0.105 at near 0.2 in GTI Club 2), slot 1 the 2D
  orthographic projection (±256 × ±192). Each has six matrix terms at `0x26DC + 24·slot`, row 0
  first (e.g. 2n/(r−l)), and the frustum they come from, left, right, bottom, top, near and far,
  at `0x270C + 24·slot`;
- the **viewport**: x scale, x centre, y scale, y centre (256, 256, 192, 192) at `0x2940`
  (TD2 `0x2944`). Screen x = centre + scale · x_clip / w;
- the screen size as integers at `0x274C` (512, 384), which the library writes to the Voodoo's
  `clipLeftRight`/`clipLowYHighY`;
- the bounding-sphere culling (GTI Club 2 `0x281AC`) builds its six planes from the frustum.

**Widening (Hor+).** For a factor k = (512 + 2M) / 512, row 0 of each projection is divided by
k and the viewport x scale multiplied by k. The two cancel on screen, so every pixel stays where
it was, but the clip volume is k times wider; the frustum's left and right are widened k times
around their centre, so the culling matches. The game then draws the same picture plus what
lies to its left and right, at x from −M to 512 + M. The 2D layer, drawn through the same
transform, keeps its 4:3 layout in the centre.
- **Hooks** (`enhanced.hooks.gl`): `projection` after each write of a slot (identity, the two
  frustum commands of the command-list dispatcher, the library's init), `viewport` after the
  viewport API and the init. GTI Club 2 (JAB and EAA share the module): `0x213F4`, `0x21718`,
  `0x2178C`, `0x28C1C`, and `0x212E4`, `0x28E60`. TD2 (EBB, JAA and AAA): `0x217BC`, `0x21AE0`,
  `0x21B54`, `0x2A888`, and `0x215F4`, `0x2AAC8`.
- Each hook widens the values just written, once. A change of the option rescales what is
  already there by k_new / k_applied (`wide_tick`, every frame), so it takes effect at once.
- The emulated timing does not change: the frame rate stays at 29 fps in races. The attract
  demos drift slightly from the 4:3 run (more objects pass the culling, so the CPU does more
  work), but the game logic still steps per frame.

**Voodoo side** (`set_wide_margin`). The displayed colour buffers get render targets
(512 + 2M)·N wide, the same mechanism as the 2X resolution, also at N = 1.
- Native x maps to (x + M)·N. A clip rectangle that spans the whole picture (left 0, right ≥
  512) is widened to the margins; any other is moved by M.
- An untextured triangle spanning exactly x = 0…512 (the fades to a colour) is stretched about
  the centre to the full width, with its x gradients divided by k. The textured full-screen
  effects stay 4:3: TD2's crash noise is not a single quad.
- The emulated cost of a draw is scaled back to the native 4:3 area (`hires_native_pixels`).
- A front buffer without a scaled target (no triangles since the option changed, or only 2D
  blits) is published enlarged and centred, so the frame size never changes from frame to
  frame; the frontend resizes the window only when the aspect ratio changes.
- The overlay is laid out 384 logical lines high, so the menus use the whole width.

**Speed** (Apple Silicon, GTI Club 2, 125 emulated seconds of attract and race): 1X 4:3 12.6 s,
16:9 14.6 s, 21:9 16.7 s; 2X 4:3 30.6 s, 16:9 37.3 s, 21:9 46.1 s.

**Known limits.** Elements that the original kept just off screen can show at the sides (TD2's
crash captions scrolling in). The in-car start camera of TD2 shows the edge of its cockpit
model. 21:9 has not been checked for missing scenery at the far edges, beyond the attract demos.

## 6. Current status (2026-09-29)

Thrill Drive 2 is working:
- full extraction from the CHD and recompilation of every module;
- boot matches MAME (the boot state sequence was checked against it);
- attract mode, coin-up, car/course selection, and a **playable race** at 30 fps with correct
  audio;
- the game's test menus (I/O CHECK, CALIBRATION, SOUND OPTIONS, …);
- automatic steering/pedal calibration on first launch;
- the SDL frontend at real-time speed.

Performance (Apple Silicon, M-series): 100 s of gameplay run in about 14.5 s of real time (about
7× real time) with 4 render threads. About 80 % of host time goes to the Voodoo rasteriser and
about 10 % to the recompiled code.

GTI Club: Corso Italiano **ver JAB** (`gticlub2`), tested headless with scripted inputs: boot,
attract mode, coin-up, selection (including the MT/AT choice), races, and the automatic
calibration including the motor test and the handbrake.

Driving Party / GTI Club 2 **ver EAA** (`gticlub2ea`), tested headless with scripted inputs:
- boot, attract mode (demo races, title, TOP 10), coin-up, car/course selection and races;
- TEST MODE and the automatic calibration;
- audio (music in attract mode).
90 s of emulated time run in about 10 s.

Thrill Drive 2 **ver JAA** (`thrild2j`) and **ver AAA** (`thrild2a`), tested headless with
scripted inputs: boot, attract mode, coin-up, car/course selection and races, and the automatic
calibration.
- The game code is the same as EBB's: same modules, function count and unresolved `gl`
  `bctr`s, with the `game` module's text 0x20 bytes longer.
- The cabinet is the GTI Club 2 JAB one: TEST MODE shows MOTOR TYPE K-TYPE and a HAND BRAKE
  item, and the steering calibration ends with the motor test. The profiles therefore use GTI
  Club 2 JAB's calibration script and handbrake input (AN3 at rest at -200).
- JAA needs 2 coins per credit and AAA 4, with the starting NVRAMs.
- **Region locks:** TEST MODE shows LANGUAGE DISPLAY and CURRENCY DISPLAY but cannot change
  them on JAA (Japanese, U.S. dollar) and AAA (English, H.K. dollar); EBB offers English and
  Italian, and euro, lire and pound. The game honours whatever the NVRAM holds: every language
  (0 Japanese, 1 English, 2 Italian) and currency works on every version. JAA's calibration
  therefore writes yen (`0x89` = 0), and its enhanced OPTIONS page offers all three languages.
- **Enhanced mode:** EBB's profile data applies unchanged (the attract hook `0x7DD40`, the
  FREE PLAY string `0xB8EE8`, the font file and the NVRAM fields are at the same addresses).
  The one difference: the TEST MODE main menu has no KONAMI INTERNET CHALLENGE item (EBB has it
  after BOOKKEEPING), so GAME MODE is item 12. Tested headless: first-launch setup, attract
  menu, OPTIONS (language applied and restarted), pause and the return to the attract mode.

Open:
- **TD2 JAA and AAA**: interactive play;
- **GTI Club 2**: interactive play (steering feel, handbrake); optional force feedback on a
  gamepad (rumble); the unmapped accesses at
  `0xFFE50000`/`0xFFE58000`/`0xFFE80000`/`0xFFE90000` during boot;
- **graphics**: the multibase texture fix removed the texture noise in the attract mode of TD2
  (street lights, trees, embankments) and GTI Club 2. Next, catalogue whatever glitches are
  left in play;
- cabinet settings stored in the starting NVRAM: "SOUND IN ATTRACT MODE: COMPLETE OFF", and
  BGM/SE volume at 3. This is why the default audio gain is ×16;
- Windows/Linux builds. First-run calibration uses `system()`, which needs adapting for
  Windows.

### MAME oracle (development tool)

MAME (`brew install mame`) runs the same sets; `roms/` can be passed directly as its rompath,
and `MAME_SET=<set>` selects the game (default `thrild2`). Its debugger, driven by Lua scripts
(`-debug -debugger none -autoboot_script`), provides reference values:
- `tools/mame_bp.sh` + `tools/mame_bp.lua`: breakpoints that print r0, r3–r8, LR, r1 and one
  extra expression (`MAMEBP=addr,… MAMEBP_EXTRA='d@addr'` or a register, e.g. `r31`). The
  recompiled-side equivalent is `RT_BP=addr,…` (plus `RT_BP_MEM`, `RT_BP_STOP`). It fires at
  function entries; for other instructions, recompile with `RECOMP_PROBES=addr,…` and build with
  `EXTRA=-DRT_TRACE`;
- `tools/mame_dumpram.lua`: dumps RAM at a given time;
- watchpoints (`wpset`) and video snapshots via Lua, to compare memory accesses and screens.

Recompiled-side tools:
- `RT_INPUT="t:port=hex,…"`: scripted inputs. Ports 0–7 are digital; 10–13 are signed analog;
- `RT_AN0..3`: force analog values;
- `RT_FPS_STATS=1`: counts distinct frames per emulated second;
- `RT_PROFILE=seconds`: a virtual-time profiler;
- `RT_I2C_LOG=1`, `RT_SC_LOG=1`, `RT_BP=…`;
- `tools/contact_sheet.py`: builds contact sheets from dumped frames.

Notes:
- In MAME the BIOS spends about 2.9 s more before starting the kernel; we skip that time.
- MAME's gdbstub does not support the MPC8240.

## 7. Next steps

1. Graphics: catalogue what is left after the multibase fix, against real
   references (gameplay videos or real hardware). MAME's core shares the same bugs, so it is not a
   reference for graphics.
2. Further rasteriser optimisation (SIMD, less contention); eventually a GPU backend.
3. CMake builds for Windows and Linux, CI.
4. Distributable package: the game data stays external and is extracted from the user's CHD.

## 8. Usage

```sh
brew install rom-tools sdl2     # chdman (Linux: mame-tools), SDL2
pip3 install --user capstone    # only for tools/ppcdis.py
make games                      # list the game profiles
make check   GAME=thrild2       # verify roms/ against the expected SHA1s
make -j8 game GAME=thrild2      # extract + recomp + build in one step
make extract GAME=thrild2       # roms/thrild2/ -> work/thrild2/
make recomp  GAME=thrild2       # work/thrild2/ -> generated/thrild2/*.c + game_config.h
make -j      GAME=thrild2       # ./td2   (GAME defaults to thrild2)
make distclean GAME=thrild2     # removes work/, generated/, build/ of that game and its saved NVRAM
python3 recomp/coverage.py thrild2 [module]   # coverage report
./td2                           # play (SDL window)
./td2 --headless --seconds 5    # headless run with log (testing)
RT_INPUT="12:3=fb,12.2:3=ff" ./td2 --headless ...   # scripted input (s:port=hex)
make EXTRA=-DRT_TRACE           # ring buffer of the last executed blocks, included in dumps
RT_MMIO_LOG=10000 RT_MMIO_RANGE=fe000000-feffffff ./td2   # log MMIO accesses
./td2 --frames DIR --frame-every 60   # dump video frames (PPM)
RT_SC_LOG=1 ./td2                     # log kernel syscalls
RT_VOODOO_LOG=1 ./td2                 # messages from MAME's Voodoo core
RT_VOODOO_TEXLOG=1 ./td2              # log each new texture setup (format, LODs, base registers)
RT_VOODOO_VRAMDUMP=vram.bin:1300 ./td2 --headless ...   # dump the whole VRAM at frame 1300
```
