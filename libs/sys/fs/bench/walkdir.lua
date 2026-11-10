-- Walking a tree of 2 levels: 60 files and 4 directories of 60 files
-- (304 entries).
--
-- Few large directories rather than many small ones, and one loop per
-- benchmark file (also walkdir_depth.lua, walkdir_prune.lua): with many
-- small directories, or several loops sharing the iterator, the JIT's
-- traces (and the count) differ from run to run by up to 30%, depending
-- on which entry of the iterator gets hot first. That depends on where
-- LuaJIT's allocator, which is randomized, puts the bytecode.
local fs = require "lust.sys.fs"
local tree = dofile(arg[0]:gsub("[^/]*$", "") .. "_tree.lua")

local root, n = tree.tree(4, 2, 60)
assert(n == 304, n)

local acc = 0
for _ = 1, 60 do
  for _, _, type in fs.walkdir(root) do
    acc = acc + (type == "directory" and 2 or 1)
  end
end
tree.remove(root)
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 18480, acc)
