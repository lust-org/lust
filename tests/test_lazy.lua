-- Using one library must not load any other one.
-- usage: <interpreter> test_lazy.lua <shared|static> <lib> <lib> ...
local T = require "t"
local mode = arg[1]
local libs = { unpack(arg, 2) }

T.run {
  only_the_required_library_is_loaded = function()
    require("lust." .. libs[1])
    for i = 2, #libs do
      T.eq(package.loaded["lust." .. libs[i]], nil, libs[i] .. " loaded")
    end
  end,

  shared_libraries_are_not_even_mapped = function()
    if mode ~= "shared" then return end
    local f = assert(io.open("/proc/self/maps"))
    local maps = f:read("*a")
    f:close()
    -- (lust/sys/env.so for sys.env)
    local function so(lib) return "/lust/" .. lib:gsub("%.", "/") .. ".so" end
    assert(maps:find(so(libs[1]), 1, true), libs[1] .. " not mapped")
    for i = 2, #libs do
      assert(not maps:find(so(libs[i]), 1, true), libs[i] .. " mapped")
    end
  end,

  -- Embedded libraries come from lust's own searcher: no file lookup, and
  -- nothing registered per library at startup.
  static_libraries_are_embedded = function()
    if mode ~= "static" then return end
    for name in pairs(package.preload) do
      assert(not name:find("^lust%."), name .. " registered in package.preload")
    end
    for _, lib in ipairs(libs) do
      T.eq(type(package.loaders[2]("lust." .. lib)), "function", lib)
    end
    T.eq(package.loaders[2]("lust.does_not_exist"), nil)
  end,
}
