-- lust.json: JSON, fast.
--
--   build/src/lust examples/json.lua

local json = require("lust.json")

local doc = json.decode([[{"name": "lust", "libs": ["xxh", "pcre2", "json"], "extra": null}]])
print(doc.name, #doc.libs, doc.extra == json.null) --> lust  3  true

-- A table with the keys 1..n is an array, any other table an object (the
-- order of its keys is not defined)
print(json.encode({ 1, 2, { three = 3 } })) --> [1,2,{"three":3}]
print(json.encode({ name = "lust" }, { indent = 2 }))

-- {} is ambiguous, an object unless said otherwise
print(json.encode({ {}, json.empty_array, setmetatable({}, json.array_mt) })) --> [{},[],[]]

-- Numbers keep their value: the shortest text that reads back exactly
print(json.encode({ 0.1, 1 / 3, 2 ^ 53, 1e100 }))

-- Bad input is not an exception: nil and a message
print(json.decode("[1, 2,")) --> nil  unexpected end of data (at position 7)
print(json.encode({ f = print })) --> nil  cannot serialise function
print(json.encode(0 / 0)) --> nil  cannot serialise number: ...

-- Relaxed input, and the special numbers, on request
print(json.decode("[1, 2, /* three */ 3,]", { comments = true, trailing_commas = true })[3])
print(json.encode({ math.huge }, { nan_inf = true })) --> [Infinity]
