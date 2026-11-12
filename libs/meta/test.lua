local T = require "t"
local meta = require "lust.meta"

-- What each known version looks like (the dependencies are only there
-- when the libraries bundling them are in the build)
local formats = {
  lust = "^%d+%.%d+%.%d+$",
  luajit = "^%d+%.%d+%.%d+$",
  xxhash = "^%d+%.%d+%.%d+$",
  yyjson = "^%d+%.%d+%.%d+$",
  pcre2 = "^10%.%d+ %d%d%d%d%-%d%d%-%d%d$", -- (as pcre2_config's)
  mbedtls = "^%d+%.%d+%.%d+$",
}

T.run {
  versions = function()
    local v = meta.versions()
    assert(v.lust, "no lust version")
    -- (no jit library: PUC Lua 5.1, no LuaJIT version either)
    if jit then T.eq("LuaJIT " .. v.luajit, jit.version) else T.eq(v.luajit, nil) end
    for k, s in pairs(v) do
      assert(formats[k], "unknown key " .. tostring(k))
      assert(type(s) == "string" and s:match(formats[k]), k .. ": " .. tostring(s))
    end
  end,

  versions_is_a_new_table_each_call = function()
    local a = meta.versions()
    a.lust = nil
    assert(a ~= meta.versions() and meta.versions().lust)
  end,
}
