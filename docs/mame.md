# The MAME used

Any MAME with the Model 2 driver runs the port (`sfight`, see the README). The checks used
builds of [claude_mame](https://github.com/biggestsonicfan/mame) (MAME 0.289):

| Build | Source | For |
|---|---|---|
| native, Model 2 + Mega Drive in one binary | branch `web-audio-latency`, `1be23f21bff` | `tools/mdlockstep` (`genesis` is the reference, `sfight` the port) and runs |
| web (Emscripten), Model 2 | `ede4c2fa981` | Pinboard's `mame-m2` launcher |

The lockstep binary:

```sh
make SUBTARGET=m2md SOURCES=src/mame/sega/model2.cpp,src/mame/sega/mdconsole.cpp -j2
```

Stock roms needed: `sfight.zip`, `schamp.zip`, `segabill.zip` (the port replaces three of
sfight's EPROMs, the rest comes from these), and the Sonic cartridge image for `genesis`.

Headless runs: `SDL_VIDEODRIVER=dummy mame ... -video none -sound none -nothrottle`, with
`-cfg_directory`/`-nvram_directory` pointed at a scratch folder.

Things about MAME's side that the lockstep depends on:

- `screen:pixels()` in a frame notifier returns the picture of the previous frame.
- The Mega Drive's autovector interrupts wait for the E clock (`vpa_sync`, a 10-cycle grid),
  and VINT is raised 32 x 4 master clocks into the line (`315_5313.cpp`).
- MAME's Model 2 i960 counts cycles as ALU 1, load 4, store 2, compare-and-branch 4, call 9,
  ret 7; the port's timer panel and budgets are in those cycles, not the real chip's.
