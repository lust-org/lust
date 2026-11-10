local T = require "t"
local json = require "lust.json"

local null = json.null

-- Structural equality, order of table keys does not matter.
local function same(a, b)
  if type(a) ~= type(b) then return false end
  if type(a) ~= "table" then return a == b end
  for k, v in pairs(a) do if not same(v, b[k]) then return false end end
  for k in pairs(b) do if a[k] == nil then return false end end
  return true
end

-- A call that failed softly: nil and a message containing `needle`.
local function fails(needle, v, err)
  T.eq(v, nil, "result")
  assert(type(err) == "string" and err:find(needle, 1, true), tostring(err))
end

local function roundtrip(v)
  local s = json.encode(v)
  assert(same(json.decode(s), v), s)
  return s
end

T.run {
  decode_scalars = function()
    T.eq(json.decode("1"), 1)
    T.eq(json.decode(" -12.5e1 "), -125)
    T.eq(json.decode("true"), true)
    T.eq(json.decode("false"), false)
    T.eq(json.decode("null"), null)
    T.eq(json.decode('"a\\n\\u00e9\\ud83d\\ude00"'), "a\n\195\169\240\159\152\128")
    T.eq(json.decode("18446744073709551615"), 18446744073709551615)
    T.eq(json.decode("-9223372036854775808"), -9223372036854775808)
  end,

  decode_containers = function()
    local t = json.decode('{"a":[1,2,{"b":null}],"c":"d","e":{}}')
    T.eq(t.a[1], 1)
    T.eq(t.a[2], 2)
    T.eq(t.a[3].b, null)
    T.eq(t.c, "d")
    T.eq(next(t.e), nil)
    T.eq(#json.decode("[null,null,3]"), 3)
    T.eq(#json.decode("[]"), 0)
  end,

  decode_keeps_embedded_zeros = function()
    local t = json.decode('{"a\\u0000b":"c\\u0000d"}')
    T.eq(t["a\0b"], "c\0d")
  end,

  decode_duplicate_keys_last_wins = function()
    T.eq(json.decode('{"a":1,"a":2}').a, 2)
  end,

  decode_numeric_keys_stay_strings = function()
    local t = json.decode('{"1":"x"}')
    T.eq(t["1"], "x")
    T.eq(t[1], nil)
  end,

  decode_errors = function()
    fails("length is 0", json.decode(""))
    fails("(at position", json.decode("{"))
    fails("(at position", json.decode("[1,]"))
    fails("(at position", json.decode("nul"))
    fails("(at position", json.decode("[1] x"))
    fails("(at position", json.decode("'a'"))
    fails("(at position", json.decode('"\255"'))
    fails("(at position", json.decode("NaN"))
    -- positions are 1-based like string.find: they point at the offending
    -- byte, or right behind the data when it ended too early
    fails("(at position 5)", json.decode("[1] x"))
    fails("(at position 6)", json.decode('{"a" 1}'))
    fails("(at position 7)", json.decode("[1, 2,"))
    T.raises(function() json.decode() end, "bad argument #1")
    -- the module is still fine afterwards
    T.eq(json.decode("[1]")[1], 1)
  end,

  decode_options = function()
    T.eq(json.decode("[1,2,]", { trailing_commas = true })[2], 2)
    T.eq(json.decode("[1,/*c*/2]", { comments = true })[2], 2)
    local n = json.decode("[NaN,-Infinity]", { nan_inf = true })
    assert(n[1] ~= n[1] and n[2] == -math.huge)
    T.eq(json.decode('"\255"', { invalid_unicode = true }), "\255")
    T.raises(function() json.decode("[1]", { bogus = true }) end, "unknown option 'bogus'")
    T.raises(function() json.decode("[1]", { max_depth = "x" }) end, "must be a number")
    T.raises(function() json.decode("[1]", 1) end, "bad argument #2")
  end,

  decode_depth = function()
    local function nested(n) return ("["):rep(n) .. ("]"):rep(n) end
    json.decode(nested(1000))
    fails("nesting too deep", json.decode(nested(1001)))
    json.decode(nested(5), { max_depth = 5 })
    fails("nesting too deep", json.decode(nested(6), { max_depth = 5 }))
    -- nothing blows up (C stack, Lua stack) with absurd depths either
    fails("", json.decode(nested(1e6), { max_depth = 1e6 }))
    fails("", json.decode(nested(1e6)))
    T.eq(json.decode("[1]")[1], 1)
  end,

  encode_scalars = function()
    T.eq(json.encode(nil), "null")
    T.eq(json.encode(null), "null")
    T.eq(json.encode(true), "true")
    T.eq(json.encode(false), "false")
    T.eq(json.encode(0), "0")
    T.eq(json.encode(-0.0), "0")
    T.eq(json.encode(-17), "-17")
    T.eq(json.encode(1234567890123), "1234567890123")
    T.eq(json.encode(2 ^ 53), "9007199254740992")
    T.eq(json.encode(0.1), "0.1")
    T.eq(json.encode(-1.5e-7), "-1.5e-7")
    T.eq(json.encode(1e300), "1e300")
    T.eq(json.encode(1 / 3), "0.3333333333333333")
    T.eq(json.encode("x"), '"x"')
  end,

  encode_numbers_roundtrip = function()
    for _, n in ipairs { 0.1, 1 / 3, math.pi, 1e-300, 2 ^ 63, -2 ^ 70, 123456789.125, 5e-324, 1.7976931348623157e308 } do
      T.eq(json.decode(json.encode(n)), n)
    end
  end,

  encode_string_escapes = function()
    T.eq(json.encode('a"b\\c/d'), '"a\\"b\\\\c/d"')
    T.eq(json.encode("\n\r\t\b\f"), '"\\n\\r\\t\\b\\f"')
    T.eq(json.encode("\0\1\31"), '"\\u0000\\u0001\\u001f"')
    T.eq(json.encode("\127"), '"\127"')
    T.eq(json.encode("é😀"), '"é😀"')
    -- all positions relative to the 8 byte blocks the scanner works in
    for n = 0, 40 do
      for _, c in ipairs { '"', "\\", "\n", "\1" } do
        for pos = 1, n + 1 do
          local s = ("x"):rep(pos - 1) .. c .. ("y"):rep(n + 1 - pos)
          T.eq(json.decode(json.encode(s)), s)
        end
      end
    end
    local all = {}
    for i = 0, 255 do all[#all + 1] = string.char(i) end
    local s = table.concat(all):gsub("[\128-\255]", "")
    T.eq(json.decode(json.encode(s)), s)
    local long = ("abc\"def\\"):rep(10000)
    T.eq(json.decode(json.encode(long)), long)
  end,

  encode_arrays = function()
    T.eq(json.encode({}), "{}")
    T.eq(json.encode({ 1, 2, 3 }), "[1,2,3]")
    T.eq(json.encode({ "a", { true, null }, {} }), '["a",[true,null],{}]')
    T.eq(json.encode({ [3] = "c", [2] = "b", [1] = "a" }), '["a","b","c"]')
    -- more than the array part holds
    local t = {}
    for i = 1, 1000 do t[i] = i end
    T.eq(#json.decode(json.encode(t)), 1000)
  end,

  encode_objects = function()
    T.eq(json.encode({ a = 1 }), '{"a":1}')
    T.eq(json.encode({ [1] = "a", [3] = "c" }):find('"1":"a"', 1, true) ~= nil, true) -- sparse: object
    T.eq(json.encode({ 1, 2, x = 3 }):find('"x":3', 1, true) ~= nil, true) -- mixed: object
    local s = json.encode({ [1.5] = true })
    T.eq(s, '{"1.5":true}')
    T.eq(json.encode({ a = { b = { c = {} } } }), '{"a":{"b":{"c":{}}}}')
    T.eq(json.encode({ ["a\"b"] = 1 }), '{"a\\"b":1}')
  end,

  encode_empty_arrays = function()
    T.eq(json.encode(setmetatable({}, json.array_mt)), "[]")
    T.eq(json.encode(json.empty_array), "[]")
    T.eq(json.encode({ a = json.empty_array, b = setmetatable({}, json.array_mt) }):gsub('"%a"', ""), "{:[],:[]}")
    T.eq(json.encode(setmetatable({ 1, 2 }, json.array_mt)), "[1,2]")
    T.eq(json.encode(setmetatable({}, {})), "{}") -- some other metatable
  end,

  encode_ignores_metamethods = function()
    local t = setmetatable({}, { __index = function() return 1 end, __len = function() return 5 end })
    T.eq(json.encode(t), "{}")
  end,

  encode_pretty = function()
    T.eq(json.encode({ 1, { 2 }, {} }, { indent = 2 }), "[\n  1,\n  [\n    2\n  ],\n  {}\n]")
    T.eq(json.encode({ a = { b = 1 } }, { indent = 4 }), '{\n    "a": {\n        "b": 1\n    }\n}')
    T.eq(json.encode({ a = 1 }, { indent = 0 }), '{"a":1}')
    T.eq(json.encode({}, { indent = 2 }), "{}")
    T.eq(json.encode(json.empty_array, { indent = 2 }), "[]")
  end,

  encode_errors = function()
    fails("cannot serialise function", json.encode(print))
    fails("cannot serialise function", json.encode({ f = print }))
    fails("cannot serialise thread", json.encode(coroutine.create(print)))
    fails("cannot serialise userdata", json.encode(io.stdout))
    fails("cannot serialise table key of type table", json.encode({ [{}] = 1 }))
    fails("cannot serialise table key of type boolean", json.encode({ [true] = 1 }))
    T.raises(function() json.encode() end, "bad argument #1")
    T.raises(function() json.encode(1, { bogus = 1 }) end, "unknown option 'bogus'")
    T.raises(function() json.encode(1, { indent = -1 }) end, "indent must be an integer")
    T.raises(function() json.encode(1, 5) end, "bad argument #2")
    -- an error half way leaves nothing behind
    T.eq(json.encode({ 1, 2 }), "[1,2]")
  end,

  errors_are_returned_not_raised = function()
    -- (misuse raises, see decode_errors and encode_errors)
    local ok, v, err = pcall(json.decode, "{")
    T.eq(ok, true)
    T.eq(v, nil)
    assert(err:find("position", 1, true))
    ok, v, err = pcall(json.encode, print)
    T.eq(ok, true)
    T.eq(v, nil)
    assert(err:find("function", 1, true))
  end,

  encode_nan_inf = function()
    fails("NaN or Infinity", json.encode(0 / 0))
    fails("NaN or Infinity", json.encode(math.huge))
    fails("NaN or Infinity", json.encode({ -math.huge }))
    fails("NaN or Infinity", json.encode({ [math.huge] = 1 }))
    T.eq(json.encode({ 0 / 0, math.huge, -math.huge }, { nan_inf = true }), "[NaN,Infinity,-Infinity]")
    local back = json.decode("[NaN,Infinity,-Infinity]", { nan_inf = true })
    assert(back[1] ~= back[1] and back[2] == math.huge and back[3] == -math.huge)
  end,

  encode_depth_and_cycles = function()
    local t = {}
    t[1] = t
    fails("nesting too deep", json.encode(t))
    local o = {}
    o.self = o
    fails("nesting too deep", json.encode(o))
    local deep = {}
    local cur = deep
    for _ = 1, 999 do cur[1] = {}; cur = cur[1] end
    json.encode(deep) -- 1000 levels
    cur[1] = {}
    fails("nesting too deep", json.encode(deep))
    json.encode(deep, { max_depth = 1001 })
    fails("nesting too deep", json.encode({ { 1 } }, { max_depth = 1 }))
    -- absurd depth does not overflow any stack
    local a = {}
    cur = a
    for _ = 1, 100000 do cur[1] = {}; cur = cur[1] end
    fails("", json.encode(a, { max_depth = 1e6 }))
    T.eq(json.encode({ 1 }), "[1]")
  end,

  roundtrips = function()
    roundtrip({})
    roundtrip({ 1, 2.5, "x", true, false, null, { a = { b = { 1, 2, { c = "d" } } } } })
    roundtrip({ ["with space"] = 1, ["é"] = "ü", [""] = "" })
    local big = {}
    for i = 1, 5000 do big[i] = { id = i, name = "n" .. i, tags = { "a", "b" }, v = i / 7 } end
    roundtrip(big)
  end,

  large_documents = function()
    -- beyond the size of the reused output buffer
    local s = ("x"):rep(3 * 1024 * 1024)
    T.eq(json.decode(json.encode({ s }))[1], s)
    T.eq(json.encode("small"), '"small"')
    T.eq(#json.encode(s), #s + 2)
  end,

  garbage_collection = function()
    -- many conversions, no leaks or crashes under a collector working hard
    for i = 1, 2000 do
      local v = json.decode('{"k' .. i .. '":[1,2,3,{"a":"b"}]}')
      collectgarbage("step")
      assert(json.encode(v):find("^{"))
    end
    collectgarbage()
  end,
}
