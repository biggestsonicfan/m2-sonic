# Lockstep with MAME's Mega Drive

`tools/mdlockstep` runs the same cartridge twice under one MAME: once on MAME's own Mega
Drive (`genesis`, the reference) and once as the port (`sfight` with the port's EPROMs),
both on the same input script, and compares them frame by frame.

```sh
MAME=~/build/mame-bin/mame-shared/shared CART=Sonic_The_Hedgehog.bin ROMS_M2=/path/to/stock/roms \
GAME=build LS_FRAMES=6000 tools/mdlockstep/run.sh
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

## Results (6000 frames, attract mode and play)

| What | Identical |
|---|---|
| interrupts | 6000 VINTs on both (no HINTs in these scenes) |
| 68000 registers at VINT | 5973 of 6000 frames |
| work RAM | 5989 of 6000 |
| VRAM | 5996 of 6000 |
| CRAM, VSRAM, VDP registers | 6000 of 6000 |
| YM2612 / PSG writes, Z80 commands | 6000 of 6000 frames, same writes in the same order (14065 / 7566 / 243) |
| port pictures vs its VDP state | 121 of 121 |
| MAME pictures vs its VDP state | 469 of 498 (the other 29 up to 3936 pixels; not explained yet) |

The differences are transients, in busy frames (loading a zone, frames 24 and 646-650):
the VINT lands a few instructions earlier or later than on MAME, so it catches a loop in a
different iteration (registers, a few words of the object table at `ffa500` and the stack),
and a DMA/copy is at a different point (VRAM `c600`-`ed00`). The next frames agree again.
The host interpreter (`tools/mdhost.c`) does the same, so it is not the recompiler.

### Interrupt timing (3000 frames, measured 2026-10-02)

Both sides now record the 68000's time at each interrupt: MAME's cycle count at the
acknowledge bus cycle (`device.total_cycles`, from claude_mame's `md-lockstep` branch, in the
shared build) in `ref_iack.bin`, the port's `md_now()` as it takes the interrupt in
`port_iack.bin`. Both count from power-on, so the times compare directly:

- Frame lengths agree: MAME's acknowledges come 128000 / 128010 cycles apart (`vpa_sync`
  puts every autovector acknowledge on the E clock's 10-cycle grid) and the port's 128005 /
  128006; both average 128005.7 (262 x 3420 / 7).
- MAME acknowledges 22-36 cycles after the port takes the VINT (median 25, within 10 of that
  in 2164 of 3000 frames): the VDP raises VINT 18 cycles into line 224, then the 68000
  finishes its instruction, starts the exception and waits for the E edge. The port takes it
  exactly at line 224 (cycle 109440 of the frame) because it skips `WaitForVBla` idling.
- The register differences are **not** this lead. With `MD_VINT_DELAY=18` and with
  `MD_VINT_DELAY=18 MD_E_PHASE=0` the same 27 frames differ, in the same way. They are frame
  24 (boot), 476 and 626-650 (a zone loading). In those frames the VINT interrupts
  decompression code (`0x17ac`-`0x1928`, `0x6ae6`-`0x6c12`) with the port a loop iteration
  or two ahead or behind, so per-instruction timing differs. The port's 68000 times
  instructions like Musashi, while MAME's `genesis` uses MAME's newer 68000 core, which
  times some instructions differently.
  The state converges by frame 651 and nothing visible or audible differs.

So `MD_VINT_DELAY` / `MD_E_PHASE` / `MD_E_ADJ` stay off (0 / -1 / 0). Matching those frames
would mean matching MAME's 68000 core cycle for cycle, which buys nothing on screen.

Fixes this found (now in the port): the YM2612 busy flag (192 cycles, ymfm) and the Z80-bus
wait state, which had put the whole game one frame behind from boot; MAME's sprite masking
and 320-pixel line budget; mixed-palette cells of more than 15 colours overwriting the next
group's palette; the palette applied with the composite swap.

## Audio

The port re-voices the chips on the SCSP, so its audio cannot match sample for sample.
`compare.py` lines the two recordings up (the port trails by 1.5 s: the sound board boots)
and compares loudness (correlation 0.61) and the spectrum per second (median similarity
0.42). The register writes above are the real check; `docs/port.md` has the pitch check.
