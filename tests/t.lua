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

-- Runs fn() while a __gc finalizer calls inner() whenever the garbage
-- collector finalizes, i.e. inside the allocations of the library calls
-- in fn (see docs/gc-reentrancy.md). Returns how often inner ran. fn
-- should spend its allocations in the library (big inputs, a loop), so
-- that inner runs within its calls; the GC runs eagerly meanwhile.
--
-- LuaJIT drops errors raised in finalizers: inner's first one is raised
-- here instead. The finalizer is disarmed before T.during_gc returns: at
-- exit, a stock luajit may have unloaded the library's .so before the
-- last finalizers run.
function T.during_gc(fn, inner)
  local armed, count, inner_err = true, 0, nil
  local function arm()
    local p = newproxy(true)
    getmetatable(p).__gc = function()
      if not armed then return end
      count = count + 1
      local ok, err = pcall(inner)
      if not ok and inner_err == nil then inner_err = tostring(err) end
      arm()
    end
  end
  local pause = collectgarbage("setpause", 100)
  local stepmul = collectgarbage("setstepmul", 1000)
  arm()
  local ok, err = pcall(fn)
  armed = false
  collectgarbage("setpause", pause)
  collectgarbage("setstepmul", stepmul)
  if not ok then error(err, 0) end
  if inner_err ~= nil then error("in a finalizer: " .. inner_err, 0) end
  return count
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
