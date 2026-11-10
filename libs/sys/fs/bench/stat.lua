-- stat of existing (also not following links) and missing files, and
-- readlink.
local fs = require "lust.sys.fs"
local tree = dofile(arg[0]:gsub("[^/]*$", "") .. "_tree.lua")

local dir = tree.tree(0, 1, 50)
local paths, missing = {}, {}
for i = 1, 50 do
  paths[i] = dir .. "/file" .. i
  missing[i] = dir .. "/nope" .. i
end
local nofollow = { follow = false }

local acc = 0
for _ = 1, 100 do
  for i = 1, 50 do
    local st = fs.stat(paths[i])
    acc = acc + st.size + (st.type == "file" and 1 or 0)
    acc = acc + (fs.stat(dir, nofollow).type == "directory" and 1 or 0)
    local v, _, code = fs.stat(missing[i])
    acc = acc + (v == nil and code == "ENOENT" and 1 or 0)
  end
  -- (a link that is always there, its target is the pid)
  acc = acc + (tonumber(fs.readlink("/proc/self")) and 1 or 0)
end
tree.remove(dir)
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 15100, acc)
