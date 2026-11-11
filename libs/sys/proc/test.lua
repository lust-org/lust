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
    status, out, err = proc.run({ "true" }, { stdout = "inherit", stderr = "capture" })
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
    T.raises(function() proc.run({ "true" }, { stdout = "pipe" }) end,
      "stdout must be 'capture', 'inherit' or 'null'")
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
