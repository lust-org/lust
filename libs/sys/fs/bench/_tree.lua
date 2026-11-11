-- Helpers of the benchmarks: scratch trees below a fresh temporary
-- directory, and their removal (no os.execute: perf would count the
-- instructions of the shell and rm, too).
local fs = require "lust.sys.fs"

local M = {}

-- The paths below each root M.tree made, in the order they were created
-- (a directory before its contents), for M.remove.
local created = {}

function M.touch(path)
  assert(io.open(path, "w")):close()
end

-- A new temporary directory with `files` files and `width` directories,
-- each of those again the same `depth` - 1 levels deep. Returns the
-- directory and the number of entries below it.
function M.tree(width, depth, files)
  local root, paths = assert(fs.tempdir()), {}
  local function fill(dir, level)
    for i = 1, files do
      local file = dir .. "/file" .. i
      M.touch(file)
      paths[#paths + 1] = file
    end
    if level == depth then return end
    for i = 1, width do
      local sub = dir .. "/dir" .. i
      assert(fs.mkdir(sub))
      paths[#paths + 1] = sub
      fill(sub, level + 1)
    end
  end
  fill(root, 1)
  created[root] = paths
  return root, #paths
end

-- Removes a tree M.tree made: its paths in reverse order, the contents
-- of a directory before it. Not with walkdir: a walk in the cleanup sends
-- the traces the benchmark compiled for it down paths they did not take
-- yet (e.g. into subdirectories after a walk with max_depth = 1). Whether
-- LuaJIT then records side traces that keep aborting depends on its
-- randomized trace penalties, which made a run cost 50% more now and then.
function M.remove(root)
  local paths = assert(created[root], "not a tree of M.tree")
  for i = #paths, 1, -1 do assert(os.remove(paths[i])) end
  assert(os.remove(root))
  created[root] = nil
end

return M
