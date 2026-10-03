# Lockstep with MAME's Mega Drive

`tools/mdlockstep` runs the same cartridge twice under one MAME: once on MAME's own Mega
Drive (`genesis`, the reference) and once as the port (`sfight` with the port's EPROMs),
both on the same input script, and compares them frame by frame.

```sh
MAME=~/build/mame-bin/mame-shared/shared CART=Sonic_The_Hedgehog.bin ROMS_M2=/path/to/stock/roms \
GAME=build LS_FRAMES=3000 tools/mdlockstep/run.sh
```

## Watching it

`tools/mdlockstep/live.sh` (same variables, `DISPLAY` set, no `LS_FRAMES`) opens the two
side by side instead of recording: MAME's `genesis` with the cartridge on the left, the
port under `sfight` on the right, both playing `inputs.lua`. `live.lua` keeps them on the
same frame: at each VINT a side writes its count to `$LIVE_DIR/<side>.cnt` and the one
ahead waits for the other. Each screen shows its frame number. The pair runs at about 52
frames a second. The port shows ~18 pictures a second, so its picture can be a frame or
two behind its own frame number. Close either window and the other runs on alone after
10 seconds.

## Checking it live

While the two run, `live.lua` hashes both sides at every VINT and `live_check.py` compares
them. Its verdict shows on both screens under the frame number (green: the same; red:
`DIFF`), and each difference goes to `$LIVE_DIR/check.log`. Hashes are FNV-1a 64, folded
to 32 bits. Each side writes `$LIVE_DIR/<side>.rec`:

- **State per frame:** the 68000's registers, normalised as in `compare.py`. Work RAM in
  16 blocks of 4 KB, plus the live stack. VRAM in 16 blocks. CRAM, VSRAM and the VDP
  registers. The frame's YM2612/PSG writes and Z80 commands.
- **The picture:** at MAME's levels mapped to the Mega Drive's 3 bits a channel, so the
  two palettes compare. The port draws only some frames; each one it draws is checked
  against MAME's picture of the same frame.
- **The sound:** the reference's chip writes, the SCSP register writes the Model 2's
  sound board makes, and each side's loudness per frame. `live_check.py` turns both into
  notes per voice (FM1-6, PSG1-3, noise, drums), each a pitch in cents. A note matches
  when the Model 2 plays it within 2 frames early to 45 late, ±50 cents.

```sh
# 1500 frames, no windows, as fast as they go (~70 frames a second), then the report
LS_FRAMES=1500 CART=... ROMS_M2=~/build/mameroms GAME=build tools/mdlockstep/live.sh
```

- `LS_FRAMES`: no windows; stop at that frame and print the report (also `report.txt`).
- `LIVE_CHECK=0`: only the lockstep, without the hashes.
- `LIVE_PICS=a-b`: save both pictures of frames a-b as `<side>_NNNNN.rgb` (320x224 RGB).
- `LIVE_DBG=1`: log the bytes the port has queued for the sound board at each frame.
- `python3 tools/mdlockstep/live_check.py $LIVE_DIR` reports on a finished run.

Results, 3000 frames (title, Green Hill, measured 2026-10-03):

| what | the same |
|---|---|
| everything (registers, RAM, VRAM, CRAM, VSRAM, VDP, chip writes) | 3000 of 3000 frames |
| chip writes | 3000 of 3000 |
| pictures | 996 of 1003 |
| notes | 2108 of 2251, none extra; FM all, PSG2 all, noise 225/226, PSG1 417/546, drums 152/165 |
| when | median 1 frame late |

- The 7 differing pictures (472, 1497, 1905, 1987, 2069, 2151, 2429) are not explained yet;
  the state they are drawn from is identical.
- PSG1's missing notes are fast sweeps (a step a frame). The UART carries about 50 bytes a
  frame, so two steps sometimes reach the SCSP in the same frame.
- Drums: their samples stream over the same line for the first ~340 frames, and the port
  skips a drum not uploaded yet. The matcher then pairs a skipped drum with the next one,
  which is where the 12-16 frame "late" group in the report comes from.

The check found two bugs in the port's sound, now fixed:

- The relay queue's head could pass its tail, and the UART then sent the whole 4 KB ring
  again. It replayed old key-ons, and key-offs were lost behind it. Cause: the SDK's
  `m2_irq_off()` has no `"memory"` clobber, so GCC read the head before masking
  (`src/m2_scsp.h`).
- A new instrument's wave (64 writes) used to go out in one burst, delaying the key-ons
  behind it by up to 9 frames. It now goes out a few words a frame, and the key-on plays
  the sine until it is in. PSG tones are now sent once a frame, not half-written.

## What is recorded

Frames are counted by VINTs taken, on both sides; the pad (`inputs.lua`: title, START, then
a fixed pseudo-random walk to the right with jumps) is set at the k-th level-6 acknowledge,
so both read the same pad in the same frame.

- **Reference** (`ref.lua`): at each VINT acceptance the 68000's registers, work RAM, VRAM,
  CRAM, VSRAM and VDP registers; every YM2612/PSG write and Z80 command by the 68000;
  the picture; the audio (`-wavwrite`).
- **Port** (`port.lua`): the port records the same state into `md_irq_snap` when it takes
  a VINT; the script reads it and the emulated memories out of the i960's RAM (symbols from
  `game.elf`), the chip writes from the port's sound queue, and the Model 2 picture. It
  skips frames where the i960 is still busy (`sonic_busy`) and takes the picture one
  notifier later (MAME's `pixels()` returns the previous frame).
- **compare.py** normalises what may legitimately differ: SR as MAME shows it after the
  acknowledge, the idle loop (`WaitForVBla`: the two may wait on different instructions of
  it), the dead stack below SP. Pictures are checked against `render.c` (the reference
  renderer, `src/md_render.h`) run on each side's own VDP state, since the Model 2 draws
  only some of the frames.

## Results (3000 frames, attract mode and play, measured 2026-10-03)

| What | Identical |
|---|---|
| interrupts | 3000 VINTs on both (no HINTs in these scenes) |
| 68000 registers at VINT | 3000 of 3000 frames |
| work RAM, VRAM, CRAM, VSRAM, VDP registers | 3000 of 3000 |
| YM2612 / PSG writes, Z80 commands | 3000 of 3000 frames, same writes in the same order (8501 / 4906 / 165) |
| port pictures vs its VDP state | 86 of 86 |
| MAME pictures vs its VDP state | 248 of 248 |

### Cycle timing

Both sides record the 68000's time at each interrupt acknowledge: MAME's cycle count
(`device.total_cycles`, from claude_mame's `md-lockstep` branch, in the shared build) in
`ref_iack.bin`, the port's `md_now()` in `port_iack.bin`, both from power-on. The port is now
2-7 cycles behind MAME (median 3) at every one of the 3000 acknowledges: what is left is
MAME's E-clock wait (`vpa_sync` puts autovector acknowledges on a 10-cycle grid), which
the port does not model.

Getting there took, in order (each found with per-instruction traces of both, in MAME
with a Lua tap on the bus, in the port with `tools/mdhost.c -t`):

- **MAME's 68000 times, not Musashi's:** `tools/optiming` runs every opcode on both
  (`docs/port.md`); `tools/m68k_cyc_mame.py` corrects the table.
- **The clock:** MAME's 68000 runs at the integer 7670453 Hz, not MCLK/7, its reset
  exception takes 34 cycles, and it takes VINT after the first instruction ending 149
  master clocks into line 224.
- **The idle skip and the recompiler** must not move the interrupt: the skip puts it where
  the skipped loop would have been, and the last 200 cycles before VINT run on the
  interpreter, which can stop on any instruction.
- **The Z80's busy flag** (`src/md_z80.h`, `docs/port.md`): during a drum the sound driver
  polls `$A01FFD`; once a poll comes out differently, the 68000 is ~140 cycles off, and in
  a zone load (frames 636-650) that put the VINT into a different loop iteration of the
  decompressor. The model follows MAME's scheduler, which completes a Z80 access that falls
  past the 68000's time in the Z80's next timeslice (the next scanline timer); it was fitted
  offline against MAME's own traces (817 of 817 bus requests and every `$1FFD`/`$1FFF`
  write).

`hostrec.c` stamps chip writes with the frame at the end of `md_frame()`, so writes made
between that and the VINT land a frame late in its report (2415 of 3000 frames the same,
all of them shifted, not different); the live check and `run.sh` stamp them as they are made.

Fixes this found (now in the port): the YM2612 busy flag (192 cycles, ymfm) and the Z80-bus
wait state, which had put the whole game one frame behind from boot; MAME's sprite masking
and 320-pixel line budget; mixed-palette cells of more than 15 colours overwriting the next
group's palette; the palette applied with the composite swap.

## Audio

The port re-voices the chips on the SCSP, so its audio cannot match sample for sample.
`compare.py` lines the two recordings up (the port trails by 1.5 s: the sound board boots)
and compares loudness (correlation 0.61) and the spectrum per second (median similarity
0.42). The register writes above are the real check; `docs/port.md` has the pitch check.
The live check (above) matches the notes, frame by frame.
