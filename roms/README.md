# roms/

Put your own dumps here. The layout is the same as a MAME rompath. Git ignores everything in
this folder except this file and the `README.txt` files.

Each subfolder has a `README.txt` listing the files it expects, with their SHA1s. These text
files are generated from the game profiles (`python3 tools/game.py roms-readme`).

| Folder | Game | MAME set |
|---|---|---|
| `kviper/` | Viper BIOS, shared by every game | `kviper` |
| `thrild2/` | Thrill Drive 2 (ver EBB, Europe) | `thrild2` |
| `thrild2j/` | Thrill Drive 2 (ver JAA, Japan) | `thrild2j` |
| `thrild2a/` | Thrill Drive 2 (ver AAA, Asia) | `thrild2a` |
| `thrild2c/` | Thrill Drive 2 (ver EAA, Europe; the only known CF dump is bad) | `thrild2c` |
| `gticlub2/` | GTI Club: Corso Italiano / GTI Club 2 / Driving Party (ver JAB, Japan) | `gticlub2` |
| `gticlub2ea/` | Driving Party: Racing in Italy / GTI Club 2 / GTI Club: Corso Italiano (ver EAA, Europe) | `gticlub2ea` |

You only need `kviper/` plus the folders of the games you want to build. `thrild2j` and
`thrild2a` share the same CF card (`a41a02.chd`): one copy in either folder is enough.
