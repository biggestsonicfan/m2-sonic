-- mdlockstep/live.lua: the lockstep, to watch. Load it into both MAMEs (live.sh starts them):
-- `genesis` with the cartridge and `sfight` with the port's EPROMs. Both play inputs.lua's
-- pad, frame k = the k-th VINT, and keep each other in step: at each VINT a side writes its
-- count to $LIVE_DIR/<side>.cnt and, while it is ahead of the other, waits there. A side
-- whose partner stops counting for 10 seconds (closed, or not started in 60) runs on alone
-- until the partner counts again.
-- Each screen shows which side it is and the frame. The port's symbols: $LIVE_DIR/port.syms.
local tools = os.getenv("LS_TOOLS") or "tools/mdlockstep"
local dir = os.getenv("LIVE_DIR") or "/tmp/sonic-live"
if LS_LIVE then return end
LS_LIVE = true
dofile(tools .. "/inputs.lua")
local port = manager.machine.system.name ~= "genesis"
local me, other = port and "port" or "ref", port and "ref" or "port"
local vints, alone = 0, false
local seen, seen_at = nil, os.time() + 50           -- the partner's count, when it last moved

local function put(k)
  local f = io.open(dir .. "/" .. me .. ".tmp", "w")
  if not f then return end
  f:write(tostring(k)); f:close()
  os.rename(dir .. "/" .. me .. ".tmp", dir .. "/" .. me .. ".cnt")
end
local function get()
  local f = io.open(dir .. "/" .. other .. ".cnt")
  if not f then return nil end
  local v = tonumber(f:read("a")); f:close()
  return v
end
local function sync(k)                               -- this side has reached VINT k
  put(k)
  while true do
    local o = get()
    if o ~= seen then seen, seen_at, alone = o, os.time(), false end
    if alone or (o and o >= k) then return end
    if os.time() - seen_at > 10 then alone = true; return end
    local t = os.clock() + 0.0003                    -- spin: a sleep process is too coarse
    while os.clock() < t do end
  end
end

local scr
if port then
  local SYM = {}
  for l in io.lines(dir .. "/port.syms") do
    local a, n = l:match("^(%x+) %a (%S+)$")
    if a then SYM[n] = tonumber(a, 16) end
  end
  local sp = manager.machine.devices[":maincpu"].spaces["program"]
  local S = SYM._md_irq_snap
  scr = manager.machine.screens[":screen"]
  LIVE_IRQ = sp:install_write_tap(S + 21 * 4, S + 21 * 4 + 3, "live_irq", function()
    if sp:read_u32(S + 19 * 4) ~= 6 then return end  -- VINTs only (0: the boot clearing RAM)
    vints = vints + 1
    sp:write_u8(SYM._md_pad, LS_INPUTS(vints))      -- what this VINT's handler will read
    sync(vints)
  end)
else
  local cpu = manager.machine.devices[":maincpu"]
  local sp = cpu.spaces["program"]
  local th = 0x40
  scr = manager.machine.screens[":megadriv"]
  LIVE_IACK = cpu.spaces["cpu_space"]:install_read_tap(0xfffff0, 0xffffff, "live_iack", function(offset)
    if (offset & 0xf) >> 1 ~= 6 then return end
    vints = vints + 1
    sync(vints)
  end)
  -- the pad: TH from the game's writes to the data port, the buttons from the script
  LIVE_PADW = sp:install_write_tap(0xa10002, 0xa10003, "live_padw", function(offset, data, mask)
    if (mask & 0x00ff) ~= 0 then th = data & 0x40 end
  end)
  LIVE_PADR = sp:install_read_tap(0xa10002, 0xa10003, "live_padr", function(offset, data, mask)
    local pad = LS_INPUTS(vints)
    local v = th ~= 0 and (0x40 | (pad & 0x3f)) or (((pad >> 2) & 0x30) | (pad & 0x03))
    return (data & 0xff00) | (data & 0x80) | v
  end)
end

local label = port and "m2-sonic: the i960 port on Model 2 (sfight)" or "MAME genesis: the cartridge"
emu.register_frame_done(function()
  scr:draw_text(2, 2, string.format("%s   frame %d%s", label, vints, alone and "  (partner gone: running alone)" or ""),
                0xffffffff, 0xc0000000)
end)
