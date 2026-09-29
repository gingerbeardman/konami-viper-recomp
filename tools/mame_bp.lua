-- MAME oracle script: env MAMEBP="addr[,addr...]", MAMEBP_FMT extra printf args (default regs),
-- MAMEBP_MAX max hits per bp. Output lines "BPHIT <addr> ..." on stdout.
local bps = os.getenv("MAMEBP") or ""
local extra = os.getenv("MAMEBP_EXTRA") or ""
local maxhits = tonumber(os.getenv("MAMEBP_MAX") or "20")
local seen, started = 0, false
emu.register_frame_done(function()
  local d = manager.machine.debugger
  if not d then return end
  if not started then
    started = true
    for a in string.gmatch(bps, "[^,]+") do
      local fmt = 'BPHIT ' .. a .. ' r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x r8=%08x lr=%08x r1=%08x'
      local args = 'r3,r4,r5,r6,r7,r8,lr,r1'
      if extra ~= "" then fmt = fmt .. ' m=%08x'; args = args .. ',' .. extra end
      d:command(string.format('bpset %s,1,{printf "%s",%s; g}', a, fmt, args))
    end
    d:command('g')
  end
  local log = d.consolelog
  for i = seen + 1, #log do
    if string.find(log[i], "BPHIT", 1, true) then print(string.format("%.4f %s", manager.machine.time:as_double(), log[i])) end
  end
  seen = #log
end)
