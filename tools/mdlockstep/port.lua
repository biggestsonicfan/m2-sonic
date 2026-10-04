-- mdlockstep/port.lua: the i960 port (src/sonic.c) under MAME's Model 2 driver, sfight with
-- the five EPROMs swapped. Records what ref.lua records, from the port's own variables
-- (symbols from $LS_OUT/port.syms, `i960-elf-nm game.elf`):
--   port_irq.bin / port_mem.bin at every interrupt the 68000 takes (md_irq_snap: a write
--   tap on its count, written last), port_snd.bin from the port's chip-write queue (md_snd,
--   a write tap on md_snd_head) and md_z80_cmd, and pictures: the Model 2 screen's 320x224
--   window when the tilemaps hold frame k (sonic_shown; not while sonic_busy, the next
--   picture being built), k % 60 < 5 -> port_NNNNN.rgb, with the VDP state it was made
--   from (port_NNNNN.mem).
-- Frames are counted in VINTs taken (the reference counts them; the boot runs frames
-- without). port_iack.bin: frame u32, level u32, the port's 68000 time (md_now) u64.
-- The pad: md_pad written as each VINT is taken, for its handler to read. Every SCSP register write the sound board makes:
-- port_scsp.bin (time f64, register u16, value u16).
local tools = os.getenv("LS_TOOLS") or "tools/mdlockstep"
if LS_PORT then return end
LS_PORT = true
dofile(tools .. "/inputs.lua")
local SYM = {}
for l in io.lines(LS_OUT .. "/port.syms") do
  local a, n = l:match("^(%x+) %a (%S+)$")
  if a then SYM[n] = tonumber(a, 16) end
end
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local snd = manager.machine.devices[":audiocpu"].spaces["program"]
local scr = manager.machine.screens[":screen"]
local irq = assert(io.open(LS_OUT .. "/port_irq.bin", "wb"))
local mem = assert(io.open(LS_OUT .. "/port_mem.bin", "wb"))
local sout = assert(io.open(LS_OUT .. "/port_snd.bin", "wb"))
local wout = assert(io.open(LS_OUT .. "/port_scsp.bin", "wb"))
local iack = assert(io.open(LS_OUT .. "/port_iack.bin", "wb"))
local done, vints, shown, last = false, 0, -1, -1
local vint_of = {}                                   -- the port's frame -> the VINTs by its end
local S = SYM._md_irq_snap

local function w16(base, n)      -- u16 words as the i960 holds them (native), big-endian out
  local t = {}
  for i = 0, n - 1 do t[#t + 1] = string.pack(">I2", sp:read_u16(base + i * 2)) end
  return table.concat(t)
end

LS_IRQ = sp:install_write_tap(S + 21 * 4, S + 21 * 4 + 3, "ls_irq", function(offset, data, mask)
  if done then return end
  local level = sp:read_u32(S + 19 * 4)
  if level == 0 then return end                      -- the boot code clearing RAM
  if level == 6 then
    vints = vints + 1
    vint_of[sp:read_u32(S + 20 * 4)] = vints
    sp:write_u8(SYM._md_pad, LS_INPUTS(vints))       -- what this VINT's handler will read
  end
  local t = { string.pack("<I4I4", level == 6 and vints or vints + 1, level) }
  for i = 0, 15 do t[#t + 1] = string.pack("<I4", sp:read_u32(S + i * 4)) end
  for i = 16, 18 do t[#t + 1] = string.pack("<I4", sp:read_u32(S + i * 4)) end
  irq:write(table.concat(t))
  iack:write(string.pack("<I4I4I8", level == 6 and vints or vints + 1, level, sp:read_u32(S + 22 * 4)))
  if level == 6 then
    local m = { w16(SYM._md_ram, 0x8000), w16(SYM._md_vram, 0x8000), w16(SYM._md_cram, 64), w16(SYM._md_vsram, 40) }
    local r = {}
    for i = 0, 23 do r[#r + 1] = string.char(sp:read_u8(SYM._md_reg + i)) end
    m[#m + 1] = table.concat(r)
    mem:write(table.concat(m))
    if vints >= LS_FRAMES then done = true; irq:close(); mem:close(); sout:close(); wout:close(); iack:close(); manager.machine:exit() end
  end
end)


-- the chip writes the port queued (md_snd: 0x01PPRRVV YM, 0x020000VV PSG) and Z80 commands
LS_SND = sp:install_write_tap(SYM._md_snd_head, SYM._md_snd_head + 3, "ls_snd", function(offset, data, mask)
  if done or data == 0 then return end               -- 0: the boot code clearing RAM
  local e = sp:read_u32(SYM._md_snd + ((data - 1) & 1023) * 4)
  local f = vints + 1                                -- written before the next VINT
  if (e >> 24) == 1 then sout:write(string.pack("<I4BBBB", f, 1, (e >> 16) & 1, (e >> 8) & 0xff, e & 0xff))
  else sout:write(string.pack("<I4BBBB", f, 2, 0, 0, e & 0xff)) end
end)
local CMDW, CMDSH = SYM._md_z80_cmd & ~3, (SYM._md_z80_cmd & 3) * 8   -- taps take whole words
LS_CMD = sp:install_write_tap(CMDW, CMDW + 3, "ls_cmd", function(offset, data, mask)
  if not done and ((mask >> CMDSH) & 0xff) ~= 0 and sp:read_u32(SYM._md_frames) > 0 then
    sout:write(string.pack("<I4BBBB", vints + 1, 3, 0, 0, (data >> CMDSH) & 0xff))
  end
end)
LS_SCSP = snd:install_write_tap(0x100000, 0x100fff, "ls_scsp", function(offset, data, mask)
  if not done then wout:write(string.pack("<dI2I2", manager.machine.time:as_double(), offset - 0x100000, data & 0xffff)) end
end)

-- pictures: the frame the tilemaps hold, on screen from the next vblank
local function vdpstate()           -- a port_mem.bin record: what the tilemaps were built from
  local r = {}
  for i = 0, 23 do r[#r + 1] = string.char(sp:read_u8(SYM._md_reg + i)) end
  return w16(SYM._md_ram, 0x8000) .. w16(SYM._md_vram, 0x8000) .. w16(SYM._md_cram, 64) .. w16(SYM._md_vsram, 40) .. table.concat(r)
end
LS_SHOWN = sp:install_write_tap(SYM._sonic_shown, SYM._sonic_shown + 3, "ls_shown", function(offset, data, mask)
  shown = vint_of[data] or -1                        -- as a VINT count, like the reference
  if not done and shown > 0 and shown % 60 < 5 and shown ~= last then
    local f = io.open(string.format("%s/port_%05d.mem", LS_OUT, shown), "wb"); f:write(vdpstate()); f:close()
  end
end)
-- MAME's screen at a frame notifier is the one drawn at the vblank before (a frame of
-- display latency): the picture handed over is on screen at the notifier after next
local pend = nil
LS_PIC = emu.add_machine_frame_notifier(function()
  if done then return end
  if pend then
    if pend ~= shown then pend = nil; return end         -- a newer picture came first: drop it
    local px, w, h = scr:pixels()
    local t = {}
    for y = 80, 80 + 223 do
      for x = 88, 88 + 319 do
        local c = string.unpack("<I4", px, (y * w + x) * 4 + 1)
        t[#t + 1] = string.char((c >> 16) & 255, (c >> 8) & 255, c & 255)
      end
    end
    local f = io.open(string.format("%s/port_%05d.rgb", LS_OUT, pend), "wb"); f:write(table.concat(t)); f:close()
    pend = nil
    return
  end
  if shown < 1 or shown == last or shown % 60 >= 5 then return end
  if sp:read_u32(SYM._sonic_busy) ~= 0 then return end   -- the next picture half built: not this one
  last = shown; pend = shown
end)
