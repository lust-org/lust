-- stat into one reused table (fs.stat(path, opts, t)), of existing and
-- missing files. A file of its own: stat.lua measures the new table per
-- call, and loops in one file share traces (see walkdir.lua).
local fs = require "lust.fs"
local tree = dofile(arg[0]:gsub("[^/]*$", "") .. "_tree.lua")

local dir = tree.tree(0, 1, 50)
local paths, missing = {}, {}
for i = 1, 50 do
  paths[i] = dir .. "/file" .. i
  missing[i] = dir .. "/nope" .. i
end
local t = {}

local acc = 0
for _ = 1, 100 do
  for i = 1, 50 do
    local st = fs.stat(paths[i], nil, t)
    acc = acc + st.size + (st.type == "file" and 1 or 0)
    local v, _, code = fs.stat(missing[i], nil, t)
    acc = acc + (v == nil and code == "ENOENT" and 1 or 0)
  end
end
tree.remove(dir)
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 10000, acc)
