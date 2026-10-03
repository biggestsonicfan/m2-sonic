#!/bin/bash
# tools/optiming/run.sh: time every 68000 opcode on the port's core (tools/mdhost.c) and on
# MAME's Mega Drive (the reference), and list where they differ (cmp.py).
#   MAME  a MAME with genesis (default: the shared build)
#   OT    scratch dir (default /tmp/optiming); traces go to /dev/shm/optiming-$$
set -e
MAME=${MAME:-$HOME/build/mame-bin/mame-shared/shared}
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../.." && pwd)
OT=${OT:-/tmp/optiming}; shm=/dev/shm/optiming-$$
mkdir -p "$OT/inc" "$shm"; trap 'rm -f "$shm"/*.tr; rmdir "$shm"' EXIT
python3 "$here/genrom.py" "$OT/optest.bin"
python3 "$repo/tools/mdrom.py" "$OT/optest.bin" -o "$OT/inc/sonic_rom.h" > /dev/null
cc -O2 -I"$OT/inc" -I"$repo/src" -o "$OT/mdhost" "$repo/tools/mdhost.c"
"$OT/mdhost" -f 700 -t "$shm/port.tr" > /dev/null
( cd "$OT" && OT_TRACE=$shm/ref.tr SDL_VIDEODRIVER=dummy "$MAME" genesis -cart "$OT/optest.bin" \
    -video none -nothrottle -sound none -skip_gameinfo -debug -debugger none -seconds_to_run 12 \
    -cfg_directory "$OT/cfg" -nvram_directory "$OT/nvram" -autoboot_script "$here/trace.lua" > /dev/null 2>&1 )
python3 "$here/cmp.py" "$OT/optest.bin" "$shm/port.tr" "$shm/ref.tr" | tee "$OT/report.txt"
