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
    assert(maps:find("/lust/" .. libs[1] .. ".so", 1, true), libs[1] .. " not mapped")
    for i = 2, #libs do
      assert(not maps:find("/lust/" .. libs[i] .. ".so", 1, true), libs[i] .. " mapped")
    end
  end,

  static_libraries_are_preloaded = function()
    if mode ~= "static" then return end
    for _, lib in ipairs(libs) do
      T.eq(type(package.preload["lust." .. lib]), "function", lib)
    end
  end,
}
