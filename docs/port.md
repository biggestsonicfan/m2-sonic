# The port in detail

## How it works

- **68000:** `src/m2_m68k.h`, an interpreter with Musashi's flags and MAME's cycle counts
  (`src/m2_m68k_cyc.h`: Musashi's per-opcode table, 5.5 KB, corrected to MAME's microcoded
  68000 by `tools/m68k_cyc_mame.py`; MUL/DIV and the bit ops' data-dependent times as MAME
  has them). Checked against Musashi one instruction at a time (`tools/mdlock`, below), and
  timed opcode by opcode against MAME (`tools/optiming`). The hot code is statically recompiled
  (`tools/m68krecomp.py`, the pattern of Pac-Man's `z80recomp.py`): the 4200 most run
  instructions (97.8% of what runs) become C with constant operands, flags computed only
  where read, gotos between blocks and a hash for returns and indirect jumps; everything
  else runs on the interpreter. An idle skip ends the frame in the game's WaitForVBla loop.
- **Board:** `src/md_hw.h`: memory map, the VDP's ports, DMA, VINT/HINT, 3-button pads, as
  MAME's `megadriv.cpp` and `315_5313.cpp`. YM2612 and PSG writes are queued for the sound
  code. The YM2612 reads busy for 192 cycles after a write (ymfm), and each 68000 access to
  the Z80 bus costs a wait state, as in MAME: Sonic's sound driver waits on that flag, so
  without it the port ran ahead of MAME at boot (found by the lockstep, [lockstep.md](lockstep.md)).
- **Timing, cycle for cycle with MAME's `genesis`:** the 68000 at MAME's integer clock
  (7670453 Hz, so a line is 488.57 cycles and the frame drifts against MCLK/7 as MAME's
  does); 34 cycles of reset exception before the first instruction; VINT taken after the
  first instruction that ends 149 master clocks or more into line 224 (MAME's traces put it
  between 148.05 and 149.6). The recompiled code checks its cycles only at branches, so the
  last 200 cycles before VINT run on the interpreter (`MD_VINT_TAIL`), and the idle skip
  puts the interrupt where the skipped `tst.b`/`bne.s` loop would have been. A 68000 -> VRAM
  DMA stops the 68000 as MAME's does.
- **Z80:** not emulated, but its timing is: `src/md_z80.h` models the game's DAC driver
  (Z80 RAM 0-0xff) instruction by instruction, in MAME's attoseconds, only for what the 68000
  can see: the busy flag at `$A01FFD` and the command byte at `$A01FFF`. It follows MAME's
  scheduler: the Z80 runs up to the 68000's bus cycle on its own clock edges, an access
  falling past the edge completes in its next timeslice (the next scanline timer or bus
  request), and it stops while the 68000 holds the bus. Sonic's sound driver polls `$A01FFD`
  before writing the YM2612 while a drum plays, so this decides how long it waits (frames
  636-650 differed without it). Whole poll and sample loops are skipped in one step: with
  it the port still draws as many pictures (1003 in 3000 frames; 985 before).
- **Video:** `src/md_s24.h` puts the picture on the Model 2's System 24 tilemaps: plane B and
  plane A on the two scrolling layers (the hardware does the per-line scroll), each
  (pattern, flip, palette line) as its own char (the char number fixes the palette), the
  sprites composited into plane A's cells: one-line cells as they are, mixed ones with their
  colours in shared 15-colour banks (a rare cell of more than 15 takes the nearest). The
  sprites follow MAME's line rules: its masking (a sprite at x = 0 and one at 0 < x < 0x40)
  and 320 sprite pixels a line. Composites are built in one half of a group pool while
  the other half is on screen. The 320x224 picture is at (88,80) of
  the 496x384 screen. A picture is built (`s24_build`: scroll values, sprites, composites)
  without touching what is shown, then put on screen in one pass (`s24_swap`: patterns,
  name tables, colours, scroll, composites), so the HUD (sprites composited into scrolling
  plane A) never meets a scroll of another frame. MAME draws the screen at the end of
  vblank, 41000 timer ticks after the interrupt; timer 2, reloaded at each vblank
  (`src/i_handle.s`), says where the i960 is, and `sonic.c` waits when the swap
  (`s24_swap_cost` plus its recent error) would span that moment.
- **Timing:** the Mega Drive runs at 59.92 Hz, the Model 2 at 57.52 Hz; each vblank runs the
  frames due (up to 4) and draws once, so busy scenes draw fewer pictures instead of slowing
  down.
- **Sound:** the serial line to the sound board carries ~50 bytes a frame, so the chips are
  re-voiced, not emulated (`src/md_snd.h`): each FM channel plays a wavetable of its
  instrument rendered on the i960 (all 8 algorithms, feedback, multipliers; modulators at
  their sustain level; cached in sound RAM), with the carrier's envelope on the SCSP's; the
  PSG plays a square wave and the SCSP's noise; the drums are decoded from the Z80 DAC
  driver the game loads into Z80 RAM and played at its rate. The UART is fed by its
  interrupt (board IRQ bit 10, `m2_scsp_irq_start`; `src/i_handle.s` is the SDK's with the
  vector-15 handler calling it), so a busy i960 still sends at full rate. The interrupt is
  enabled only while bytes wait: an idle i8251 keeps TxRDY up and MAME's signals it again at
  every bit clock, so left on it interrupted the i960 ~480 times a frame for nothing (11% of
  its time in busy scenes, 59% game speed at worst in the attract mode; 69% without).

## How it was checked

| What | How | Result |
|---|---|---|
| 68000 core | `tools/mdlock`: every instruction (and interrupt) run again on Musashi, its bus replaying the core's accesses | 199M instructions (30000 frames of the attract mode) and 3000-frame plays of 3 zones: registers, SR, both SPs and every write identical (cycles too, before they were moved to MAME's: `-c`) |
| 68000 timing | `tools/optiming/run.sh`: a test cartridge runs every legal opcode with each addressing mode on the port's core and on MAME's Mega Drive; per-instruction times compared | all equal except 73 `-(An)` cases, where an earlier illegal opcode MAME traps left different data; MUL/DIV: 1920 operand pairs equal |
| whole machine | `tools/mdlockstep`: against MAME's own Mega Drive, frame by frame | [lockstep.md](lockstep.md) |
| recompiled code | `m68krecomp.py --exact` vs the interpreter, RAM per frame (`mdhost -m`) | identical, 7 zones x 2500 frames |
| System 24 video | `tools/mds24.c`: the tile/char/palette RAM drawn as MAME's `model2_v.cpp` + `segaic24.cpp` compose it, against a reference renderer (`src/md_render.h`) | 0 pixels differ in 596 of 600 sampled frames (20 zones); 14 pixels in the other 4 (sprite priority, below) |
| sound | ymfm (MAME's YM2612) rendering the same register writes vs MAME's recording of the Model 2 | the pitches agree (e.g. 98/100, 247/246, 488/492 Hz) |
| MAME | native and web (Pinboard) Model 2 builds; game speed and pictures per 300 frames from a Lua script | play (the lockstep's input script, 3000 frames): 97% game speed, 30 pictures/s; attract mode (6000 frames): 90%, 21 pictures/s, 69% at worst (Spring Yard, Marble Zone) |

`tools/mdhost.c` runs the whole thing on a PC (pictures, RAM, profiles; `-t` a per-instruction
time trace); `tools/m68k_cyc.c` makes the cycle table from Musashi and
`tools/m68k_cyc_mame.py` corrects it to MAME's; `tools/mds24.c` (`-I../m2-sdk/src`) checks the tilemap
output against the reference renderer.

## Not done / known differences

- Sprites keep one priority bit per 8x8 cell: where a sprite meets plane B's high-priority
  pixels it can be wrong (a few pixels, rarely).
- Per-line palette changes (Labyrinth Zone's water colours) are not shown: one palette per
  frame. No window plane, shadow/highlight or sprite line limits (Sonic 1 doesn't need them).
- Sound: FM detune, LFO, SSG-EG and envelope curves are approximate; the SEGA voice at boot
  (the 68000 drives the DAC itself) is silent; the timpani uploads last (~20 s after boot).
- A swap that rewrites the whole name table (the title card) takes longer than vblank, so
  that one screen is torn.
- Busy scenes (Marble Zone, Spring Yard) run slow: 70-85% game speed at 10-13 pictures a
  second. There the 68000 takes about half the i960 and the sprite composites (`s24_sprites`,
  `s24_composite`) a third; the sound and the Z80 timing 1-2%. Switches for trying it
  (`-DSONIC_DEFS="..."` to cmake): `MD_CATCH_UP=8` (up to 8 frames between pictures, default
  4: 97% game speed in the attract mode, but 7 pictures a second at the worst),
  `MD_NO_Z80_TIMING` (no Z80 timing, YM2612 never busy: no longer in step with MAME's
  genesis), `SONIC_NO_SOUND`.
- The program ROM has about 1.5 KB left (the recompiled code fills it); `md_z80.h` is
  compiled `-Os` for that reason.
- Runs in MAME only so far: not tried on real hardware or m2emulator (the sound relay's
  caveats are Pac-Man's: m2-pacman `docs/sound.md`).
