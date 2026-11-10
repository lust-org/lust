-- Creating (and removing) directories, also with missing parents, and
-- temporary files and directories.
local fs = require "lust.sys.fs"

local root = assert(fs.tempdir())
local parents = { parents = true }

local acc = 0
for i = 1, 100 do
  local d = root .. "/d" .. i
  acc = acc + (fs.mkdir(d) and 1 or 0)
  acc = acc + (select(3, fs.mkdir(d)) == "EEXIST" and 1 or 0)
  acc = acc + (fs.mkdir(d .. "/a/b/c", parents) and 1 or 0)
  acc = acc + (fs.mkdir(d .. "/a/b/c", parents) and 1 or 0)
  assert(os.remove(d .. "/a/b/c") and os.remove(d .. "/a/b") and os.remove(d .. "/a"))
  assert(os.remove(d))
end
local opts = { dir = root, suffix = ".tmp" }
local dopts = { dir = root }
for _ = 1, 100 do
  local f = assert(fs.tempfile(opts))
  local d = assert(fs.tempdir(dopts))
  acc = acc + #f - #d
  assert(os.remove(f) and os.remove(d))
end
assert(os.remove(root))
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 800, acc)
