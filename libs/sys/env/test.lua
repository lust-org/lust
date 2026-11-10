local T = require "t"
local env = require "lust.sys.env"

-- (a name no one else uses)
local NAME = "LUST_SYS_ENV_TEST"

-- Whether a child process sees NAME with value v.
local function child_sees(v)
  local r = os.execute('test "$' .. NAME .. '" = "' .. v .. '"')
  return r == 0 or r == true -- (true with LUAJIT_ENABLE_LUA52COMPAT)
end

T.run {
  set_is_seen_by_getenv_and_children = function()
    env.set(NAME, "a b")
    T.eq(os.getenv(NAME), "a b", "getenv")
    assert(child_sees("a b"), "child")
    env.set(NAME, "")
    T.eq(os.getenv(NAME), "", "empty value")
    env.set(NAME, 12)
    T.eq(os.getenv(NAME), "12", "number value")
    env.unset(NAME)
  end,

  unset_removes = function()
    env.set(NAME, "x")
    env.unset(NAME)
    T.eq(os.getenv(NAME), nil, "getenv")
    assert(child_sees(""), "child")
    env.unset(NAME) -- (not set: no error)
  end,

  misuse_raises = function()
    T.raises(function() env.set(NAME) end, "bad argument #2")
    T.raises(function() env.set(nil, "x") end, "bad argument #1")
    T.raises(function() env.set("", "x") end, "name must be non-empty")
    T.raises(function() env.set("A=B", "x") end, "without '='")
    T.raises(function() env.set("A\0B", "x") end, "zero bytes")
    T.raises(function() env.set(NAME, "a\0b") end, "value contains a zero byte")
    T.raises(function() env.unset() end, "bad argument #1")
    T.raises(function() env.unset("A=B") end, "without '='")
    T.eq(os.getenv(NAME), nil, "nothing set")
  end,
}
