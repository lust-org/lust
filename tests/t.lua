-- Minimal test harness: T.run{ name = function() ... end, ... }
-- Prints one line per test and exits non-zero if any failed.
local T = {}

function T.eq(got, expected, what)
  if got ~= expected then
    error(string.format("%s: expected %s, got %s",
      what or "values differ", tostring(expected), tostring(got)), 2)
  end
end

-- Assert that fn() raises an error matching the (plain) string `needle`.
function T.raises(fn, needle)
  local ok, err = pcall(fn)
  if ok then error("expected an error, got none", 2) end
  if not tostring(err):find(needle, 1, true) then
    error(string.format("expected error containing %q, got %q", needle, tostring(err)), 2)
  end
end

function T.run(tests)
  local names = {}
  for name in pairs(tests) do names[#names + 1] = name end
  table.sort(names)

  local failed = 0
  for _, name in ipairs(names) do
    local ok, err = pcall(tests[name])
    print(string.format("%s %s%s", ok and "ok  " or "FAIL", name,
      ok and "" or (": " .. tostring(err))))
    if not ok then failed = failed + 1 end
  end
  if failed > 0 then os.exit(1) end
end

return T
