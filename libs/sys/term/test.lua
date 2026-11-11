local T = require "t"
local term = require "lust.sys.term"

-- Runs the Lua `code` in a child interpreter through the shell command
-- `wrap` (%s: the command), returns its output.
local interp = arg[-1]
local function child(code, wrap)
  local file = os.tmpname()
  local f = assert(io.open(file, "w"))
  f:write('local term = require "lust.sys.term"\n', code)
  f:close()
  local p = assert(io.popen(wrap:format(interp .. " " .. file)))
  local out = p:read("*a")
  p:close()
  os.remove(file)
  return out
end

-- Whether `script` (util-linux) can run a command in a pseudo terminal.
local function have_script()
  local r = os.execute("script -qec true /dev/null >/dev/null 2>&1")
  return r == 0 or r == true -- (true with LUAJIT_ENABLE_LUA52COMPAT)
end

T.run {
  not_a_terminal = function()
    -- (all three standard descriptors redirected: no terminal whoever runs this)
    local out = child("print(term.isatty(0), term.isatty(1), term.size(1))",
      "%s </dev/null | cat")
    -- (strerror(ENOTTY) is worded differently by each C library: glibc
    -- "inappropriate ioctl for device", musl "not a tty")
    assert(out:match("^false\tfalse\tnil\tfd 1: %l[^\t]+\tENOTTY\n$"), "output: " .. out)
  end,

  a_terminal = function()
    if not have_script() then return print("skip: no script(1)") end
    local out = child("print(term.isatty(0), term.isatty(1), term.size(1))",
      "script -qec 'stty cols 91 rows 37; %s' /dev/null")
    T.eq(out, "true\ttrue\t91\t37\r\n", "output") -- (the terminal: \r\n)
  end,

  bad_descriptor = function()
    T.eq(term.isatty(12345), false, "isatty")
    local v, err, code = term.size(12345)
    T.eq(v, nil, "size")
    T.eq(err, "fd 12345: bad file descriptor", "message")
    T.eq(code, "EBADF", "code")
  end,

  misuse_raises = function()
    T.raises(function() term.isatty() end, "bad argument #1")
    T.raises(function() term.isatty(-1) end, "must be a file descriptor")
    T.raises(function() term.size(1.5) end, "must be a file descriptor")
    T.raises(function() term.size({}) end, "bad argument #1")
  end,
}
