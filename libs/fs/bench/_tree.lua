-- Helpers of the benchmarks: scratch trees below a fresh temporary
-- directory, and their removal (no os.execute: perf would count the
-- instructions of the shell and rm, too).
local fs = require "lust.fs"

local M = {}

function M.touch(path)
  assert(io.open(path, "w")):close()
end

-- A new temporary directory with `files` files and `width` directories,
-- each of those again the same `depth` - 1 levels deep. Returns the
-- directory and the number of entries below it.
function M.tree(width, depth, files)
  local root, n = assert(fs.tempdir()), 0
  local function fill(dir, level)
    for i = 1, files do M.touch(dir .. "/file" .. i) end
    n = n + files
    if level == depth then return end
    for i = 1, width do
      local sub = dir .. "/dir" .. i
      assert(fs.mkdir(sub))
      n = n + 1
      fill(sub, level + 1)
    end
  end
  fill(root, 1)
  return root, n
end

-- Removes dir and everything below it: walkdir yields a directory before
-- its contents, so the reverse order removes the contents first.
function M.remove(dir)
  local paths = {}
  for d, name in fs.walkdir(dir) do paths[#paths + 1] = d .. "/" .. name end
  for i = #paths, 1, -1 do assert(os.remove(paths[i])) end
  assert(os.remove(dir))
end

return M
