-- Listing a directory of 300 files, names only and with types.
local fs = require "lust.sys.fs"
local tree = dofile(arg[0]:gsub("[^/]*$", "") .. "_tree.lua")

local dir = tree.tree(0, 1, 300)
local opts = { types = true }

local acc = 0
for _ = 1, 100 do
  acc = acc + #assert(fs.list(dir))
  local names, types = assert(fs.list(dir, opts))
  acc = acc + #names + (types[1] == "file" and 1 or 0)
end
tree.remove(dir)
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 60100, acc)
