-- Walking the tree of walkdir.lua with a max_depth (64 entries of 304).
local fs = require "lust.sys.fs"
local tree = dofile(arg[0]:gsub("[^/]*$", "") .. "_tree.lua")

local root = tree.tree(4, 2, 60)
local opts = { max_depth = 1 }

local acc = 0
for _ = 1, 200 do
  for _ in fs.walkdir(root, opts) do acc = acc + 1 end
end
tree.remove(root)
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 12800, acc)
