# Lockstep with MAME's Mega Drive

`tools/mdlockstep` runs the same cartridge twice under one MAME: once on MAME's own Mega
Drive (`genesis`, the reference) and once as the port (`sfight` with the port's EPROMs),
both on the same input script, and compares them frame by frame.

```sh
MAME=~/build/mame-bin/m2md/m2md CART=Sonic_The_Hedgehog.bin ROMS_M2=/path/to/stock/roms \
GAME=build LS_FRAMES=6000 tools/mdlockstep/run.sh
```

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
The host interpreter (`tools/mdhost.c`) does the same, so it is not the recompiler: it is
the interrupt timing against MAME. What is known of it:

- MAME's frame is 128005.7 68000 cycles; its IACKs come 128010 / 128000 cycles apart
  (`vpa_sync`: autovectors wait for the E clock's 10-cycle grid).
- MAME raises VINT 32 x 4 master clocks after the line starts (18 68000 cycles).

`md_hw.h` has both as experiments, off by default: `MD_VINT_DELAY` and
`MD_E_PHASE`/`MD_E_ADJ`. They are not finished; with them set the frames do not yet all
agree.

Fixes this found (now in the port): the YM2612 busy flag (192 cycles, ymfm) and the Z80-bus
wait state, which had put the whole game one frame behind from boot; MAME's sprite masking
and 320-pixel line budget; mixed-palette cells of more than 15 colours overwriting the next
group's palette; the palette applied with the composite swap.

## Audio

The port re-voices the chips on the SCSP, so its audio cannot match sample for sample.
`compare.py` lines the two recordings up (the port trails by 1.5 s: the sound board boots)
and compares loudness (correlation 0.61) and the spectrum per second (median similarity
0.42). The register writes above are the real check; `docs/port.md` has the pitch check.
