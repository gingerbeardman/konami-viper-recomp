-- env MAMEDUMP_T=seconds MAMEDUMP_OUT=file : dump the 16MB RAM of the running Viper game at emulated time T
local t = tonumber(os.getenv("MAMEDUMP_T") or "8")
local out = os.getenv("MAMEDUMP_OUT") or "ram.bin"
local done = false
emu.register_frame_done(function()
  if done or manager.machine.time:as_double() < t then return end
  done = true
  local sp = manager.machine.devices[":maincpu"].spaces["program"]
  local f = io.open(out, "wb")
  local chunk = {}
  for a = 0, 0xffffff, 4 do
    local v = sp:read_u32(a)
    chunk[#chunk + 1] = string.char((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255)
    if #chunk == 4096 then f:write(table.concat(chunk)); chunk = {} end
  end
  f:write(table.concat(chunk)); f:close()
  print(string.format("dumped RAM at %.3f", manager.machine.time:as_double()))
  manager.machine:exit()
end)
