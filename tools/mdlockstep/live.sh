#!/bin/bash
# tools/mdlockstep/live.sh: the lockstep to watch. Opens two MAME windows side by side on
# $DISPLAY: left MAME's Mega Drive (genesis) with the cartridge, right the i960 port under
# the Model 2 driver (sfight + the port's EPROMs), both playing inputs.lua in step (live.lua).
#   MAME     a MAME with both drivers (default: the shared build)
#   CART     the Sonic the Hedgehog cartridge image (.bin)
#   ROMS_M2  a folder with sfight.zip, schamp.zip, segabill.zip
#   GAME     the port's build dir (game.elf)
#   LIVE_DIR scratch for the EPROMs, MAME's cfg/nvram and the sync files (/tmp/sonic-live)
# Close either window to stop it; the other then runs on alone.
set -e
MAME=${MAME:-$HOME/build/mame-bin/mame-shared/shared}
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../.." && pwd)
export LS_TOOLS=$here LIVE_DIR=${LIVE_DIR:-/tmp/sonic-live}
mkdir -p "$LIVE_DIR/roms/sfight"
rm -f "$LIVE_DIR/ref.cnt" "$LIVE_DIR/port.cnt"
i960-elf-nm "$GAME/game.elf" | grep -E " _(md_(irq_snap|pad)|sonic_(shown|busy))$" > "$LIVE_DIR/port.syms"
# the EPROMs from that same game.elf, so the symbols match
i960-elf-objcopy -O binary "$GAME/game.elf" "$LIVE_DIR/game.bin"
python3 "${M2_SDK:-$repo/../m2-sdk}/tools/stfbin2rom.py" --input "$LIVE_DIR/game.bin" --output "$LIVE_DIR/roms/sfight" > /dev/null
python3 "$repo/tools/snd2rom.py" "$repo/snd/scsp_passthru.bin" "$LIVE_DIR/roms/sfight" > /dev/null
common="-window -nomaximize -resolution 944x708 -video soft -sound none -skip_gameinfo -nvram_directory $LIVE_DIR/nvram -autoboot_script $here/live.lua"
cd "$LIVE_DIR"                                       # the Model 2 driver writes geo_list_dump.bin here
"$MAME" genesis -cart "$CART" $common -cfg_directory "$LIVE_DIR/cfg-ref" > "$LIVE_DIR/ref.log" 2>&1 &
"$MAME" sfight -rompath "$LIVE_DIR/roms;$ROMS_M2" $common -cfg_directory "$LIVE_DIR/cfg-port" > "$LIVE_DIR/port.log" 2>&1 &
# place the windows: genesis left, the port right (titles "<game> [genesis] - MAME ...")
set +e
for _ in $(seq 60); do
  g=$(xdotool search --name '\[genesis\]' | head -1); f=$(xdotool search --name '\[sfight\]' | head -1)
  [ -n "$g" ] && [ -n "$f" ] && break; sleep 0.5
done
[ -n "$g" ] && xdotool windowmove "$g" 8 40
[ -n "$f" ] && xdotool windowmove "$f" 968 40
wait
