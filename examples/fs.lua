-- lust.fs: list, walk and inspect directories, create directories and
-- temporary files.
--
--   build/src/lust examples/fs.lua

local fs = require "lust.fs"

-- A scratch directory only we can access, with a small tree in it.
local root = assert(fs.tempdir { prefix = "fs-example-" })
assert(fs.mkdir(root .. "/src/lib", { parents = true }))
for _, name in ipairs { "src/main.lua", "src/lib/util.lua", "README" } do
  assert(io.open(root .. "/" .. name, "w")):close()
end

-- The entries of one directory, with their types (no stat needed).
local names, types = assert(fs.list(root, { types = true }))
for i, name in ipairs(names) do print("list", name, types[i]) end

-- Everything below it, a directory before its contents: the directory an
-- entry is in and its name (join them only where the path is needed).
for dir, name, type in fs.walkdir(root) do
  print("walk", (dir .. "/" .. name):sub(#root + 2), type)
end

-- File metadata; failures return nil, a message and the errno name.
local st = assert(fs.stat(root .. "/README"))
print("stat", st.type, st.size, string.format("%o", st.mode))
print("stat", fs.stat(root .. "/missing")) --> nil  .../missing: no such file or directory  ENOENT
-- In a loop, one table filled again and again costs half as much.
local reused = {}
for _, name in ipairs { "src", "README" } do
  print("stat", name, fs.stat(root .. "/" .. name, nil, reused).type)
end

-- A temporary file only we can access, to be opened by name.
local tmp = assert(fs.tempfile { dir = root, suffix = ".txt" })
local f = assert(io.open(tmp, "w"))
f:write("hello\n")
f:close()
print("tempfile", tmp:sub(#root + 2), fs.stat(tmp).size)

print("readlink", fs.readlink("/proc/self/exe"))

-- Clean up: walkdir yields a directory before its contents, so removing in
-- reverse order empties every directory before removing it.
local paths = {}
for dir, name in fs.walkdir(root) do paths[#paths + 1] = dir .. "/" .. name end
for i = #paths, 1, -1 do assert(os.remove(paths[i])) end
assert(os.remove(root))
