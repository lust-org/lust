local T = require "t"
local fs = require "lust.fs"

-- A call that failed softly: nil, a message containing `needle`, `code`.
local function fails(code, needle, v, err, c)
  T.eq(v, nil, "result")
  assert(type(err) == "string" and err:find(needle, 1, true),
    "expected error containing " .. needle .. ", got " .. tostring(err))
  T.eq(c, code, "code")
end

-- Runs a shell command (paths from in_tmp/fs.tempdir need no quoting).
local function sh(cmd)
  local r = os.execute(cmd)
  assert(r == 0 or r == true, cmd) -- (true with LUAJIT_ENABLE_LUA52COMPAT)
end

-- Runs fn(dir) with a fresh temporary directory, removed afterwards (also
-- if a test left an unreadable directory behind).
local function in_tmp(fn)
  return function()
    local dir = assert(fs.tempdir())
    local ok, err = pcall(fn, dir)
    sh("chmod -R u+rwx '" .. dir .. "' && rm -rf '" .. dir .. "'")
    if not ok then error(err, 0) end
  end
end

local function touch(path, content)
  local f = assert(io.open(path, "w"))
  f:write(content or "")
  f:close()
end

local function sorted(t)
  table.sort(t)
  return table.concat(t, ",")
end

local function octal(s) return tonumber(s, 8) end

-- There is no fs.symlink (yet): ln -s, with paths from in_tmp (no quotes).
local function symlink(target, path)
  sh(("ln -s '%s' '%s'"):format(target, path))
end

-- The path of an entry walkdir yields (dir is root as given at the top,
-- which may end in "/").
local function join(dir, name)
  return dir:sub(-1) == "/" and dir .. name or dir .. "/" .. name
end

-- All of a walk as a sorted "path:type,..." with dir cut off the paths.
local function walk(dir, opts)
  local t, base = {}, dir:gsub("/$", "")
  for d, name, type in assert(fs.walkdir(dir, opts)) do
    t[#t + 1] = join(d, name):sub(#base + 2) .. ":" .. type
  end
  return sorted(t)
end

-- dir/{a/{b/{f}, g}, l -> a, e/, x}
local function make_tree(dir)
  assert(fs.mkdir(dir .. "/a/b", { parents = true }))
  assert(fs.mkdir(dir .. "/e"))
  touch(dir .. "/a/b/f")
  touch(dir .. "/a/g")
  touch(dir .. "/x")
  symlink("a", dir .. "/l")
end

T.run {
  list_types = in_tmp(function(dir)
    make_tree(dir)
    local names, types = assert(fs.list(dir, { types = true }))
    T.eq(#names, 4)
    T.eq(#types, 4)
    local t = {}
    for i, name in ipairs(names) do t[i] = name .. ":" .. types[i] end
    T.eq(sorted(t), "a:directory,e:directory,l:link,x:file")
    -- without the option: only names
    T.eq(select("#", fs.list(dir)), 1)
    T.eq(select("#", fs.list(dir, { types = false })), 1)
    names, types = fs.list("/dev", { types = true })
    for i, name in ipairs(names) do
      if name == "null" then T.eq(types[i], "char") end
    end
  end),

  readlink = in_tmp(function(dir)
    symlink("some/where", dir .. "/l")
    T.eq(fs.readlink(dir .. "/l"), "some/where")
    local long = ("x"):rep(255) .. ("/" .. ("y"):rep(200)):rep(15)
    symlink(long, dir .. "/long")
    T.eq(fs.readlink(dir .. "/long"), long)
    fails("ENOENT", dir .. "/nope: no such file or directory", fs.readlink(dir .. "/nope"))
    touch(dir .. "/f")
    fails("EINVAL", dir .. "/f: invalid argument", fs.readlink(dir .. "/f"))
  end),

  walkdir = in_tmp(function(dir)
    make_tree(dir)
    T.eq(walk(dir), "a/b/f:file,a/b:directory,a/g:file,a:directory,e:directory,l:link,x:file")
    T.eq(walk(dir .. "/"), walk(dir))
    T.eq(walk(dir .. "/e"), "")
    -- dir is root as given or the path of a directory yielded before it,
    -- which comes before its contents; name is a bare name
    local seen = {}
    for d, name in fs.walkdir(dir) do
      assert(d == dir or seen[d], name .. " in " .. d .. " before it")
      assert(not name:find("/"), name)
      seen[d .. "/" .. name] = true
    end
    for d in fs.walkdir(dir .. "/", { max_depth = 1 }) do T.eq(d, dir .. "/") end
  end),

  walkdir_options = in_tmp(function(dir)
    make_tree(dir)
    T.eq(walk(dir, { max_depth = 1 }), "a:directory,e:directory,l:link,x:file")
    T.eq(walk(dir, { max_depth = 2 }),
      "a/b:directory,a/g:file,a:directory,e:directory,l:link,x:file")
    local pruned = {}
    T.eq(walk(dir, { prune = function(p) pruned[#pruned + 1] = p; return p:find("/a$") end }),
      "a:directory,e:directory,l:link,x:file")
    T.eq(sorted(pruned), dir .. "/a," .. dir .. "/e")
  end),

  walkdir_failures = in_tmp(function(dir)
    fails("ENOENT", dir .. "/nope: no such file or directory", fs.walkdir(dir .. "/nope"))
    touch(dir .. "/f")
    fails("ENOTDIR", "not a directory", fs.walkdir(dir .. "/f"))

    -- a directory vanishing while walking is skipped
    assert(fs.mkdir(dir .. "/d/sub", { parents = true }))
    local got = {}
    for d, name in fs.walkdir(dir) do
      local path = d .. "/" .. name
      got[#got + 1] = path:sub(#dir + 2)
      if path == dir .. "/d/sub" then assert(os.remove(path)) end
    end
    T.eq(sorted(got), "d,d/sub,f")

    -- unreadable: raises, or calls on_error
    assert(fs.mkdir(dir .. "/d/ro", { mode = 0 }))
    if fs.list(dir .. "/d/ro") then return end -- (running as root)
    local ok, err = pcall(walk, dir)
    T.eq(ok, false)
    T.eq(err, dir .. "/d/ro: permission denied")
    local errors = {}
    T.eq(walk(dir, { on_error = function(p, e, c) errors[#errors + 1] = p .. "|" .. e .. "|" .. c end }),
      "d/ro:directory,d:directory,f:file")
    T.eq(errors[1], dir .. "/d/ro|" .. dir .. "/d/ro: permission denied|EACCES")
  end),

  list_names_without_dot_entries = in_tmp(function(dir)
    T.eq(sorted(assert(fs.list(dir))), "")
    touch(dir .. "/b")
    touch(dir .. "/.hidden")
    assert(fs.mkdir(dir .. "/a"))
    T.eq(sorted(assert(fs.list(dir))), ".hidden,a,b")
    T.eq(sorted(assert(fs.list(dir .. "/"))), ".hidden,a,b")
  end),

  list_many_entries = in_tmp(function(dir)
    for i = 1, 500 do touch(dir .. "/f" .. i) end
    T.eq(#assert(fs.list(dir)), 500)
  end),

  list_failures = in_tmp(function(dir)
    fails("ENOENT", dir .. "/nope: no such file or directory", fs.list(dir .. "/nope"))
    touch(dir .. "/f")
    fails("ENOTDIR", "not a directory", fs.list(dir .. "/f"))
    -- (the stream of a failed call does not leak into the next one)
    T.eq(#assert(fs.list(dir)), 1)
  end),

  stat_file = in_tmp(function(dir)
    local before = os.time()
    touch(dir .. "/f", "hello")
    local st = assert(fs.stat(dir .. "/f"))
    T.eq(st.type, "file")
    T.eq(st.size, 5)
    T.eq(st.nlink, 1)
    T.eq(type(st.ino), "number")
    T.eq(type(st.dev), "number")
    T.eq(type(st.uid), "number")
    T.eq(type(st.gid), "number")
    assert(st.mtime >= before - 1 and st.mtime <= os.time() + 1, st.mtime)
    assert(st.atime > 0 and st.ctime > 0)
    assert(st.mode >= 0 and st.mode <= octal "7777", st.mode)
  end),

  stat_types = function()
    T.eq(assert(fs.stat("/")).type, "directory")
    T.eq(assert(fs.stat("/dev/null")).type, "char")
    -- /proc/self is a link to a directory
    T.eq(assert(fs.stat("/proc/self")).type, "directory")
    T.eq(assert(fs.stat("/proc/self", { follow = false })).type, "link")
    T.eq(assert(fs.stat("/proc/self", { follow = true })).type, "directory")
  end,

  stat_into = in_tmp(function(dir)
    touch(dir .. "/f", "hello")
    local t = { mine = 1 }
    T.eq(fs.stat(dir .. "/f", nil, t), t)
    local fresh = assert(fs.stat(dir .. "/f"))
    for k, v in pairs(fresh) do T.eq(t[k], v, k) end
    T.eq(t.mine, 1) -- (other keys are kept)
    -- every field is set again by the next call
    T.eq(fs.stat(dir, {}, t), t)
    T.eq(t.type, "directory")
    T.eq(t.ino, assert(fs.stat(dir)).ino)
    T.eq(fs.stat("/proc/self", { follow = false }, t).type, "link")
    -- left alone on failure
    fails("ENOENT", "no such file or directory", fs.stat(dir .. "/nope", nil, t))
    T.eq(t.type, "link")
  end),

  stat_failures = in_tmp(function(dir)
    fails("ENOENT", "no such file or directory", fs.stat(dir .. "/nope"))
    touch(dir .. "/f")
    fails("ENOTDIR", dir .. "/f/x: not a directory", fs.stat(dir .. "/f/x"))
  end),

  mkdir_creates_a_directory = in_tmp(function(dir)
    T.eq(fs.mkdir(dir .. "/a"), true)
    T.eq(assert(fs.stat(dir .. "/a")).type, "directory")
    T.eq(fs.mkdir(dir .. "/m", { mode = octal "700" }), true)
    T.eq(assert(fs.stat(dir .. "/m")).mode, octal "700")
  end),

  mkdir_failures = in_tmp(function(dir)
    assert(fs.mkdir(dir .. "/a"))
    fails("EEXIST", dir .. "/a: file exists", fs.mkdir(dir .. "/a"))
    fails("ENOENT", "no such file or directory", fs.mkdir(dir .. "/x/y"))
    assert(fs.mkdir(dir .. "/ro", { mode = octal "500" }))
    if fs.mkdir(dir .. "/ro/x") then return end -- (running as root)
    fails("EACCES", dir .. "/ro/x: permission denied", fs.mkdir(dir .. "/ro/x"))
    fails("EACCES", dir .. "/ro/x: permission denied",
      fs.mkdir(dir .. "/ro/x/y", { parents = true }))
  end),

  mkdir_parents = in_tmp(function(dir)
    T.eq(fs.mkdir(dir .. "/a/b//c/", { parents = true, mode = octal "700" }), true)
    T.eq(assert(fs.stat(dir .. "/a/b/c")).type, "directory")
    T.eq(assert(fs.stat(dir .. "/a/b/c")).mode, octal "700")
    -- existing directories are fine
    T.eq(fs.mkdir(dir .. "/a/b/c", { parents = true }), true)
    T.eq(fs.mkdir(dir .. "/a", { parents = true }), true)
    T.eq(fs.mkdir("/", { parents = true }), true)
    T.eq(fs.mkdir(dir .. "/a/b/./d", { parents = true }), true)
    T.eq(assert(fs.stat(dir .. "/a/b/d")).type, "directory")
  end),

  mkdir_parents_failures = in_tmp(function(dir)
    touch(dir .. "/f")
    fails("EEXIST", dir .. "/f: file exists", fs.mkdir(dir .. "/f", { parents = true }))
    fails("ENOTDIR", dir .. "/f/x/y: not a directory",
      fs.mkdir(dir .. "/f/x/y", { parents = true }))
    fails("ENOENT", "no such file or directory", fs.mkdir("", { parents = true }))
  end),

  tempfile = in_tmp(function(dir)
    local p = assert(fs.tempfile { dir = dir })
    T.eq(p:sub(1, #dir + 6), dir .. "/lust-")
    local st = assert(fs.stat(p))
    T.eq(st.type, "file")
    T.eq(st.size, 0)
    T.eq(st.mode, octal "600")
    -- usable through io.open
    touch(p, "x")
    T.eq(assert(fs.stat(p)).size, 1)

    local q = assert(fs.tempfile { dir = dir .. "/", prefix = "pre.", suffix = ".txt" })
    assert(q:match("^" .. dir:gsub("%p", "%%%0") .. "/pre%.[%w]+%.txt$"), q)
    assert(q ~= p)
    -- empty prefix
    local r = assert(fs.tempfile { dir = dir, prefix = "" })
    T.eq(#r, #dir + 1 + 6)
    T.eq(#assert(fs.list(dir)), 3)
  end),

  tempfile_defaults_to_tmpdir = function()
    local p = assert(fs.tempfile())
    local tmp = os.getenv("TMPDIR")
    if tmp == nil or tmp == "" then tmp = "/tmp" end
    T.eq(p:sub(1, #tmp), tmp)
    assert(p:find("/lust-[^/]+$"), p)
    assert(os.remove(p))
  end,

  tempdir = in_tmp(function(dir)
    local p = assert(fs.tempdir { dir = dir, prefix = "d" })
    local st = assert(fs.stat(p))
    T.eq(st.type, "directory")
    T.eq(st.mode, octal "700")
    assert(p:match("/d[%w]+$"), p)
    assert(fs.tempdir { dir = dir } ~= assert(fs.tempdir { dir = dir }))
  end),

  temp_failures = in_tmp(function(dir)
    fails("ENOENT", dir .. "/nope/lust-XXXXXX: no such file or directory",
      fs.tempfile { dir = dir .. "/nope" })
    fails("ENOENT", "no such file or directory", fs.tempdir { dir = dir .. "/nope" })
  end),

  argument_errors = function()
    T.raises(function() fs.list() end, "bad argument #1 to 'list'")
    T.raises(function() fs.list({}) end, "bad argument #1")
    T.raises(function() fs.stat("a\0b") end, "path contains a zero byte")
    T.raises(function() fs.mkdir("a\0b") end, "path contains a zero byte")
    T.raises(function() fs.stat("/", 1) end, "bad argument #2")
    T.raises(function() fs.stat("/", { folow = false }) end, "unknown option 'folow'")
    T.raises(function() fs.stat("/", nil, 1) end, "bad argument #3")
    T.raises(function() fs.stat("/", nil, "t") end, "table expected")
    T.raises(function() fs.mkdir("x", { mode = "755" }) end, "mode must be a number")
    T.raises(function() fs.mkdir("x", { mode = 4096 }) end, "mode must be an integer in [0, 4095]")
    T.raises(function() fs.mkdir("x", { mode = 1.5 }) end, "mode must be an integer")
    T.raises(function() fs.mkdir("x", { [1] = true }) end, "unknown option '?'")
    T.raises(function() fs.tempfile("x") end, "bad argument #1")
    T.raises(function() fs.tempfile { dir = 1 } end, "dir must be a string")
    T.raises(function() fs.tempfile { prefix = "a/b" } end, "prefix must not contain '/'")
    T.raises(function() fs.tempfile { suffix = "/b" } end, "suffix must not contain '/'")
    T.raises(function() fs.tempfile { dir = "a\0" } end, "dir contains a zero byte")
    T.raises(function() fs.tempdir { suffix = ".d" } end, "unknown option 'suffix'")
    T.raises(function() fs.list("/", { type = true }) end, "unknown option 'type'")
    T.raises(function() fs.readlink() end, "bad argument #1 to 'readlink'")
    T.raises(function() fs.walkdir() end, "bad argument #1 to 'walkdir'")
    T.raises(function() fs.walkdir("/", { max_depth = 0 }) end,
      "max_depth must be an integer in [1, 2147483647]")
    T.raises(function() fs.walkdir("/", { prune = true }) end, "prune must be a function")
    T.raises(function() fs.walkdir("/", { on_error = "x" }) end, "on_error must be a function")
    T.raises(function() fs.walkdir("/", { follow = true }) end, "unknown option 'follow'")
  end,
}
