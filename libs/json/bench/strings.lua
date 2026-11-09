-- Strings: long plain ones, ones full of escapes, non-ASCII.
local json = require "lust.json"

local plain = ("The quick brown fox jumps over the lazy dog. "):rep(2000)
local escaped = ('say "hi"\n\tpath\\to\\file\1'):rep(2000)
local utf8 = ("ünïcödé 日本語 😀 "):rep(2000)

local acc = 0
for _ = 1, 5 do
  for _, s in ipairs { plain, escaped, utf8 } do
    local j = json.encode(s)
    acc = acc + #j
    assert(json.decode(j) == s)
  end
end
assert(acc == 1060030, acc)
