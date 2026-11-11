-- realpath (existing and missing), getcwd and chmod: resolving paths and
-- changing permissions, as a script does before acting on its files.
local fs = require "lust.sys.fs"
local tree = dofile(arg[0]:gsub("[^/]*$", "") .. "_tree.lua")

local dir = tree.tree(0, 1, 50)
local paths, dotted, missing = {}, {}, {}
for i = 1, 50 do
  paths[i] = dir .. "/file" .. i
  dotted[i] = dir .. "/./file" .. i
  missing[i] = dir .. "/nope/../file" .. i
end
local rw, rwx = tonumber("600", 8), tonumber("700", 8)

local acc = 0
for _ = 1, 40 do
  for i = 1, 50 do
    acc = acc + (#fs.realpath(dotted[i]) > 0 and 1 or 0)
    local v, _, code = fs.realpath(missing[i])
    acc = acc + (v == nil and code == "ENOENT" and 1 or 0)
    acc = acc + (fs.chmod(paths[i], rw) and 1 or 0)
    acc = acc + (fs.chmod(paths[i], rwx) and 1 or 0)
  end
  acc = acc + (#fs.getcwd() > 0 and 1 or 0)
end
tree.remove(dir)
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 8040, acc)
