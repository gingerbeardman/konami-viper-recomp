# Konami Viper — static recompilation

Native ports of Konami arcade games for the **Viper** hardware, built by _statically
recompiling_ the original PowerPC code into C. The game's own code runs on your machine as
native code; the board hardware (Voodoo3 graphics, interrupt controller, CF card, sound, I/O,
timekeeper) is reimplemented by a small runtime shared by every game.

This is not an emulator: no PowerPC instruction is interpreted at runtime. The recompiler
translates the game at build time, from files you supply yourself.

**This repository contains no game code or data.** You must provide your own dumps of the game
and of the board BIOS (see [Required files](#required-files)). The build extracts and
recompiles them locally. The resulting files (`work/`, `generated/`, the executables) are derived
from copyrighted material and must not be redistributed.

## Games

| Game | Version | `GAME=` | Executable | Status |
| --- | --- | --- | --- | --- |
| **Thrill Drive 2** (2001) | EBB (Europe) | `thrild2` | `./td2` | Playable |
| **Thrill Drive 2** | JAA (Japan) | `thrild2j` | `./td2j` | Not yet verified |
| **Thrill Drive 2** | AAA (Asia) | `thrild2a` | `./td2a` | Not yet verified |
| **Thrill Drive 2** | EAA (Europe, older) | `thrild2c` | `./td2c` | Not yet verified; the only known CF dump is bad |
| **GTI Club: Corso Italiano** (2000), also known as _GTI Club 2_ and _Driving Party: Racing in Italy_ | JAB (Japan) | `gticlub2` | `./gticlub2` | Not yet verified |
| **Driving Party: Racing in Italy** (2000), also known as _GTI Club 2_ and _GTI Club: Corso Italiano_ | EAA (Europe) | `gticlub2ea` | `./gticlub2ea` | Not yet verified |

The `GAME=` value is the name of the game's MAME set.
- **Profiles:** each version is described by a profile in `games/<id>/game.json`, which lists
  the expected files, the modules to recompile, the input defaults and the calibration.
- **Shared settings:** versions of the same game inherit the main profile and override only
  what differs.
- **Listing:** `make games` lists the profiles.

**Thrill Drive 2**
- Boots like the original board; attract mode, coin-up, menus and test mode work.
- Fully playable at the original 30 fps, with correct sound.
- Steering and pedals are calibrated automatically on first launch.
- Known issues: some graphical glitches, which are still being investigated. Many of them are
  also present in MAME's Voodoo core, which this project uses.

**Other Thrill Drive 2 versions**: same game engine, so they are expected to work like EBB.
They still have to be verified, and their automatic calibration is disabled until then.
- JAA and AAA share the same CF card; the region comes from the NVRAM.
- MAME uses the GTI Club 2 control layout (K-type wheel) for JAA and AAA.
- For EAA, MAME marks the only known CF dump as bad (blue screens), and its NVRAM has never
  been dumped: that version starts from an empty NVRAM.

**GTI Club 2**: the profiles exist, but the game has not been extracted or run yet.
- Expect the control mapping (K-type steering wheel, handbrake) and the automatic calibration
  to need work.
- For the EAA version, MAME notes that a DIP switch must be set, otherwise the game asks for a
  password. The profile already sets it.

**Platforms:** tested on macOS (Apple Silicon). The code targets x86-64 and arm64. Linux should
build with the same Makefile but has not been tested yet. Windows is not supported yet.

Technical details (boot chain, file formats, recompiler and runtime design) are in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Required files

Put your files in `roms/`, which has the same layout as a MAME rompath. You only need
`kviper/` plus the folder of the game you want to build. Each folder contains a `README.txt`
listing the files it expects:

```
roms/
├── kviper/        941b01.u25, ds2430.u3             Viper BIOS set, shared by every game
├── thrild2/       a41b02.chd, a41ebb_nvram.u39      Thrill Drive 2 (ver EBB)
├── thrild2j/      a41a02.chd, a41jaa_nvram.u39      Thrill Drive 2 (ver JAA)
├── thrild2a/      a41a02.chd, a41aaa_nvram.u39      Thrill Drive 2 (ver AAA)
├── thrild2c/      a41c02.chd                        Thrill Drive 2 (ver EAA)
├── gticlub2/      941b02.chd, 941jab_nvram.u39      GTI Club: Corso Italiano (ver JAB)
└── gticlub2ea/    941a02.chd, 941eaa_nvram.u39      Driving Party: Racing in Italy (ver EAA)
```

| File | What it is | SHA1 |
| --- | --- | --- |
| `kviper/941b01.u25` | Viper board BIOS (GM941B01) | `66ff268d5bf78fbfa48cdc3e1b08f8956cfd6cfb` |
| `kviper/ds2430.u3` | DS2430A 1-Wire EEPROM | `ed7cd9b2763b3e377df9663943160f9871f65105` |
| `thrild2/a41b02.chd` | Thrill Drive 2 (EBB) CF card | `0426f4bb9001cf457f44e2c22e3d7575b8049aa3` |
| `thrild2/a41ebb_nvram.u39` | Thrill Drive 2 (EBB) NVRAM (M48T58) | `e14ea2ba95b72edf0a3331ab82c192760bfdbce3` |
| `thrild2j/` or `thrild2a/` `a41a02.chd` | Thrill Drive 2 (JAA/AAA) CF card, shared: one copy is enough | `bbb71e23bddfa07dfa30b6565a35befd82b055b8` |
| `thrild2j/a41jaa_nvram.u39` | Thrill Drive 2 (JAA) NVRAM | `085f40816befde993069f56fdd5f8bd6ccfcf301` |
| `thrild2a/a41aaa_nvram.u39` | Thrill Drive 2 (AAA) NVRAM | `768bcd46a6ad20948f60f5e0ecd2f7b9c2901061` |
| `thrild2c/a41c02.chd` | Thrill Drive 2 (EAA) CF card (bad dump) | `ab3020e8709768c0fd2467573e92b679a05944e5` |
| `thrild2c/941eaa_nvram.u39` | Thrill Drive 2 (EAA) NVRAM: never dumped, optional | — |
| `gticlub2/941b02.chd` | GTI Club 2 (JAB) CF card | `943bc9b1ea7273a8382b94c8a75010dfe296df14` |
| `gticlub2/941jab_nvram.u39` | GTI Club 2 (JAB) NVRAM | `2753dda42cdd81af22dc6780678f1ddeb3c62013` |
| `gticlub2ea/941a02.chd` | GTI Club 2 (EAA) CF card | `dd180ad92dd344b38f160e31833077e342cee38d` |
| `gticlub2ea/941eaa_nvram.u39` | GTI Club 2 (EAA) NVRAM | `92e0ce01049308f459985d466fbfcfac82f34a47` |

- **CHD hashes:** for a CHD, the hash is the CHD's internal SHA1, as shown by `chdman info`.
- **Checking your files:** `make check GAME=<id>` compares your files with this table. It also
  runs automatically before extraction.
- **Files you don't need:** `941a01.u25`, the other BIOS revision in `kviper`, is not used.
- **"Needs redump" is fine:** MAME flags `ds2430.u3` and the GTI Club 2 NVRAM dumps as "needs
  redump". This is expected.

## Prerequisites

- A C11 / C++20 compiler (Clang or GCC) and `make`
- Python 3 (standard library only)
- SDL2 (the `sdl2-config` script must be on your `PATH`)
- `chdman`, from MAME's tools

On macOS (Homebrew):

```sh
brew install sdl2 rom-tools
```

On Debian/Ubuntu (untested):

```sh
sudo apt install build-essential python3 libsdl2-dev mame-tools
```

## Build

```sh
make extract GAME=thrild2     # roms/thrild2/ -> work/thrild2/       (CF image, kernel, game modules)
make recomp  GAME=thrild2     # work/thrild2/ -> generated/thrild2/  (recompiled PowerPC code)
make -j8     GAME=thrild2     # generated/thrild2/ + runtime/ -> ./td2
```

Run the three steps in this order: `make` needs the files that `make recomp` produces.
`GAME` defaults to `thrild2`. Each game builds into its own `work/<id>/`, `generated/<id>/` and
`build/<id>/`, so you can build several games side by side.

`make clean GAME=<id>` removes the build objects and the executable. `make distclean GAME=<id>`
also removes `work/<id>/`, `generated/<id>/` and the saved NVRAM, which contain everything
derived from that game's data.

## Run

```sh
./td2
```

**First launch:** if the game has an automatic calibration (currently Thrill Drive 2), it runs
before the window opens.
- The game's TEST MODE calibration runs in the background and takes a few seconds.
- The result is saved to `<executable>_nvram.bin` (for example `td2_nvram.bin`).
- The game then starts with steering and pedals centred. To redo the calibration, delete that
  file.

The game settings (volume, difficulty and so on) are saved to the same file. They are written
every 60 seconds and on exit.

### Controls

| Action          | Keyboard | Gamepad    |
| --------------- | -------- | ---------- |
| Steer           | ← / →    | Left stick |
| Accelerator     | ↑        | R2 (or A)  |
| Brake           | ↓        | L2 (or B)  |
| Handbrake (GTI Club 2 only) | Space    | X          |
| Shift up / down | A / Z    | R1 / L1    |
| Insert coin     | 5        | Back       |
| Start           | 1        | Start      |
| Test / Service  | F2 / 9   | —          |
| Fullscreen      | F11      | —          |
| Quit            | Esc      | —          |

### Options

```
./td2 --scale 3          window scale (default 2)
./td2 --volume 8         audio gain (default 16)
./td2 --headless --seconds 30        no window/audio, runs as fast as possible (testing)
./td2 --frames DIR --frame-every 60  dump video frames as PPM (with --headless)
./td2 --help             all options
```

The environment variables for debugging are described in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Repository layout

| Path              | Contents                                                                                                                             |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `games/`          | One profile per game (`game.json`): required files and SHA1s, modules, hints for hand-written code, inputs, calibration              |
| `roms/`           | Where you put your own dumps (ignored by git)                                                                                        |
| `tools/`          | Asset pipeline: CHD → FAT16 → game file → Konami LZSS / internal filesystem; game profiles; development tools (MAME debugger scripts, disassembler) |
| `recomp/`         | Static recompiler: PowerPC 603e decoder, control-flow analysis (including jump tables), C emitter                                    |
| `runtime/`        | Runtime shared by every game: CPU context and exceptions, kernel task switching, event scheduler, Viper hardware, SDL frontend       |
| `runtime/voodoo/` | Voodoo3 core from MAME, with a small compatibility layer                                                                             |
| `docs/`           | Architecture and reverse-engineering notes                                                                                           |

## Legal

- This is an unofficial, non-commercial fan project for preservation and research. It is not
  affiliated with, endorsed by, or sponsored by Konami.
- "Konami", "Thrill Drive", "GTI Club" and "Driving Party" are trademarks of Konami Group
  Corporation. All other trademarks belong to their owners.
- This repository contains **no copyrighted game code, ROM, BIOS, CHD or NVRAM data**. The build
  extracts the game from files you supply and translates it locally.
- You may use this project only with dumps you are legally entitled to use, for example dumps
  of hardware you own. Do not ask for game files in this project's issues or discussions, and
  do not link to them.
- Do not redistribute anything the build produces from the game data: `work/`, `generated/`,
  the executables (`td2`, `td2j`, `gticlub2`, …) and the `*_nvram.bin` files. These will contain
  Konami's copyrighted code and data.
- **License:** the original code in this repository is released under the
  [BSD-3-Clause license](LICENSE). The license covers only this project's code. It grants no
  rights to the games.
- **Third-party code:** `runtime/voodoo/` contains the 3dfx Voodoo emulation core, `poly.h` and
  `rgbutil` from [MAME](https://github.com/mamedev/mame).
  - This code is distributed under the BSD-3-Clause license, and its copyright belongs to its
    original authors (see the header of each file).
  - Some device logic in `runtime/hw.c` follows MAME's implementations: the M48T58 timekeeper
    (`timekpr.cpp`), the Konami K056230 LANC and `konami/viper.cpp`. That logic is also covered
    by BSD-3-Clause.
  - MAME is used here as a hardware reference.
- The software is provided "as is", without warranty of any kind.
