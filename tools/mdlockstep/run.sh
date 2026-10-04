#!/bin/bash
# tools/mdlockstep/run.sh: record MAME's Mega Drive (genesis) running the Sonic cartridge and
# the i960 port (src/sonic.c under MAME's Model 2 driver) on the same input script, then
# compare them frame by frame (compare.py).
#   MAME     a MAME with both drivers (docs/mame.md; default: the shared build)
#   CART     the Sonic the Hedgehog cartridge image (.bin)
#   ROMS_M2  a folder with sfight.zip, schamp.zip, segabill.zip
#   GAME     the port's build dir (game.elf; the EPROMs are its roms/sonic/)
#   LS_FRAMES (3000), LS_OUT (/tmp/mdlockstep)
set -e
MAME=${MAME:-$HOME/build/mame-bin/mame-shared/shared}
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../.." && pwd)
export LS_TOOLS=$here LS_FRAMES=${LS_FRAMES:-3000} LS_OUT=${LS_OUT:-/tmp/mdlockstep}
mkdir -p "$LS_OUT/roms/sfight"
i960-elf-nm "$GAME/game.elf" | grep -E " _(md_(irq_snap|ram|vram|cram|vsram|reg|frames|pad|snd|snd_head|z80_cmd)|sonic_(shown|busy))$" > "$LS_OUT/port.syms"
common="-video none -nothrottle -skip_gameinfo -cfg_directory $LS_OUT/cfg -nvram_directory $LS_OUT/nvram -sound none"
SDL_VIDEODRIVER=dummy "$MAME" genesis -cart "$CART" $common -wavwrite "$LS_OUT/ref.wav" \
  -autoboot_script "$here/ref.lua" > "$LS_OUT/ref.log" 2>&1
cp "$repo"/roms/sonic/epr-190{01.15,02.16,03.7,04.8,21.31} "$LS_OUT/roms/sfight/"
SDL_VIDEODRIVER=dummy "$MAME" sfight -rompath "$LS_OUT/roms;$ROMS_M2" $common -wavwrite "$LS_OUT/port.wav" \
  -autoboot_script "$here/port.lua" > "$LS_OUT/port.log" 2>&1
python3 "$here/compare.py" "$LS_OUT"
