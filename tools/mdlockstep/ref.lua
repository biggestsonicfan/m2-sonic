-- mdlockstep/ref.lua: MAME's Mega Drive (genesis -cart sonic.bin), the reference.
-- At every interrupt the 68000 takes (its acknowledge cycle, a read tap on its CPU space):
-- ref_irq.bin: frame u32, level u32, d0-d7 a0-a7 pc sr usp (19 x u32);
-- at each VINT also the machine: ref_mem.bin: work RAM 64 KB, VRAM 64 KB, CRAM 128 B,
-- VSRAM 80 B, VDP registers 24 B. Every YM2612 / PSG write the 68000 makes and every Z80
-- sound command (0xA01FFF): ref_snd.bin (frame u32, kind u8 1 YM / 2 PSG / 3 command,
-- port u8, register u8, value u8). The pad (0xA10003) is fed from inputs.lua, frame k = the
-- VINTs so far + 1. Pictures: ref_NNNNN.rgb (320 x 224 x RGB) for frames k % 60 < 5.
-- ref_iack.bin: frame u32, level u32, the 68000's cycle count at the acknowledge u64, when
-- the MAME has device.total_cycles (claude_mame's md-lockstep branch, in the shared build).
local tools = os.getenv("LS_TOOLS") or "tools/mdlockstep"
dofile(tools .. "/inputs.lua")
local cpu = manager.machine.devices[":maincpu"]
local sp = cpu.spaces["program"]
local cs = cpu.spaces["cpu_space"]
local vdp = manager.machine.devices[":gen_vdp"]
local scr = manager.machine.screens[":megadriv"]
local irq = assert(io.open(LS_OUT .. "/ref_irq.bin", "wb"))
local mem = assert(io.open(LS_OUT .. "/ref_mem.bin", "wb"))
local snd = assert(io.open(LS_OUT .. "/ref_snd.bin", "wb"))
local iack = cpu.total_cycles and assert(io.open(LS_OUT .. "/ref_iack.bin", "wb"))
local vints, done, th = 0, false, 0x40
local ym_addr = { 0, 0 }

-- the VDP's memories as save items (sega315_5313 / its mode 4 base)
local function item(name)
  for k, v in pairs(vdp.items) do if k:match("/" .. name .. "$") then return emu.item(v) end end
  error("no VDP item " .. name)
end
local VRAM, CRAM, VSRAM, REGS = item("m_vram"), item("m_cram"), item("m_vsram"), item("m_regs")
local function words(it, n) local t = {} for i = 0, n - 1 do t[#t + 1] = string.pack(">I2", it:read(i) & 0xffff) end return table.concat(t) end
local function bytes(it, n) local t = {} for i = 0, n - 1 do t[#t + 1] = string.char(it:read(i) & 0xff) end return table.concat(t) end

local function reg(n) return cpu.state[n].value & 0xffffffff end
LS_IACK = cs:install_read_tap(0xfffff0, 0xffffff, "ls_iack", function(offset, data, mask)
  if done then return end
  local level = (offset & 0xf) >> 1
  local t = { string.pack("<I4I4", vints + 1, level) }
  for i = 0, 7 do t[#t + 1] = string.pack("<I4", reg("D" .. i)) end
  for i = 0, 6 do t[#t + 1] = string.pack("<I4", reg("A" .. i)) end
  t[#t + 1] = string.pack("<I4", reg("SP"))                          -- A7
  t[#t + 1] = string.pack("<I4I4I4", reg("PC"), reg("SR"), reg("USP"))
  irq:write(table.concat(t))
  if iack then iack:write(string.pack("<I4I4I8", vints + 1, level, cpu.total_cycles)) end
  if level == 6 then
    vints = vints + 1
    local m = {}
    for a = 0xff0000, 0xfffffc, 4 do m[#m + 1] = string.pack(">I4", sp:read_u32(a)) end
    m[#m + 1] = words(VRAM, 0x8000); m[#m + 1] = words(CRAM, 64); m[#m + 1] = words(VSRAM, 40); m[#m + 1] = bytes(REGS, 24)
    mem:write(table.concat(m))
    if vints >= LS_FRAMES then done = true; irq:close(); mem:close(); snd:close(); if iack then iack:close() end; manager.machine:exit() end
  end
end)

local function s(kind, port, r, v) if not done then snd:write(string.pack("<I4BBBB", vints + 1, kind, port, r, v)) end end
LS_YM = sp:install_write_tap(0xa04000, 0xa04003, "ls_ym", function(offset, data, mask)
  local port, b = (offset >> 1) & 1, (mask & 0xff00 ~= 0) and ((data >> 8) & 0xff) or (data & 0xff)
  if (mask & 0xff00) ~= 0 then ym_addr[port + 1] = (data >> 8) & 0xff    -- even byte: the register
  else s(1, port, ym_addr[port + 1], data & 0xff) end                   -- odd byte: the data
end)
LS_PSG = sp:install_write_tap(0xc00010, 0xc00017, "ls_psg", function(offset, data, mask)
  if (mask & 0x00ff) ~= 0 then s(2, 0, 0, data & 0xff) end
end)
LS_CMD = sp:install_write_tap(0xa01ffe, 0xa01fff, "ls_cmd", function(offset, data, mask)
  if (mask & 0x00ff) ~= 0 then s(3, 0, 0, data & 0xff) end
end)
-- the pad: TH from the game's writes to the data port, the buttons from the script
LS_PADW = sp:install_write_tap(0xa10002, 0xa10003, "ls_padw", function(offset, data, mask)
  if (mask & 0x00ff) ~= 0 then th = data & 0x40 end
end)
LS_PADR = sp:install_read_tap(0xa10002, 0xa10003, "ls_padr", function(offset, data, mask)
  local pad = LS_INPUTS(vints)                     -- the VINT being handled (inputs.lua)
  local v = th ~= 0 and (0x40 | (pad & 0x3f)) or (((pad >> 2) & 0x30) | (pad & 0x03))
  return (data & 0xff00) | (data & 0x80) | v
end)
-- pictures: after the frame is drawn (a frame notifier), frames k % 60 < 5
LS_PIC = emu.add_machine_frame_notifier(function()
  if done or vints % 60 >= 5 or vints == 0 then return end
  local px, w, h = scr:pixels()
  local t = {}
  for y = 0, 223 do
    for x = 0, 319 do
      local c = string.unpack("<I4", px, (y * w + x) * 4 + 1)
      t[#t + 1] = string.char((c >> 16) & 255, (c >> 8) & 255, c & 255)
    end
  end
  local f = io.open(string.format("%s/ref_%05d.rgb", LS_OUT, vints), "wb"); f:write(table.concat(t)); f:close()
end)
