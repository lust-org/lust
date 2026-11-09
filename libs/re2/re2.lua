-- lust.re2: Google's RE2 regular expressions (vendor/re2, statically
-- linked) through the LuaJIT FFI. RE2 matches in time linear in the size of
-- the input (no catastrophic backtracking), so it is safe for untrusted
-- patterns and subjects. It is not a Lua pattern language: the syntax is
-- RE2's (https://github.com/google/re2/wiki/Syntax), and it works on UTF-8.
--
--   local re2 = require "lust.re2"
--   local re = assert(re2.new("(\\w+)@(?P<host>\\w+)\\.com"))
--   re:test("mail bob@example.com")       --> true
--   re:find("mail bob@example.com")       --> 6, 20, "bob", "example"
--   re:match("mail bob@example.com")      --> "bob", "example"
--   re:captures("bob@example.com")        --> {[0]="bob@example.com", "bob", "example", host="example"}
--   for user, host in re:gmatch(s) do ... end
--   re:gsub("bob@example.com", "\\2 at \\1")   --> "example at bob", 1
--   re2.new("a.c", {caseless = true, dot_nl = true})
--   re2.new("(") --> nil, "missing ): ("
--   re2.quote("a.b")                      --> "a\\.b"
--
-- Positions are 1-based byte offsets like in string.find; `init` may be
-- negative. A group that did not take part in a match is `false`. A regexp
-- is garbage collected like any other value.
--
-- This chunk is compiled to bytecode and run by libs/ffi_shim.c, which
-- passes the path of the shared object as `...` (nil inside `lust`).
local ffi = require "ffi"

local C = (...) and ffi.load((...)) or ffi.C

ffi.cdef [[
typedef struct lust_re2 lust_re2;
lust_re2 *lust_re2_new(const char *pat, size_t len, unsigned flags,
                       double max_mem, char *err, size_t errsz);
void lust_re2_free(lust_re2 *r);
int lust_re2_ngroups(const lust_re2 *r);
size_t lust_re2_group_names(const lust_re2 *r, char *buf, size_t bufsz);
int lust_re2_program_size(const lust_re2 *r);
int lust_re2_match(const lust_re2 *r, const char *s, size_t len,
                   size_t startpos, int anchor, int nsub, double *out);
]]

local format, sub, byte = string.format, string.sub, string.byte
local concat = table.concat
local type, error, tostring, setmetatable = type, error, tostring, setmetatable

local re_new, re_free = C.lust_re2_new, C.lust_re2_free
local re_match = C.lust_re2_match

-- Option name -> bit, see shim.cc. Built at runtime from an array on
-- purpose: `luajit -b` dumps a table constructor with constant keys in the
-- (per process randomized) order of its hash nodes, which would make the
-- embedded bytecode, and with it the build, non-reproducible.
local FLAGS = {}
for i, name in ipairs {
  "literal",       -- the pattern is a plain string, not a regexp
  "caseless",      -- case insensitive
  "longest",       -- leftmost-longest instead of leftmost-first
  "posix",         -- restrict to POSIX egrep syntax (no \d, \b, ...)
  "never_nl",      -- never match "\n", even if the pattern says so
  "dot_nl",        -- "." matches "\n"
  "never_capture", -- no capturing groups, (...) behaves like (?:...)
  "perl_classes",  -- allow \d \s \w (only matters with posix)
  "word_boundary", -- allow \b \B (only matters with posix)
  "one_line",      -- ^ and $ only match at the ends of the text (posix)
  "latin1",        -- text and pattern are Latin-1, not UTF-8
} do
  FLAGS[name] = 2 ^ (i - 1)
end

local ANCHOR_NONE, ANCHOR_START, ANCHOR_BOTH = 0, 1, 2

local Regexp = {}
Regexp.__index = Regexp

local M = {}

-- Argument checking with the same style of messages as the C API libraries.
local function check_str(v, n, fname)
  local t = type(v)
  if t == "string" then return v end
  if t == "number" then return tostring(v) end
  error(format("bad argument #%d to '%s' (string expected, got %s)",
    n, fname, v == nil and "no value" or t), 3)
end

local function check_self(self, fname)
  if type(self) ~= "table" or getmetatable(self) ~= Regexp then
    error(format("bad argument #1 to '%s' (regexp expected, got %s)",
      fname, self == nil and "no value" or type(self)), 3)
  end
end

-- Optional 1-based start position as in string.find -> 0-based offset in
-- [0, len], or nil if the position is beyond the end (no match possible).
local function start_offset(init, len, fname)
  if init == nil then return 0 end
  if type(init) ~= "number" then
    error(format("bad argument #3 to '%s' (number expected, got %s)",
      fname, type(init)), 3)
  end
  init = init - init % 1 -- truncate
  if init < 0 then
    init = len + init + 1
    if init < 1 then init = 1 end
  elseif init == 0 then
    init = 1
  end
  if init > len + 1 then return nil end
  return init - 1
end

-- Match s starting at the 0-based offset `pos`. On a match the offsets of
-- the first `nsub` groups are in self.out (see shim.cc) and it returns true.
local function exec(self, s, pos, anchor, nsub)
  local rc = re_match(self.re, s, #s, pos, anchor, nsub, self.out)
  if rc < 0 then
    error("lust.re2: matching failed (out of memory?)", 3)
  end
  return rc == 1
end

-- Substring of group i of the last match (false if it did not take part).
-- Whole-match offsets are valid: s:sub(a + 1, b).
local function group(self, s, i)
  local a = self.out[2 * i]
  if a < 0 then return false end
  return sub(s, a + 1, self.out[2 * i + 1])
end

-- Names of the groups as an array (false for unnamed ones), computed on
-- first use.
local function group_names(self)
  local names = self.names
  if names then return names end
  names = {}
  if self.n > 0 then
    local need = tonumber(C.lust_re2_group_names(self.re, nil, 0))
    local buf = ffi.new("char[?]", need)
    C.lust_re2_group_names(self.re, buf, need)
    local p = 0
    for i = 1, self.n do
      local name = ffi.string(buf + p)
      names[i] = name ~= "" and name
      p = p + #name + 1
    end
  end
  self.names = names
  return names
end

-- The captures of the last match as multiple values; the whole match if
-- the regexp has no groups (like string.match).
local function captures(self, s, i, n)
  if n == 0 then return group(self, s, 0) end
  if i > n then return end
  return group(self, s, i), captures(self, s, i + 1, n)
end

-- Offset right behind the character starting at the 0-based offset `pos`
-- (len + 1 if pos is at or behind the end, which ends a global match
-- loop). UTF-8 aware unless the regexp is Latin-1.
local function next_char(self, s, pos, len)
  if pos >= len then return len + 1 end
  if self.latin1 then return pos + 1 end
  local b = byte(s, pos + 1)
  local n = b < 0xC0 and 1 or b < 0xE0 and 2 or b < 0xF0 and 3 or 4
  -- never run past the end for truncated sequences
  return pos + n > len and len or pos + n
end

--- Compile a regexp. Returns the regexp, or nil and the error message.
-- `opts` is an optional table of booleans (see FLAGS above), plus
-- `max_mem` (bytes RE2 may use for the compiled program, default 8 MiB).
function M.new(pattern, opts)
  pattern = check_str(pattern, 1, "new")
  local flags, max_mem = 0, 0
  if opts ~= nil then
    if type(opts) ~= "table" then
      error(format("bad argument #2 to 'new' (table expected, got %s)",
        type(opts)), 2)
    end
    for k, v in pairs(opts) do
      if k == "max_mem" then
        if type(v) ~= "number" then
          error("bad argument #2 to 'new' (max_mem must be a number)", 2)
        end
        max_mem = v
      elseif FLAGS[k] then
        if v then flags = flags + FLAGS[k] end
      else
        error(format("bad argument #2 to 'new' (unknown option '%s')",
          tostring(k)), 2)
      end
    end
  end

  local err = ffi.new("char[256]")
  local re = re_new(pattern, #pattern, flags, max_mem, err, 256)
  if re == nil then return nil, ffi.string(err) end
  re = ffi.gc(re, re_free)

  -- Fields are assigned one by one on purpose: a table constructor with
  -- named fields becomes a template whose layout depends on the (per
  -- process randomized) string hashes, which would make the embedded
  -- bytecode, and with it the build, non-reproducible
  -- (tests/test_bytecode.py).
  local n = C.lust_re2_ngroups(re)
  local self = setmetatable({}, Regexp)
  self.re = re
  self.n = n
  self.src = pattern
  self.latin1 = opts ~= nil and opts.latin1 and true or false
  -- offsets of the whole match and all groups, reused by every call
  self.out = ffi.new("double[?]", 2 * (n + 1))
  return self
end

--- Escape everything that has a special meaning in a regexp, so that the
-- result matches s literally.
function M.quote(s)
  s = check_str(s, 1, "quote")
  -- Like RE2::QuoteMeta: letters, digits, "_" and bytes >= 0x80 are kept.
  return (s:gsub("[^A-Za-z0-9_\128-\255]", function(c)
    return c == "\0" and "\\x00" or "\\" .. c
  end))
end

--- true if the regexp matches anywhere in s (at or after init).
function Regexp:test(s, init)
  check_self(self, "test")
  s = check_str(s, 2, "test")
  local pos = start_offset(init, #s, "test")
  return pos ~= nil and exec(self, s, pos, ANCHOR_NONE, 0)
end

--- Like string.find: start, end (inclusive), captures... or nil.
function Regexp:find(s, init)
  check_self(self, "find")
  s = check_str(s, 2, "find")
  local pos = start_offset(init, #s, "find")
  if pos == nil or not exec(self, s, pos, ANCHOR_NONE, self.n + 1) then
    return nil
  end
  -- Offsets are read before the captures are cut (no calls in between).
  local a, b = self.out[0] + 1, self.out[1]
  if self.n == 0 then return a, b end
  return a, b, captures(self, s, 1, self.n)
end

--- Like string.match: the captures, or the whole match if the regexp has
-- no groups; nil if there is no match.
function Regexp:match(s, init)
  check_self(self, "match")
  s = check_str(s, 2, "match")
  local pos = start_offset(init, #s, "match")
  if pos == nil or not exec(self, s, pos, ANCHOR_NONE, self.n + 1) then
    return nil
  end
  return captures(self, s, 1, self.n)
end

--- Like match, but the regexp has to match the whole of s.
function Regexp:fullmatch(s)
  check_self(self, "fullmatch")
  s = check_str(s, 2, "fullmatch")
  if not exec(self, s, 0, ANCHOR_BOTH, self.n + 1) then return nil end
  return captures(self, s, 1, self.n)
end

--- A table with all captures: [0] is the whole match, [1..n] the groups
-- and every named group is also stored under its name. nil if no match.
function Regexp:captures(s, init)
  check_self(self, "captures")
  s = check_str(s, 2, "captures")
  local pos = start_offset(init, #s, "captures")
  if pos == nil or not exec(self, s, pos, ANCHOR_NONE, self.n + 1) then
    return nil
  end
  local names, t = group_names(self), {}
  for i = 0, self.n do
    local v = group(self, s, i)
    t[i] = v
    if i > 0 and names[i] then t[names[i]] = v end
  end
  return t
end

--- Iterate over all non-overlapping matches, yielding what match returns.
function Regexp:gmatch(s)
  check_self(self, "gmatch")
  s = check_str(s, 2, "gmatch")
  local len, n = #s, self.n
  local pos, lastend = 0, -1
  return function()
    while pos <= len do
      if not exec(self, s, pos, ANCHOR_NONE, n + 1) then
        pos = len + 1
        return nil
      end
      local a, b = self.out[0], self.out[1]
      if a == b then
        -- An empty match right behind the previous match is not a match
        -- of its own (as in RE2's GlobalReplace). Step over a character.
        pos = next_char(self, s, a, len)
        if a ~= lastend then
          lastend = a
          return captures(self, s, 1, n)
        end
      else
        pos, lastend = b, b
        return captures(self, s, 1, n)
      end
    end
    return nil
  end
end

-- Turn a replacement string into a list of literal strings and group
-- numbers: "\\1-\\0" -> {1, "-", 0}. Checked against the group count.
local function parse_replacement(repl, n)
  local parts, i, lit = {}, 1, {}
  local function flush()
    if #lit > 0 then
      parts[#parts + 1] = concat(lit)
      lit = {}
    end
  end
  while i <= #repl do
    local c = sub(repl, i, i)
    if c == "\\" then
      local d = sub(repl, i + 1, i + 1)
      if d == "\\" then
        lit[#lit + 1] = "\\"
      elseif d:match("^%d$") then
        local g = tonumber(d)
        if g > n then
          error(format("bad replacement: group \\%d does not exist " ..
            "(the regexp has %d)", g, n), 3)
        end
        flush()
        parts[#parts + 1] = g
      else
        error("bad replacement: \\ must be followed by a digit or \\", 3)
      end
      i = i + 2
    else
      lit[#lit + 1] = c
      i = i + 1
    end
  end
  flush()
  return parts
end

--- Replace matches in s. `repl` is
--   - a string: "\\0" is the whole match, "\\1" .. "\\9" the groups and
--     "\\\\" a backslash,
--   - a function called with the captures (the whole match if there are
--     none), returning the replacement; false/nil keeps the match,
--   - a table indexed by the first capture (or the whole match); a missing
--     (false/nil) value keeps the match.
-- At most `max` matches are replaced (default: all). Returns the new string
-- and the number of replacements.
function Regexp:gsub(s, repl, max)
  check_self(self, "gsub")
  s = check_str(s, 2, "gsub")
  local n, len = self.n, #s
  local rtype = type(repl)
  local parts
  if rtype == "number" then repl, rtype = tostring(repl), "string" end
  if rtype == "string" then
    parts = parse_replacement(repl, n)
  elseif rtype ~= "function" and rtype ~= "table" then
    error(format("bad argument #3 to 'gsub' (string/function/table " ..
      "expected, got %s)", repl == nil and "no value" or rtype), 2)
  end
  if max ~= nil and type(max) ~= "number" then
    error(format("bad argument #4 to 'gsub' (number expected, got %s)",
      type(max)), 2)
  end

  local out, nout = {}, 0
  local pos, lastend, count = 0, -1, 0 -- 0-based offsets into s
  local out_ = self.out
  while pos <= len and (max == nil or count < max) do
    if not exec(self, s, pos, ANCHOR_NONE, n + 1) then break end
    local a, b = out_[0], out_[1]

    -- unmatched text before the match
    if a > pos then
      nout = nout + 1
      out[nout] = sub(s, pos + 1, a)
    end

    if a == b and a == lastend then
      -- empty match directly behind the previous one: not a match
      local nxt = next_char(self, s, a, len)
      if a < len then
        nout = nout + 1
        out[nout] = sub(s, a + 1, nxt)
      end
      pos = nxt
    else
      local whole = sub(s, a + 1, b)
      local piece
      if parts then
        local t = {}
        for k = 1, #parts do
          local p = parts[k]
          t[k] = type(p) == "number" and (group(self, s, p) or "") or p
        end
        piece = concat(t)
      else
        -- The callbacks may use this very regexp: take everything needed
        -- from `out` first.
        local first = n == 0 and whole or group(self, s, 1)
        if rtype == "function" then
          piece = repl(captures(self, s, 1, n))
        else
          piece = first and repl[first]
        end
        if not piece then
          piece = whole
        elseif type(piece) == "number" then
          piece = tostring(piece)
        elseif type(piece) ~= "string" then
          error(format("invalid replacement value (a %s)", type(piece)), 2)
        end
      end
      nout = nout + 1
      out[nout] = piece
      count = count + 1
      pos, lastend = b, b
      if a == b then
        -- empty match: copy a character so that the loop advances
        local nxt = next_char(self, s, a, len)
        if a < len then
          nout = nout + 1
          out[nout] = sub(s, a + 1, nxt)
        end
        pos = nxt
      end
    end
  end

  if pos <= len then
    nout = nout + 1
    out[nout] = sub(s, pos + 1)
  end
  return concat(out), count
end

--- Number of capturing groups (not counting the whole match).
function Regexp:groups()
  check_self(self, "groups")
  return self.n
end

--- Table name -> index of the named capturing groups.
function Regexp:groupindex()
  check_self(self, "groupindex")
  local names, t = group_names(self), {}
  for i = 1, self.n do
    if names[i] then t[names[i]] = i end
  end
  return t
end

--- The pattern the regexp was compiled from.
function Regexp:pattern()
  check_self(self, "pattern")
  return self.src
end

--- Size of the compiled program, a measure for the regexp's complexity.
function Regexp:program_size()
  check_self(self, "program_size")
  return C.lust_re2_program_size(self.re)
end

return M
