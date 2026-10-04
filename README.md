# m2-sonic

Sega's **Sonic the Hedgehog** (Mega Drive) running on a **Sega Model 2B** board: MAME's Mega
Drive, as far as Sonic needs it, ported to the board's Intel i960KB, with the 68000
emulated (and its hot code statically recompiled), the picture on the Model 2's tilemaps
and the music re-voiced on its SCSP. It runs as a replacement for Sonic the Fighters
(`sfight`): three EPROMs are swapped, everything else on the board, or in the romset, is
stock.

This repository has the port and its documentation: the game (`src/`), its 68000 core,
the Mega Drive board, video and sound, the sound board program (`snd/`, Pac-Man's SCSP
relay) and the tools (`tools/`). It is built against
[m2-sdk](https://github.com/biggestsonicfan/m2-sdk), the bare Model 2 homebrew SDK
(headers, i960 boot code, linker script), checked out beside this repository, like
[m2-pacman](https://github.com/biggestsonicfan/m2-pacman).

- [docs/port.md](docs/port.md): how it works (68000 core and recompiler, board, tilemap
  video, timing, sound) and how each part was checked
- [docs/lockstep.md](docs/lockstep.md): the port against MAME's own Mega Drive, frame by
  frame: registers, memory, VDP, sound writes, pictures, audio
- [docs/mame.md](docs/mame.md): the MAME used and how to build it

## The three EPROMs

| File | What | Made from |
|---|---|---|
| `epr-19001.15`, `epr-19002.16` | i960 program | `src/sonic.c` + the cartridge |
| `epr-19021.31` | sound board 68000 program | `snd/scsp_passthru.s`, the SCSP relay (m2-pacman `docs/sound.md`) |

The cartridge is compiled into the program EPROMs; none of it is in the repository
(`tools/mdrom.py` reads your image). The program ROM is 1 MB, half of it the game.

## Building

Needs the i960-elf GCC toolchain (see m2-sdk's README) and Python 3; m2-sdk beside this
checkout (or `-DM2_SDK=<path>`).

```sh
python3 tools/mdrom.py Sonic_The_Hedgehog.bin     # -> src/sonic_rom.h (Sega data, gitignored)
tools/sonic_recomp.sh                             # -> src/sonic_recomp.h (optional: the recompiled hot
                                                  #    code; without it the game runs slower)
cmake -G "Unix Makefiles" -B build
make -C build -j2                                 # -> roms/sonic/
```

Checked with Sonic the Hedgehog (W) (REV00), 512 KB, sha1
`4c3b606bec697b0b934ba2f650a8b83c0e3b77c7`.

## Running

MAME's Model 2 driver: the three files in a folder named `sfight`, listed before the
folder with the stock `sfight.zip`, `schamp.zip` and `segabill.zip`:

```sh
mame sfight -rompath "/path/with/sfight-folder;/path/to/stock/roms"
```

MAME reports WRONG CHECKSUMS for exactly those three files; that is expected.

Controls (Model 2 -> Mega Drive pad): stick = D-pad, button 1 = A, 2 = B, 3 = C, START 1 =
START.

The text around the picture: `SCSP SOUND` when the sound EPROM answered (`NO SOUND` with the
stock one: the game runs silent), `SPEED` (100% = the Mega Drive's 59.92 Hz), pictures drawn
per second, and the i960's time per Mega Drive frame and per picture, in thousands of cycles
(its budget is 417 a frame).

## Status

- Runs in MAME (native and web) at 98% game speed and 36 pictures a second in play;
  busy scenes (Marble Zone, Spring Yard) slow to 75-92% at 12-15 pictures a second
  ([docs/port.md](docs/port.md), "Not done").
- Checked against MAME's own Mega Drive over 3000 frames ([docs/lockstep.md](docs/lockstep.md)):
  the 68000 runs cycle for cycle with MAME's (every VINT within 2-7 cycles), so registers,
  RAM, VRAM, CRAM, VSRAM, VDP registers and every sound-chip write are identical in all of
  them; every picture the port drew is pixel for pixel what its VDP state says.
- Not done: Labyrinth Zone's water colours (per-line palette), the SEGA voice at boot.
- Not yet tried on real hardware or in m2emulator.
