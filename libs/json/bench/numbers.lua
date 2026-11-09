-- Numbers: integers, doubles that need the shortest round trip form, and
-- big/small exponents.
local json = require "lust.json"

local nums = {}
for i = 1, 5000 do
  nums[#nums + 1] = i * 7919 % 100003
  nums[#nums + 1] = i / 7
  nums[#nums + 1] = i * 1e-9 + 0.1
  nums[#nums + 1] = i * 1e15
end

local acc = 0
for _ = 1, 4 do
  local j = json.encode(nums)
  acc = acc + #j
  local back = json.decode(j)
  assert(#back == #nums and back[3] == nums[3] and back[#back] == nums[#nums])
end
assert(acc == 1184428, acc)
