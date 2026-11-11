local T = require "t"
local proc = require "lust.sys.proc"

-- A call that failed softly: nil, a message containing `needle`, `code`.
local function fails(code, needle, v, err, c)
  T.eq(v, nil, "result")
  assert(type(err) == "string" and err:find(needle, 1, true),
    "expected error containing " .. needle .. ", got " .. tostring(err))
  T.eq(c, code, "code")
end

-- Runs the shell command `script` (with arguments ...), returns run's results.
local function sh(script, opts, ...)
  return proc.run({ "sh", "-c", script, "sh", ... }, opts)
end

-- Runs the Lua `code` in a child interpreter (the one running us), with
-- proc required; returns run's results.
local interp = arg[-1]
local function child(code)
  local file = os.tmpname()
  local f = assert(io.open(file, "w"))
  f:write('local proc = require "lust.sys.proc"\n', code)
  f:close()
  local status, out, err = proc.run { interp, file }
  os.remove(file)
  return status, out, err
end

-- The targets of the descriptors a shell started by io.popen inherited
-- from us, one per line (the ones vanished meanwhile, as the glob's own
-- directory, are left out).
local function inherited()
  local p = assert(io.popen("readlink /proc/$$/fd/* 2>/dev/null"))
  local out = p:read("*a")
  p:close()
  return out
end

-- Whether s contains the plain string needle.
local function has(s, needle) return s:find(needle, 1, true) ~= nil end

-- Waits seconds (a poll for nothing).
local function sleep(seconds) proc.poll({}, seconds) end

-- Starts the shell command `script` with the options (stdout piped by
-- default), returns the child.
local function spawn(script, opts)
  opts = opts or {}
  if opts.stdout == nil then opts.stdout = "pipe" end
  return assert(proc.spawn({ "sh", "-c", script }, opts))
end

-- The state letter of process pid (/proc/<pid>/stat), nil if it's gone.
local function state(pid)
  local f = io.open("/proc/" .. pid .. "/stat")
  if not f then return nil end
  local stat = f:read("*a")
  f:close()
  return stat:match("%) (%a)") -- (after the command name, in parentheses)
end

-- Waits up to 5 s for state(pid) to become `want`.
local function await_state(pid, want)
  for _ = 1, 500 do
    if state(pid) == want then return end
    sleep(0.01)
  end
  error("process " .. pid .. " is still " .. tostring(state(pid)))
end

T.run {
  exit_status = function()
    T.eq(proc.run { "true" }, 0, "true")
    T.eq(proc.run { "false" }, 1, "false")
    T.eq(sh "exit 7", 7, "exit 7")
    T.eq(sh "exit 255", 255, "exit 255")
  end,

  killed_by_a_signal = function()
    T.eq(sh "kill -TERM $$", "SIGTERM", "status")
    T.eq(sh "kill -KILL $$", "SIGKILL", "status")
  end,

  captures_stdout_and_stderr_apart = function()
    local status, out, err = sh("echo out; echo err >&2; echo more")
    T.eq(status, 0, "status")
    T.eq(out, "out\nmore\n", "stdout")
    T.eq(err, "err\n", "stderr")
    status, out, err = proc.run { "true" }
    T.eq(out, "", "no stdout")
    T.eq(err, "", "no stderr")
  end,

  arguments_are_passed_as_they_are = function()
    local args = { "a b", "'quoted'", "$HOME", "*", "", "a\nb" }
    local _, out = sh('for a in "$@"; do printf "[%s]" "$a"; done', nil, unpack(args))
    T.eq(out, "[a b]['quoted'][$HOME][*][][a\nb]", "argv")
  end,

  large_output_on_both = function()
    -- (more than a pipe holds on each, written interleaved)
    local _, out, err = sh("i=0; while [ $i -lt 2000 ]; do echo " .. ("x"):rep(99) ..
      "; echo " .. ("y"):rep(99) .. " >&2; i=$((i+1)); done")
    T.eq(#out, 200000, "stdout")
    T.eq(#err, 200000, "stderr")
  end,

  input_is_fed_while_output_is_read = function()
    -- (cat writes back as it reads: writing all input first would deadlock)
    local input = ("0123456789abcdef"):rep(65536) -- 1 MiB
    local status, out = proc.run({ "cat" }, { input = input })
    T.eq(status, 0, "status")
    T.eq(#out, #input, "length")
    assert(out == input, "output differs")
  end,

  empty_input_is_end_of_file = function()
    local _, out = proc.run({ "wc", "-c" }, { input = "" })
    T.eq(out:match("%d+"), "0", "bytes")
  end,

  input_the_child_doesnt_read = function()
    -- (writing to it raises SIGPIPE, which must not kill us)
    local input = ("x"):rep(1048576)
    T.eq(proc.run({ "true" }, { input = input }), 0, "status")
    T.eq(sh("exec 0<&-; sleep 0.1", { input = input }), 0, "closed stdin")
  end,

  stdout_and_stderr_inherited_or_discarded = function()
    local status, out, err = sh("echo out; echo err >&2", { stdout = "null", stderr = "null" })
    T.eq(status, 0, "status")
    T.eq(out, nil, "stdout")
    T.eq(err, nil, "stderr")
    -- (inherit: ours, the test's output; only the results are checked)
    status, out, err = proc.run({ "true" }, { stdout = "inherit", stderr = "pipe" })
    T.eq(out, nil, "stdout")
    T.eq(err, "", "stderr")
  end,

  cwd = function()
    local _, out = proc.run({ "pwd" }, { cwd = "/" })
    T.eq(out, "/\n", "pwd")
    fails("ENOENT", "pwd (in /nonexistent): no such file or directory",
      proc.run({ "pwd" }, { cwd = "/nonexistent" }))
  end,

  env_is_a_modified_copy_of_ours = function()
    local _, out = sh('echo "$LUST_A|${HOME-unset}|${PATH:+path}"',
      { env = { LUST_A = "a b", HOME = false } })
    T.eq(out, "a b|unset|path\n", "env")
    _, out = sh('echo "${HOME:+home}"')
    T.eq(out, "home\n", "ours unchanged")
  end,

  only_stdio_is_inherited = function()
    -- (ls's own descriptors: what it inherited, and the directory it
    -- reads; the shell's ($$) would race with it setting up the pipe)
    local count = 'ls /proc/self/fd | wc -l'
    local _, before = sh(count)
    local files = {}
    for i = 1, 3 do files[i] = assert(io.open("/dev/null")) end
    local _, after = sh(count)
    for i = 1, 3 do files[i]:close() end
    T.eq(after, before, "descriptors")
  end,

  program_not_found = function()
    fails("ENOENT", "lust-no-such-program: no such file or directory",
      proc.run { "lust-no-such-program" })
    fails("ENOENT", "/nonexistent/x: no such file or directory",
      proc.run { "/nonexistent/x" })
    fails("EACCES", "/: permission denied", proc.run { "/" })
  end,

  misuse_raises = function()
    T.raises(function() proc.run() end, "bad argument #1")
    T.raises(function() proc.run "ls" end, "bad argument #1")
    T.raises(function() proc.run {} end, "argv is empty")
    T.raises(function() proc.run { "echo", 1 } end, "argv[2] must be a string")
    T.raises(function() proc.run { "echo", "a\0b" } end, "argv[2] must be a string without zero bytes")
    T.raises(function() proc.run({ "true" }, 1) end, "bad argument #2")
    T.raises(function() proc.run({ "true" }, { nope = 1 }) end, "unknown option 'nope'")
    T.raises(function() proc.run({ "true" }, { stdout = "capture" }) end,
      "stdout must be 'inherit', 'null', 'pipe' or a file")
    T.raises(function() proc.run({ "true" }, { stderr = io }) end,
      "stderr must be 'inherit', 'null', 'pipe' or a file")
    T.raises(function() proc.run({ "true" }, { input = "", stdin = "null" }) end,
      "input and stdin exclude each other")
    T.raises(function() proc.run({ "true" }, { input = 1 }) end, "input must be a string")
    T.raises(function() proc.run({ "true" }, { cwd = true }) end, "cwd must be a string")
    T.raises(function() proc.run({ "true" }, { env = "A=1" }) end, "env must be a table")
    T.raises(function() proc.run({ "true" }, { env = { ["A=B"] = "x" } }) end, "env names must be")
    T.raises(function() proc.run({ "true" }, { env = { A = 1 } }) end, "env.A must be a string")
    T.raises(function() proc.run({ "true" }, { env = { A = true } }) end, "env.A must be a string")
  end,

  -- A finalizer run while run copies our environment adds variables to it
  -- (setenv, through the FFI: the test needs no other library).
  env_grows_during_gc = function()
    local has_ffi, ffi = pcall(require, "ffi")
    if not has_ffi then return print("skip: no FFI (PUC Lua 5.1)") end
    ffi.cdef [[
      int setenv(const char *name, const char *value, int overwrite);
      int unsetenv(const char *name);
    ]]
    -- (a static `lust` can't look up C symbols: ./dev.py --musl-static)
    if not pcall(function() return ffi.C.setenv end) then
      return print("skip: ffi.C has no setenv")
    end
    local added = 0
    local opts = { env = { LUST_T = "x", HOME = false } }
    local n = T.during_gc(function()
      for _ = 1, 200 do
        local status, out = proc.run({ "sh", "-c", 'echo "$LUST_T${HOME:+home}"' }, opts)
        T.eq(status, 0)
        T.eq(out, "x\n")
      end
    end, function()
      for _ = 1, 20 do
        added = added + 1
        ffi.C.setenv("LUST_GC_" .. added, "v", 1)
      end
    end)
    for i = 1, added do ffi.C.unsetenv("LUST_GC_" .. i) end
    assert(n > 0, "no finalizer ran")
  end,

  spawn_reads_lines_as_they_come = function()
    -- (the second line only after the first one was read: no deadlock,
    -- no waiting for the end)
    local c = spawn("echo one; read x; echo two $x", { stdin = "pipe" })
    T.eq(c.stdout:read(), "one", "first line")
    assert(c.stdin:write("2", "\n"))
    T.eq(c.stdout:read("l"), "two 2", "second line")
    T.eq(c.stdout:read(), nil, "end")
    T.eq(c.stdout:read(), nil, "still the end")
    local status, out, err = c:wait()
    T.eq(status, 0, "status")
    T.eq(out, "", "rest")
    T.eq(err, nil, "stderr not piped")
    T.eq(c.stderr, nil, "no stderr stream")
  end,

  read_formats = function()
    local c = spawn("printf 'abcdef\nlast'")
    T.eq(c.stdout:read(2), "ab", "2 bytes")
    T.eq(c.stdout:read(), "cdef", "the rest of the line")
    T.eq(c.stdout:read("a"), "last", "all, without a newline at the end")
    T.eq(c.stdout:read("a"), "", "all at the end")
    T.eq(c.stdout:read(1), nil, "bytes at the end")
    T.eq(c:wait(), 0, "status")
    -- (n: what is there, without waiting for all of it)
    c = spawn("printf ab; read x; printf cd", { stdin = "pipe" })
    T.eq(c.stdout:read(100), "ab", "available")
    c.stdin:write("\n")
    T.eq(c.stdout:read("a"), "cd", "the rest")
    c:wait()
  end,

  long_lines = function()
    local long = ("x"):rep(100000)
    local c = assert(proc.spawn({ "sh", "-c", 'printf "%s\\n%s" "$1" "$1"', "sh", long },
      { stdout = "pipe" }))
    T.eq(c.stdout:read(), long, "a long line")
    T.eq(c.stdout:read(), long, "the last one, without a newline")
    T.eq(c.stdout:read(), nil, "end")
    c:wait()
  end,

  wait_returns_the_unread_rest = function()
    local c = spawn("echo 1; echo 2; echo 3; echo err >&2", { stderr = "pipe" })
    T.eq(c.stdout:read(), "1", "read")
    local status, out, err = c:wait()
    T.eq(status, 0, "status")
    T.eq(out, "2\n3\n", "stdout")
    T.eq(err, "err\n", "stderr")
    T.eq(c.stdout:read(), nil, "read after wait")
    -- (again: the same status, nothing more)
    status, out, err = c:wait()
    T.eq(status, 0, "status again")
    T.eq(out, "", "stdout again")
    T.eq(err, "", "stderr again")
  end,

  wait_closes_stdin = function()
    local c = spawn("cat", { stdin = "pipe" })
    c.stdin:write("abc")
    T.eq(select(2, c:wait()), "abc", "output")
    T.raises(function() c.stdin:write("x") end, "stream is closed")
  end,

  spawn_feeds_input_on_wait = function()
    local input = ("0123456789abcdef"):rep(65536) -- 1 MiB
    local c = assert(proc.spawn({ "cat" }, { input = input, stdout = "pipe" }))
    T.eq(c.input, nil, "no input field")
    local status, out = c:wait()
    T.eq(status, 0, "status")
    assert(out == input, "output differs")
  end,

  write_to_a_child_that_is_gone = function()
    local c = assert(proc.spawn({ "true" }, { stdin = "pipe" }))
    T.eq(c:wait(), 0, "status")
    c = assert(proc.spawn({ "sh", "-c", "exec 0<&-; echo closed; sleep 1" },
      { stdin = "pipe", stdout = "pipe" }))
    T.eq(c.stdout:read(), "closed", "stdin closed")
    -- (no SIGPIPE: that would kill us)
    fails("EPIPE", "write: broken pipe", c.stdin:write(("x"):rep(100000)))
    c:kill()
    c:wait()
  end,

  closing_streams = function()
    local c = spawn("cat; echo end", { stdin = "pipe" })
    c.stdin:write("data\n")
    c.stdin:close()
    c.stdin:close()
    T.eq(c.stdout:read("a"), "data\nend\n", "output after stdin closed")
    T.raises(function() c.stdin:write("x") end, "stream is closed")
    c.stdout:close()
    T.raises(function() c.stdout:read() end, "stream is closed")
    T.raises(function() c.stdout:lines() end, "stream is closed")
    T.raises(function() proc.poll { c.stdout } end, "list[1] is a closed stream")
    T.eq(c:wait(), 0, "status")
  end,

  streams_go_one_way = function()
    local c = spawn("cat", { stdin = "pipe" })
    T.raises(function() c.stdin:read() end, "not readable")
    T.raises(function() c.stdout:write("x") end, "not writable")
    T.raises(function() c.stdout:read("L") end, "format must be")
    T.raises(function() c.stdout:read(0) end, "integer >= 1")
    T.raises(function() c.stdout:read(1.5) end, "integer >= 1")
    T.raises(function() c.stdin:write({}) end, "string expected, got table")
    c:wait()
  end,

  lines_iterates_with_one_function = function()
    local seen, iters = {}, {}
    for _ = 1, 2 do
      local c = spawn("echo a; echo; echo b")
      local iter = c.stdout:lines()
      iters[#iters + 1] = iter
      for line in iter, c.stdout do seen[#seen + 1] = "[" .. line .. "]" end
      c:wait()
    end
    T.eq(table.concat(seen), "[a][][b][a][][b]", "lines")
    assert(iters[1] == iters[2], "a new iterator per call")
  end,

  files_as_stdin_stdout_stderr = function()
    local path = os.tmpname()
    local f = assert(io.open(path, "w"))
    f:write("first\n") -- (buffered: written before the child's)
    T.eq(proc.run({ "echo", "second" }, { stdout = f }), 0, "status")
    T.eq(sh("echo third >&2", { stderr = f }), 0, "stderr")
    f:write("fourth\n")
    f:close()
    T.raises(function() proc.run({ "true" }, { stdout = f }) end, "stdout is a closed file")
    f = assert(io.open(path, "a"))
    proc.run({ "echo", "fifth" }, { stdout = f })
    f:close()
    f = assert(io.open(path))
    local status, out = proc.run({ "cat" }, { stdin = f })
    f:close()
    os.remove(path)
    T.eq(status, 0, "status")
    T.eq(out, "first\nsecond\nthird\nfourth\nfifth\n", "the file")
  end,

  stdin_null_and_inherited = function()
    T.eq(select(2, proc.run({ "cat" }, { stdin = "null" })), "", "null")
    -- (stderr = io.stdout: both into one place, in order)
    local path = os.tmpname()
    local f = assert(io.open(path, "w"))
    proc.run({ "sh", "-c", "echo 1; echo 2 >&2; echo 3" }, { stdout = f, stderr = f })
    f:close()
    f = assert(io.open(path))
    T.eq(f:read("*a"), "1\n2\n3\n", "stdout and stderr into one file")
    f:close()
    os.remove(path)
  end,

  session = function()
    -- (fields 1 and 6 of the shell's /proc/$$/stat: its pid and session)
    local script = 'set -- $(cat /proc/$$/stat); echo $1 $6'
    local pid, sid = select(2, sh(script, { session = true })):match("(%d+) (%d+)")
    T.eq(sid, pid, "leads a session")
    pid, sid = select(2, sh(script)):match("(%d+) (%d+)")
    assert(sid ~= pid, "ours")
  end,

  kill = function()
    local c = assert(proc.spawn { "sleep", "10" })
    T.eq(c:kill(), true, "kill")
    T.eq(c:wait(), "SIGTERM", "status")
    fails("ESRCH", "kill: no such process", c:kill())
    c = assert(proc.spawn { "sleep", "10" })
    T.eq(c:kill("SIGKILL"), true, "kill")
    T.eq(c:wait(), "SIGKILL", "status")
    T.raises(function() c:kill("SIGNOPE") end, "unknown signal 'SIGNOPE'")
    T.raises(function() c:kill(9) end, "unknown signal '9'")
  end,

  kill_a_session_kills_its_group = function()
    local c = spawn("sleep 10 & echo $!; wait", { session = true })
    local pid = c.stdout:read()
    T.eq(c:kill(), true, "kill")
    T.eq(c:wait(), "SIGTERM", "status")
    -- (sleep, reparented, is reaped by init once killed)
    for _ = 1, 500 do
      local st = state(pid)
      if st == nil or st == "Z" then return end
      sleep(0.01)
    end
    error("the group's sleep still runs")
  end,

  poll_waits_for_the_first_ready = function()
    local slow = spawn("sleep 0.3; echo slow")
    local fast = spawn("sleep 0.05; echo fast")
    local ready = proc.poll { slow.stdout, fast.stdout }
    T.eq(#ready, 1, "one ready")
    assert(ready[1] == fast.stdout, "the fast one")
    T.eq(fast.stdout:read(), "fast", "its line")
    -- (ready after it ended, in the order of the list)
    ready = proc.poll { fast, slow.stdout, slow }
    assert(ready[1] == fast, "the ended child")
    T.eq(slow:wait(), 0, "slow")
    ready = proc.poll { fast.stdout, slow, fast }
    T.eq(#ready, 3, "all ready")
    assert(ready[1] == fast.stdout and ready[2] == slow and ready[3] == fast, "order")
    fast:wait()
  end,

  poll_timeout = function()
    local c = spawn("sleep 10")
    local ready = proc.poll({ c, c.stdout }, 0.05)
    T.eq(#ready, 0, "nothing ready")
    T.eq(#proc.poll({}, 0), 0, "nothing at all")
    c:kill()
    T.eq(#proc.poll({ c }, 5), 1, "the killed child")
    c:wait()
  end,

  poll_sees_buffered_data = function()
    -- (both lines come in one read: the second one waits in our buffer,
    -- not in the pipe)
    local c = spawn("echo 1; echo 2; sleep 10")
    T.eq(c.stdout:read(), "1", "first")
    T.eq(#proc.poll({ c.stdout }, 5), 1, "buffered")
    T.eq(c.stdout:read(), "2", "second")
    T.eq(#proc.poll({ c.stdout }, 0), 0, "nothing more")
    c:kill()
    c:wait()
  end,

  poll_misuse_raises = function()
    local c = spawn("true", { stdin = "pipe" })
    T.raises(function() proc.poll() end, "bad argument #1")
    T.raises(function() proc.poll { 1 } end, "list[1] is no stream or child")
    T.raises(function() proc.poll { io.stdout } end, "list[1] is no stream or child")
    T.raises(function() proc.poll { c.stdin } end, "list[1] is a child's stdin")
    T.raises(function() proc.poll({}, -1) end, "timeout must be a number >= 0")
    T.raises(function() proc.poll({}, 0 / 0) end, "timeout must be a number >= 0")
    c:wait()
  end,

  a_dropped_child_runs_on_and_is_reaped = function()
    local path = os.tmpname()
    local c = assert(proc.spawn({ "sh", "-c", 'echo $$; sleep 0.2; echo done > "$1"', "sh", path },
      { stdout = "pipe" }))
    local pid = c.stdout:read()
    c = nil
    collectgarbage()
    collectgarbage()
    -- (it finishes, unkilled, and waits to be reaped)
    await_state(pid, "Z")
    local f = assert(io.open(path))
    T.eq(f:read("*a"), "done\n", "its work")
    f:close()
    os.remove(path)
    -- (by the next start)
    proc.run { "true" }
    T.eq(state(pid), nil, "reaped")
  end,

  spawn_failures = function()
    fails("ENOENT", "lust-no-such-program: no such file or directory",
      proc.spawn { "lust-no-such-program" })
    fails("ENOENT", "true (in /nonexistent): no such file or directory",
      proc.spawn({ "true" }, { cwd = "/nonexistent" }))
    T.raises(function() proc.spawn {} end, "argv is empty")
    T.raises(function() proc.spawn({ "true" }, { stdout = "capture" }) end,
      "stdout must be 'inherit', 'null', 'pipe' or a file")
  end,

  -- A finalizer run while lines are read reads from the same stream: each
  -- line is still read once, whole (long ones too, copied out in two
  -- steps).
  reading_during_gc = function()
    for _, len in ipairs { 10, 10000 } do
      local c = spawn(('i=0; while [ $i -lt 300 ]; do printf "%%0%dd\\n" $i; i=$((i+1)); done')
        :format(len))
      local got = {}
      local function take(line)
        if line then got[#got + 1] = tonumber(line) end
        return line
      end
      T.during_gc(function()
        while take(c.stdout:read()) do end
      end, function()
        take(c.stdout:read())
      end)
      c:wait()
      table.sort(got)
      T.eq(#got, 300, "lines of " .. len)
      for i = 1, 300 do T.eq(got[i], i - 1, "line") end
    end
  end,

  exec_replaces_the_process = function()
    -- (the output before the exec waits in a buffer: stdout is a pipe)
    local status, out, err = child([[
      -- (the first field of /proc/self/stat: our pid)
      io.write("before ", assert(io.open("/proc/self/stat")):read("*n"), "|")
      proc.exec({ "sh", "-c", 'echo "after $$ $X"; exit 3' }, { env = { X = "x" } })
      print("not reached")
    ]])
    T.eq(err, "", "stderr")
    T.eq(status, 3, "the program's status")
    local a, b = out:match("^before (%d+)|after (%d+) x\n$")
    assert(a and a == b, "output: " .. out)
  end,

  exec_in_another_directory = function()
    local status, out = child([[
      proc.exec({ "pwd" }, { cwd = "/" })
    ]])
    T.eq(status, 0, "status")
    T.eq(out, "/\n", "output")
  end,

  exec_hands_on_only_stdio = function()
    -- (the file is ours, without close-on-exec: inherited by io.popen's shell)
    local path = os.tmpname()
    local status, out = child(([[
      local f = assert(io.open(%q))
      proc.exec { "sh", "-c", "readlink /proc/$$/fd/* 2>/dev/null; echo end" }
    ]]):format(path))
    os.remove(path)
    T.eq(status, 0, "status")
    assert(has(out, "end\n") and not has(out, path), "inherited: " .. out)
  end,

  failed_exec_leaves_the_process_as_it_was = function()
    local path = os.tmpname()
    local f = assert(io.open(path, "w"))
    f:write("data")
    f:flush()
    local _, cwd = proc.run { "pwd" }
    assert(has(inherited(), path), "the file is inherited before")

    fails("ENOENT", "lust-no-such-program: no such file or directory",
      proc.exec { "lust-no-such-program" })
    fails("ENOENT", "/nonexistent/x: no such file or directory",
      proc.exec { "/nonexistent/x" })
    fails("EACCES", "/: permission denied", proc.exec { "/" })
    -- (the exec fails in /, then we are back)
    fails("ENOENT", "lust-no-such-program (in /): no such file or directory",
      proc.exec({ "lust-no-such-program" }, { cwd = "/" }))
    fails("ENOENT", "lust-no-such-program (in /nonexistent): no such file or directory",
      proc.exec({ "lust-no-such-program" }, { cwd = "/nonexistent" }))

    T.eq(select(2, proc.run { "pwd" }), cwd, "working directory")
    assert(has(inherited(), path), "the file is inherited after")
    f:write("more")
    f:close()
    f = assert(io.open(path))
    T.eq(f:read("*a"), "datamore", "the file still works")
    f:close()
    os.remove(path)
  end,

  exec_misuse_raises = function()
    -- (a program that doesn't exist: nothing is replaced should a check fail)
    local nope = { "lust-no-such-program" }
    T.raises(function() proc.exec() end, "bad argument #1 to 'exec'")
    T.raises(function() proc.exec {} end, "argv is empty")
    T.raises(function() proc.exec { "lust-no-such-program", 1 } end, "argv[2] must be a string")
    T.raises(function() proc.exec(nope, 1) end, "bad argument #2")
    T.raises(function() proc.exec(nope, { input = "" }) end, "unknown option 'input'")
    T.raises(function() proc.exec(nope, { stdout = "null" }) end, "unknown option 'stdout'")
    T.raises(function() proc.exec(nope, { cwd = 1 }) end, "cwd must be a string")
    T.raises(function() proc.exec(nope, { env = { A = 1 } }) end, "env.A must be a string")
  end,
}
