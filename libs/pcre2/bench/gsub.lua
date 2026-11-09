-- Replacing: gsub with a template (group references), a function, a
-- table and a limit.
local re = require "lust.pcre2"
local lines = dofile(arg[0]:gsub("[^/]*$", "") .. "_lines.lua")(2000)

local mail = assert(re.new("(\\w+)@([\\w.]+)"))
local num = assert(re.new("\\d+"))
local level = assert(re.new("DEBUG|INFO|WARN|ERROR"))
local space = assert(re.new(" "))
local short = { DEBUG = "D", INFO = "I", WARN = "W", ERROR = "E" }

local n = 0
for _ = 1, 2 do
  for i = 1, #lines do
    local l = lines[i]
    local s, k = mail:gsub(l, "\\2/\\1")
    n = n + #s + k
    s, k = num:gsub(l, function(d) return #d end)
    n = n + #s + k
    s, k = level:gsub(l, short)
    n = n + #s + k
    s, k = space:gsub(l, "_", 3)
    n = n + #s + k
  end
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(n == 1458628, n)
