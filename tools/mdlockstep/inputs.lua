-- mdlockstep/inputs.lua: the input script both recorders play: Mega Drive pad 1 as read by
-- the k-th VINT's handler (1-based), active low (MAME's PAD1 bits: 01 up 02 down 04 left 08 right
-- 10 B 20 C 40 A 80 start). Start at the title every 4000 frames (not during play: it
-- pauses), then from a fixed LCG a new direction every 23 frames (right, mostly) and a jump
-- (C) every 41. LS_FRAMES frames; files go to LS_OUT.
local function lcg(n)
  local x = 24680
  for _ = 1, (n % 97) + 1 do x = (x * 1103515245 + 12345) & 0xffffffff end
  return x
end
function LS_INPUTS(k)
  local pad = 0xff
  local m = k % 4000
  if m >= 450 and m < 456 then return pad & ~0x80 end
  if m < 700 then return pad end
  local r = (lcg(k // 23 + (k // 4000) * 11) >> 16) & 7
  local dir = ({ 0x08, 0x08, 0x08, 0x08, 0x04, 0x02, 0x01, 0x00 })[r + 1]
  pad = pad & ~dir
  if k % 41 < 12 then pad = pad & ~0x20 end
  return pad
end
LS_FRAMES = tonumber(os.getenv("LS_FRAMES") or "3000")
LS_OUT = os.getenv("LS_OUT") or "/tmp/mdlockstep"
