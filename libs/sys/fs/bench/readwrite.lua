-- Writing (emptying, appending, atomically replacing) and reading whole
-- files, small ones in the State's buffer and a large one, as scripts do
-- with config and data files.
local fs = require "lust.sys.fs"

local root = assert(fs.tempdir())
local small, large = root .. "/small", root .. "/large"
local line = ("0123456789abcdef"):rep(4) .. "\n"
local big = line:rep(4000) -- (260000 bytes, beyond the State's buffer)
local append, atomic = { append = true }, { atomic = true }

local acc = 0
for i = 1, 200 do
  assert(fs.write(small, line))
  assert(fs.write(small, line, append))
  acc = acc + #fs.read(small)
  if i % 10 == 0 then
    assert(fs.write(small, line, atomic))
    acc = acc + #fs.read(small)
  end
end
assert(fs.write(large, big))
for _ = 1, 20 do acc = acc + #fs.read(large) end
assert(os.remove(small) and os.remove(large) and os.remove(root))
-- The result, so the work can neither be skipped nor silently go wrong.
assert(acc == 200 * 130 + 20 * 65 + 20 * 260000, acc)
