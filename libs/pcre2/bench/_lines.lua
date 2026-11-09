-- Not a benchmark: shared by the others (dofile), returns a function that
-- builds n deterministic log lines like
--   "2024-03-07 12:34:56 WARN alice@db3.example.com: id=123 took=45ms ..."
return function(n)
  local seed = 1
  local function rnd(m) -- LCG, so the input never changes
    seed = (seed * 1103515245 + 12345) % 2147483648
    return seed % m
  end
  local levels = { "DEBUG", "INFO", "WARN", "ERROR" }
  local users = { "alice", "bob", "carol", "dave", "eve" }
  local words = { "disk", "full", "retry", "timeout", "connected", "cache", "miss", "ok" }
  local lines = {}
  for i = 1, n do
    local msg = {}
    for j = 1, 3 + rnd(6) do msg[j] = words[rnd(#words) + 1] end
    lines[i] = ("2024-%02d-%02d %02d:%02d:%02d %s %s@db%d.example.com: id=%d took=%dms %s"):format(
      rnd(12) + 1, rnd(28) + 1, rnd(24), rnd(60), rnd(60),
      levels[rnd(#levels) + 1], users[rnd(#users) + 1], rnd(10),
      rnd(100000), rnd(1000), table.concat(msg, " "))
  end
  return lines
end
