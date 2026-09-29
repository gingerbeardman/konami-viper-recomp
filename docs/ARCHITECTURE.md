# Konami Viper: static recompilation

Goal: native executables for macOS, Windows and Linux (x86-64 and arm64) that run Konami Viper
games without emulating the CPU. The PowerPC code is translated to C at build time. The board
hardware (Voodoo3, EPIC, CF, audio, I/O) is reimplemented by a runtime shared by every game.

The reference game is Thrill Drive 2 **ver EBB**: most of this document was reverse engineered
on it. Other games and versions run on the same board with the same BIOS and have profiles in
`games/`:
- **Other Thrill Drive 2 versions:** `thrild2j` (ver JAA) and `thrild2a` (ver AAA) share the
  CF card `a41a02`; `thrild2c` (ver EAA) has a CF dump that MAME marks bad, and an NVRAM that
  has never been dumped.
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
  emulated seconds, or `null`.

`recomp.py` turns the profile into `generated/<id>/game_config.h` (`GAME_*` macros). The runtime
is compiled once per game against it; there is no runtime game switch. The unverified profiles
inherit TD2's module list and hints, with calibration disabled. The differences MAME documents (K-type 8-bit wheel
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
| Steer | ← → | left stick |
| Accelerator / brake | ↑ / ↓ | R2 / L2 (or A / B) |
| Shift up / down | A / Z | R1 / L1 |
| Coin / Start | 5 / 1 | Back / Start |
| Test / Service | F2 / 9 | — |
| Fullscreen / Quit | F11 / Esc | — |

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

Open:
- **other versions**: the other TD2 versions, waiting for the files;
- **GTI Club 2**: interactive play (steering feel, handbrake); optional force feedback on a
  gamepad (rumble); the unmapped accesses at
  `0xFFE50000`/`0xFFE58000`/`0xFFE80000`/`0xFFE90000` during boot;
- **graphical glitches** seen in-game. We still need to find out which of them are artefacts
  already present in MAME's Voodoo core. References: real hardware and gameplay videos;
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

1. Other TD2 versions: extract, recompile and boot them.
   - Check the `gl` dispatcher addresses (`python3 recomp/coverage.py <id> gl` lists the
     unresolved `bctr`s) and set the hints.
   - Check that TD2's calibration script works.
2. Graphical glitches: catalogue them, then compare with MAME and with real references
   (gameplay videos or real hardware). Fix the Voodoo core where it is wrong.
3. Further rasteriser optimisation (SIMD, less contention); eventually a GPU backend.
4. CMake builds for Windows and Linux, CI.
5. Distributable package: the game data stays external and is extracted from the user's CHD.

## 8. Usage

```sh
brew install rom-tools sdl2     # chdman (Linux: mame-tools), SDL2
pip3 install --user capstone    # only for tools/ppcdis.py
make games                      # list the game profiles
make check   GAME=thrild2       # verify roms/ against the expected SHA1s
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
```
