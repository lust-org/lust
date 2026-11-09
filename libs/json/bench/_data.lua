-- Deterministic test data for the benchmarks (helper, not a benchmark).
-- Everything is derived from the index, no randomness, so the checksums
-- the benchmarks assert are stable.
local M = {}

-- The JSON text of n records, built without the library under test.
function M.records_text(n)
  local parts = {}
  for i = 1, n do
    parts[i] = string.format(
      '{"id":%d,"name":"user-%d","email":"user%d@example.com","active":%s,'
        .. '"score":%.3f,"tags":["alpha","beta","gamma%d"],'
        .. '"address":{"city":"City %d","zip":"%05d","geo":[%.4f,%.4f]},'
        .. '"note":null}',
      i, i, i, i % 3 == 0 and "true" or "false", i * 1.37,
      i % 10, i % 97, i % 100000, i * 0.001, -i * 0.002)
  end
  return "[" .. table.concat(parts, ",") .. "]"
end

-- The same records as a Lua value.
function M.records(n)
  local t = {}
  for i = 1, n do
    t[i] = {
      id = i,
      name = "user-" .. i,
      email = "user" .. i .. "@example.com",
      active = i % 3 == 0,
      score = i * 1.37,
      tags = { "alpha", "beta", "gamma" .. i % 10 },
      address = { city = "City " .. i % 97, zip = string.format("%05d", i % 100000), geo = { i * 0.001, -i * 0.002 } },
    }
  end
  return t
end

return M
