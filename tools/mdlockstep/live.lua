-- mdlockstep/live.lua: the lockstep, to watch. Load it into both MAMEs (live.sh starts them):
-- `genesis` with the cartridge and `sfight` with the port's EPROMs. Both play inputs.lua's
-- pad, frame k = the k-th VINT, and keep each other in step: at each VINT a side writes its
-- count to $LIVE_DIR/<side>.cnt and, while it is ahead of the other, waits there. A side
-- whose partner stops counting for 10 seconds (closed, or not started in 60) runs on alone
-- until the partner counts again.
-- Each screen shows which side it is, the frame and live_check.py's verdict ($LIVE_DIR/status.txt).
-- The port's symbols: $LIVE_DIR/port.syms. LS_FRAMES set: both stop after that many frames.
--
-- The check (LIVE_CHECK=0: off): each side appends to $LIVE_DIR/<side>.rec, for
-- live_check.py to compare (hashes: FNV-1a 64 over the raw bytes, printed as 8 hex digits):
--   F k pc cpu ram vram cram vsram vdp snd aud    at VINT k: pc, a hash of d0-a7 pc sr
--       (SR as MAME shows it after the acknowledge, WaitForVBla's two instructions as one),
--       work RAM in 16 blocks of 4 KB (the last without the stack page) and the stack from
--       SP up, VRAM in 16 blocks, CRAM, VSRAM, the 24 VDP registers, the YM2612 / PSG writes
--       and Z80 commands the 68000 made in frame k (hash/count), the audio's RMS in frame k
--   P k hash      the picture of frame k: the one the VDP state at VINT k draws (the frame
--                 shown as that VINT comes), 320x224, colours as Mega Drive levels (the
--                 port: only the frames it draws)
--   S k hash b    port: every screen MAME drew, as the frame k it should show (the last
--                 picture the port finished); b = 1: drawn while the next one was being built,
--                 2: while it was going on screen (s24_swap: torn)
--   Y k kind port reg val   ref: each YM2612 (1) / PSG (2) write, Z80 command (3)
--   W k slot reg val        port: each SCSP slot register write the sound board makes
-- LIVE_DBG=1: also D k n, the bytes the port has queued for the sound board at VINT k.
-- LIVE_PICS=a-b: also save the pictures of frames a-b as <side>_NNNNN.rgb (320x224 RGB), and
-- the port's screens showing them as scr_NNNNN_<screen>.rgb.
-- In a watch (no LS_FRAMES) the records start over every 18000 frames (<side>.rec.old: the last).
local tools = os.getenv("LS_TOOLS") or "tools/mdlockstep"
local dir = os.getenv("LIVE_DIR") or "/tmp/sonic-live"
if LS_LIVE then return end
LS_LIVE = true
dofile(tools .. "/inputs.lua")
local stop_at = tonumber(os.getenv("LS_FRAMES") or "")
local check = os.getenv("LIVE_CHECK") ~= "0"
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

-- ---- hashing -----------------------------------------------------------------------------
local unpack, fmt = string.unpack, string.format
local P, H0 = 0x100000001b3, 0xcbf29ce484222325
local F16 = "<" .. ("I4"):rep(16)
local function fnv(s, h)                             -- FNV-1a 64 over u32 words, then bytes
  h = h or H0
  local n, i = #s, 1
  while i + 63 <= n do
    local a, b, c, d, e, f, g, x, y, z, j, k, l, m, o, p = unpack(F16, s, i)
    h = (h ~ a) * P; h = (h ~ b) * P; h = (h ~ c) * P; h = (h ~ d) * P
    h = (h ~ e) * P; h = (h ~ f) * P; h = (h ~ g) * P; h = (h ~ x) * P
    h = (h ~ y) * P; h = (h ~ z) * P; h = (h ~ j) * P; h = (h ~ k) * P
    h = (h ~ l) * P; h = (h ~ m) * P; h = (h ~ o) * P; h = (h ~ p) * P
    i = i + 64
  end
  for q = i, n do h = (h ~ s:byte(q)) * P end
  return h
end
local function hx(h) return fmt("%08x", (h ~ (h >> 32)) & 0xffffffff) end
-- a picture: 320x224 pixels of an ARGB bitmap `w` wide from (x0, y0), each colour reduced to
-- the Mega Drive's 3-bit levels (Q: pixel -> 9-bit colour) before hashing
local function picture_hash(px, w, x0, y0, Q)
  local h = H0
  for y = y0, y0 + 223 do
    local i = (y * w + x0) * 4 + 1
    for _ = 1, 20 do
      local a, b, c, d, e, f, g, x, yy, z, j, k, l, m, o, p = unpack(F16, px, i)
      h = (h ~ Q[a]) * P; h = (h ~ Q[b]) * P; h = (h ~ Q[c]) * P; h = (h ~ Q[d]) * P
      h = (h ~ Q[e]) * P; h = (h ~ Q[f]) * P; h = (h ~ Q[g]) * P; h = (h ~ Q[x]) * P
      h = (h ~ Q[yy]) * P; h = (h ~ Q[z]) * P; h = (h ~ Q[j]) * P; h = (h ~ Q[k]) * P
      h = (h ~ Q[l]) * P; h = (h ~ Q[m]) * P; h = (h ~ Q[o]) * P; h = (h ~ Q[p]) * P
      i = i + 64
    end
  end
  return h
end
local pics_a, pics_b = (os.getenv("LIVE_PICS") or ""):match("^(%d+)-(%d+)$")
pics_a, pics_b = tonumber(pics_a or 1), tonumber(pics_b or 0)
local function save_picture(k, px, w, x0, y0, name)
  if k < pics_a or k > pics_b then return end
  local t = {}
  for y = y0, y0 + 223 do
    for x = x0, x0 + 319 do
      local c = unpack("<I4", px, (y * w + x) * 4 + 1)
      t[#t + 1] = string.char((c >> 16) & 255, (c >> 8) & 255, c & 255)
    end
  end
  local f = io.open(name or fmt("%s/%s_%05d.rgb", dir, me, k), "wb"); f:write(table.concat(t)); f:close()
end
local function levels()                              -- ARGB -> 9-bit colour, cached per pixel value
  local lv = {}
  for v = 0, 255 do lv[v] = (v * 7 + 127) // 255 end   -- the nearest of 8 even steps
  return setmetatable({}, { __index = function(t, c)
    local v = (lv[(c >> 16) & 255] << 6) | (lv[(c >> 8) & 255] << 3) | lv[c & 255]
    t[c] = v
    return v
  end })
end

local log, frame_snd, snd_n, aud_ss, aud_n = nil, H0, 0, 0.0, 0
local function open_log()
  log = check and assert(io.open(dir .. "/" .. me .. ".rec", "w"))
end
open_log()
local function out(s) if log then log:write(s, "\n") end end
local function snd_write(kind, p, r, v)              -- one of frame vints + 1's chip writes
  frame_snd = (frame_snd ~ ((kind << 24) | (p << 16) | (r << 8) | v)) * P
  snd_n = snd_n + 1
end
-- the audio as it leaves the speakers: RMS per frame
if check then
  for _, s in pairs(manager.machine.sounds) do if s.speaker then s.hook = true end end
  emu.register_sound_update(function(t)
    for _, chans in pairs(t) do
      for _, c in ipairs(chans) do
        for i = 1, #c do local v = c[i]; aud_ss = aud_ss + v * v end
        aud_n = aud_n + #c
      end
    end
  end)
end
-- the F record; ram/vram/cram/vsram: the memories as raw u16 words (host order on both)
local function record(k, regs, ram, sp, vram, cram, vsram, vdp)
  local pc = regs[17]
  if pc >= 0x29ac and pc <= 0x29b2 then pc = 0x29ac end  -- WaitForVBla: one wait, any instruction
  regs[17] = pc
  regs[18] = (regs[18] & ~0x8700) | 0x2000 | 0x0600     -- SR after the acknowledge
  local t = {}
  for i = 1, 19 do t[i] = fmt("%08x", regs[i] & 0xffffffff) end
  local blocks = {}
  for b = 0, 15 do
    blocks[#blocks + 1] = hx(fnv(ram:sub(b * 4096 + 1, b == 15 and 0xfd00 or (b + 1) * 4096)))
  end
  sp = sp & 0xffff
  blocks[#blocks + 1] = hx(fnv(ram:sub((sp >= 0xfd00 and sp or 0xfd00) + 1)))  -- the live stack
  local vb = {}
  for b = 0, 15 do vb[#vb + 1] = hx(fnv(vram:sub(b * 4096 + 1, (b + 1) * 4096))) end
  local rms = aud_n > 0 and math.sqrt(aud_ss / aud_n) or 0
  out(fmt("F %d %06x %s %s %s %s %s %s %s/%d %.5f", k, pc, hx(fnv(table.concat(t))),
      table.concat(blocks, ","), table.concat(vb, ","), hx(fnv(cram)), hx(fnv(vsram)), hx(fnv(vdp)),
      hx(frame_snd), snd_n, rms))
  frame_snd, snd_n, aud_ss, aud_n = H0, 0, 0.0, 0
end
local function vint_done(k)                          -- after the record: flush, start over
  if not log then return end
  log:flush()
  if not stop_at and k % 18000 == 0 then
    log:close(); os.rename(dir .. "/" .. me .. ".rec", dir .. "/" .. me .. ".rec.old"); open_log()
  end
end

local scr, last_pic
if port then
  local SYM = {}
  for l in io.lines(dir .. "/port.syms") do
    local a, n = l:match("^(%x+) %a (%S+)$")
    if a then SYM[n] = tonumber(a, 16) end
  end
  local sp = manager.machine.devices[":maincpu"].spaces["program"]
  local snd = manager.machine.devices[":audiocpu"].spaces["program"]
  local S = SYM._md_irq_snap
  local vint_of, shown, pend = {}, -1, nil
  scr = manager.machine.screens[":screen"]
  LIVE_IRQ = sp:install_write_tap(S + 21 * 4, S + 21 * 4 + 3, "live_irq", function()
    if sp:read_u32(S + 19 * 4) ~= 6 then return end  -- VINTs only (0: the boot clearing RAM)
    vints = vints + 1
    vint_of[sp:read_u32(S + 20 * 4)] = vints         -- the port's frame -> the VINTs by its end
    sp:write_u8(SYM._md_pad, LS_INPUTS(vints))      -- what this VINT's handler will read
    if check then
      local regs = {}
      for i = 0, 18 do regs[i + 1] = sp:read_u32(S + i * 4) end
      local r = {}
      for i = 0, 23 do r[#r + 1] = string.char(sp:read_u8(SYM._md_reg + i)) end
      record(vints, regs, sp:read_range(SYM._md_ram, SYM._md_ram + 0xffff, 8), regs[16],
             sp:read_range(SYM._md_vram, SYM._md_vram + 0xffff, 8),
             sp:read_range(SYM._md_cram, SYM._md_cram + 127, 8),
             sp:read_range(SYM._md_vsram, SYM._md_vsram + 79, 8), table.concat(r))
      vint_done(vints)
    end
    if SYM._m2_scsp_qh and os.getenv("LIVE_DBG") then        -- D k n: bytes waiting for the sound board
      out(fmt("D %d %d", vints, (sp:read_u32(SYM._m2_scsp_qt) - sp:read_u32(SYM._m2_scsp_qh)) & 4095))
    end
    sync(vints)
    if stop_at and vints >= stop_at then manager.machine:exit() end
  end)
  if check then
    -- the chip writes the 68000 made, as the port queued them (md_snd: 0x01PPRRVV YM,
    -- 0x020000VV PSG), and its Z80 commands
    LIVE_SND = sp:install_write_tap(SYM._md_snd_head, SYM._md_snd_head + 3, "live_snd", function(offset, data)
      if data == 0 then return end                   -- 0: the boot code clearing RAM
      local e = sp:read_u32(SYM._md_snd + ((data - 1) & 1023) * 4)
      if (e >> 24) == 1 then snd_write(1, (e >> 16) & 1, (e >> 8) & 0xff, e & 0xff)
      else snd_write(2, 0, 0, e & 0xff) end
    end)
    local CMDW, CMDSH = SYM._md_z80_cmd & ~3, (SYM._md_z80_cmd & 3) * 8   -- taps take whole words
    LIVE_CMD = sp:install_write_tap(CMDW, CMDW + 3, "live_cmd", function(offset, data, mask)
      if ((mask >> CMDSH) & 0xff) ~= 0 and sp:read_u32(SYM._md_frames) > 0 then
        snd_write(3, 0, 0, (data >> CMDSH) & 0xff)
      end
    end)
    -- what the sound board tells the SCSP
    LIVE_SCSP = snd:install_write_tap(0x100000, 0x1003ff, "live_scsp", function(offset, data)
      local o = offset - 0x100000
      out(fmt("W %d %d %d %d", vints + 1, o >> 5, o & 0x1e, data & 0xffff))
    end)
    -- the picture: the frame the tilemaps hold (sonic_shown; not while sonic_busy, the next
    -- one half built) is on screen from the next vblank, and MAME's pixels() at a frame
    -- notifier are the vblank before's: read one notifier later still (as port.lua). Made
    -- after VINT k's handler, it is what the state at VINT k + 1 draws: frame k + 1.
    local Q = levels()
    LIVE_SHOWN = sp:install_write_tap(SYM._sonic_shown, SYM._sonic_shown + 3, "live_shown", function(offset, data)
      shown = vint_of[data] or -1
    end)
    -- every screen too: the notifier comes as MAME draws one (at the end of vblank), and
    -- pixels() hold it at the next notifier
    local drawn, drawn_busy = -1, 0
    LIVE_PIC = emu.add_machine_frame_notifier(function()
      local px, w, h
      if drawn >= 1 then
        px, w = scr:pixels()
        h = picture_hash(px, w, 88, 80, Q)
        out(fmt("S %d %s %d", drawn + 1, hx(h), drawn_busy))
        scr_n = (scr_n or 0) + 1
        save_picture(drawn + 1, px, w, 88, 80, fmt("%s/scr_%05d_%d.rgb", dir, drawn + 1, scr_n))
      end
      drawn, drawn_busy = shown, sp:read_u32(SYM._sonic_busy)
      if pend then
        if pend == shown then
          if not px then px, w = scr:pixels(); h = picture_hash(px, w, 88, 80, Q) end
          out(fmt("P %d %s", pend + 1, hx(h)))
          save_picture(pend + 1, px, w, 88, 80)
        end
        pend = nil
        return
      end
      if shown < 1 or shown == last_pic or sp:read_u32(SYM._sonic_busy) ~= 0 then return end
      last_pic, pend = shown, shown
    end)
  end
else
  local cpu = manager.machine.devices[":maincpu"]
  local sp = cpu.spaces["program"]
  local vdp = manager.machine.devices[":gen_vdp"]
  local th = 0x40
  local ym_addr = { 0, 0 }
  scr = manager.machine.screens[":megadriv"]
  local function item(name)                          -- the VDP's memories as save items
    for k, v in pairs(vdp.items) do if k:match("/" .. name .. "$") then return emu.item(v) end end
    error("no VDP item " .. name)
  end
  local VRAM, CRAM, VSRAM, REGS = item("m_vram"), item("m_cram"), item("m_vsram"), item("m_regs")
  local NAMES = { "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "SP", "PC", "SR", "USP" }
  LIVE_IACK = cpu.spaces["cpu_space"]:install_read_tap(0xfffff0, 0xffffff, "live_iack", function(offset)
    if (offset & 0xf) >> 1 ~= 6 then return end
    vints = vints + 1
    if check then
      local regs = {}
      for i, n in ipairs(NAMES) do regs[i] = cpu.state[n].value & 0xffffffff end
      local r = {}
      for i = 0, 23 do r[#r + 1] = string.char(REGS:read(i) & 0xff) end
      record(vints, regs, sp:read_range(0xff0000, 0xffffff, 16, 2), regs[16], VRAM:read_block(0, 0x10000),
             CRAM:read_block(0, 128), VSRAM:read_block(0, 80), table.concat(r))
      vint_done(vints)
    end
    sync(vints)
    if stop_at and vints >= stop_at then manager.machine:exit() end
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
  if check then
    local function w(kind, p, r, v) snd_write(kind, p, r, v); out(fmt("Y %d %d %d %d %d", vints + 1, kind, p, r, v)) end
    LIVE_YM = sp:install_write_tap(0xa04000, 0xa04003, "live_ym", function(offset, data, mask)
      local p = (offset >> 1) & 1
      if (mask & 0xff00) ~= 0 then ym_addr[p + 1] = (data >> 8) & 0xff    -- even byte: the register
      else w(1, p, ym_addr[p + 1], data & 0xff) end                     -- odd byte: the data
    end)
    LIVE_PSG = sp:install_write_tap(0xc00010, 0xc00017, "live_psg", function(offset, data, mask)
      if (mask & 0x00ff) ~= 0 then w(2, 0, 0, data & 0xff) end
    end)
    LIVE_ZCMD = sp:install_write_tap(0xa01ffe, 0xa01fff, "live_zcmd", function(offset, data, mask)
      if (mask & 0x00ff) ~= 0 then w(3, 0, 0, data & 0xff) end
    end)
    -- the picture MAME drew: at the frame notifier after VINT k, pixels() hold the frame
    -- before's (what the state at VINT k - 1 draws, checked with render.c): frame k - 1
    local Q = levels()
    LIVE_PIC = emu.add_machine_frame_notifier(function()
      if vints < 1 or vints == last_pic then return end
      last_pic = vints
      local px, w = scr:pixels()
      out(fmt("P %d %s", vints - 1, hx(picture_hash(px, w, 0, 0, Q))))
      save_picture(vints - 1, px, w, 0, 0)
    end)
  end
end

local label = port and "m2-sonic: the i960 port on Model 2 (sfight)" or "MAME genesis: the cartridge"
local status, status_at = "", -100
emu.register_frame_done(function()
  scr:draw_text(2, 2, fmt("%s   frame %d%s", label, vints, alone and "  (partner gone: running alone)" or ""),
                0xffffffff, 0xc0000000)
  if not check then return end
  if vints - status_at >= 15 or vints < status_at then
    status_at = vints
    local f = io.open(dir .. "/status.txt")
    if f then status = f:read("l") or ""; f:close() end
  end
  if status ~= "" then scr:draw_text(2, 12, status, status:find("DIFF") and 0xffff8080 or 0xff80ff80, 0xc0000000) end
end)
