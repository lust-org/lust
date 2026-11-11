local T = require "t"
local signal = require "lust.sys.signal"

-- Sends signals to this process (the shell's parent), e.g. "USR1 USR2".
-- They arrive while os.execute waits for the shell, the callbacks run
-- right after it returned.
local function send(names)
  local cmd = ""
  for name in names:gmatch("%S+") do cmd = cmd .. "kill -" .. name .. " $PPID; " end
  local r = os.execute(cmd)
  assert(r == 0 or r == true, cmd) -- (true with LUAJIT_ENABLE_LUA52COMPAT)
end

T.run {
  callback_runs_with_the_name = function()
    local got = {}
    signal.on("SIGUSR1", function(name) got[#got + 1] = name end)
    send("USR1")
    T.eq(table.concat(got, ","), "SIGUSR1", "calls")
    send("USR1")
    T.eq(table.concat(got, ","), "SIGUSR1,SIGUSR1", "calls")
    signal.on("SIGUSR1", nil)
  end,

  signals_before_the_callback_ran_call_it_once = function()
    local n = 0
    signal.on("SIGUSR1", function() n = n + 1 end)
    send("USR1 USR1 USR1")
    T.eq(n, 1, "calls")
    signal.on("SIGUSR1", nil)
  end,

  every_pending_signal_runs = function()
    local got = {}
    local function cb(name) got[#got + 1] = name end
    signal.on("SIGUSR1", cb)
    signal.on("SIGUSR2", cb)
    send("USR2 USR1")
    table.sort(got)
    T.eq(table.concat(got, ","), "SIGUSR1,SIGUSR2", "calls")
    signal.on("SIGUSR1", nil)
    signal.on("SIGUSR2", nil)
  end,

  an_error_of_a_callback_is_raised_where_the_program_was = function()
    signal.on("SIGUSR1", function() error("stop", 0) end)
    local after = false
    local ok, err = pcall(function()
      send("USR1")
      after = true
    end)
    T.eq(ok, false, "pcall")
    T.eq(err, "stop", "error")
    T.eq(after, false, "code after the signal ran")
    signal.on("SIGUSR1", nil)
  end,

  other_pending_signals_survive_an_error = function()
    local got = {}
    signal.on("SIGUSR1", function() error("stop", 0) end)
    signal.on("SIGUSR2", function(name) got[#got + 1] = name end)
    local ok = pcall(send, "USR1 USR2")
    T.eq(ok, false, "pcall")
    T.eq(table.concat(got, ","), "SIGUSR2", "calls")
    signal.on("SIGUSR1", nil)
    signal.on("SIGUSR2", nil)
  end,

  replacing_and_removing = function()
    local got = {}
    signal.on("SIGWINCH", function() got[#got + 1] = "a" end)
    signal.on("SIGWINCH", function() got[#got + 1] = "b" end)
    send("WINCH")
    -- (removed: back to the default, which ignores SIGWINCH)
    signal.on("SIGWINCH", nil)
    send("WINCH")
    T.eq(table.concat(got, ","), "b", "calls")
  end,

  a_debug_hook_is_restored = function()
    local lines = 0
    signal.on("SIGUSR1", function() end)
    debug.sethook(function() lines = lines + 1 end, "l")
    send("USR1")
    local hook, mask = debug.gethook()
    debug.sethook()
    assert(hook ~= nil and mask == "l", "hook gone")
    assert(lines > 0, "hook not called")
    signal.on("SIGUSR1", nil)
  end,

  misuse_raises = function()
    T.raises(function() signal.on() end, "bad argument #1")
    T.raises(function() signal.on("SIGNOPE", print) end, "unknown signal 'SIGNOPE'")
    T.raises(function() signal.on("USR1", print) end, "unknown signal 'USR1'")
    T.raises(function() signal.on("SIGKILL", print) end, "SIGKILL cannot be handled")
    T.raises(function() signal.on("SIGSEGV", print) end, "SIGSEGV cannot be handled")
    T.raises(function() signal.on("SIGUSR1", 1) end, "bad argument #2")
  end,

  number_of_a_name = function()
    -- (the same on every Linux architecture)
    T.eq(signal.number("SIGHUP"), 1, "SIGHUP")
    T.eq(signal.number("SIGINT"), 2, "SIGINT")
    T.eq(signal.number("SIGKILL"), 9, "SIGKILL")
    T.eq(signal.number("SIGTERM"), 15, "SIGTERM")
    T.eq(signal.number("SIGRTMIN+1"), signal.number("SIGRTMIN+0") + 1, "SIGRTMIN+1")
    assert(signal.number("SIGRTMIN+0") > signal.number("SIGSYS"), "after the others")
  end,

  number_is_the_inverse_of_what_run_reports = function()
    local proc = require "lust.sys.proc"
    for _, name in ipairs { "SIGHUP", "SIGINT", "SIGKILL", "SIGUSR1", "SIGUSR2",
                            "SIGPIPE", "SIGALRM", "SIGTERM", "SIGRTMIN+0", "SIGRTMIN+3" } do
      -- (killed by the number: what the shell's status and run report)
      local n = signal.number(name)
      T.eq(proc.run { "sh", "-c", "kill -" .. n .. " $$" }, name, name)
      local _, out = proc.run { "sh", "-c", 'sh -c "kill -' .. n .. ' \\$\\$"; echo $?' }
      T.eq(out, (128 + n) .. "\n", name .. ": a shell's status")
    end
  end,

  number_misuse_raises = function()
    T.raises(function() signal.number() end, "bad argument #1 to 'number'")
    T.raises(function() signal.number({}) end, "bad argument #1 to 'number'")
    for _, name in ipairs { "SIGNOPE", "TERM", "sigterm", "SIGRTMIN", "SIGRTMIN+",
                            "SIGRTMIN+01", "SIGRTMIN+-1", "SIGRTMIN+1x", "SIGRTMIN+99999999999" } do
      T.raises(function() signal.number(name) end, "unknown signal '" .. name .. "'")
    end
    -- One past the last realtime signal.
    local last = 0
    while pcall(signal.number, "SIGRTMIN+" .. (last + 1)) do last = last + 1 end
    assert(last >= 8, "realtime signals: " .. last + 1)
    -- (realtime signals can't have callbacks)
    T.raises(function() signal.on("SIGRTMIN+1", print) end, "unknown signal 'SIGRTMIN+1'")
  end,
}
