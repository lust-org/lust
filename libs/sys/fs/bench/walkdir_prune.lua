-- Walking the tree of walkdir.lua, pruning "dir1" (prune is called per
-- directory).
local fs = require "lust.sys.fs"
local tree = dofile(arg[0]:gsub("[^/]*$", "") .. "_tree.lua")

local root = tree.tree(4, 2, 60)
local opts = { prune = function(path) return path:byte(-1) == 49 end } -- ("1")

local acc = 0
for _ = 1, 60 do
  for _ in fs.walkdir(root, opts) do acc = acc + 1 end
end
tree.remove(root)
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 14640, acc)
