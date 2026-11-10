-- Building the versions table, many times over.
local meta = require "lust.meta"

local n = 0
for _ = 1, 20000 do
  local v = meta.versions()
  n = n + #v.lust + #v.pcre2
end
-- The result, so the work can neither be skipped nor silently go wrong.
local v = meta.versions()
assert(n == 20000 * (#v.lust + #v.pcre2), n)
