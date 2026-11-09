-- Iterating: gmatch over all words, and over key=value pairs.
local re = require "lust.re2"
local lines = dofile(arg[0]:gsub("[^/]*$", "") .. "_lines.lua")(2000)

local word = assert(re.new("[a-z]+"))
local kv = assert(re.new("(\\w+)=(\\w+)"))

local n = 0
for _ = 1, 3 do
  for i = 1, #lines do
    local l = lines[i]
    for w in word:gmatch(l) do n = n + #w end
    for k, v in kv:gmatch(l) do n = n + #k + #v end
  end
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(n == 360234, n)
