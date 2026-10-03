-- optiming/trace.lua: trace the 68000 with its cycle count before each instruction
-- (OT_TRACE: the file), from the first frame on. Needs -debug.
local started = false
emu.register_frame_done(function()
  if started then return end
  manager.machine.debugger:command('trace ' .. os.getenv('OT_TRACE') .. ',maincpu,noloop,{tracelog "%d ",totalcycles}')
  manager.machine.debugger:command('go')
  started = true
end)
