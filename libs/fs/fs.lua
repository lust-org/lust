-- Lua part of lust.fs: walkdir. Compiled to bytecode and embedded, loaded
-- by the first fs.walkdir call (see fs.c), which checks the arguments and
-- calls the function returned here with plain values.
--
-- The chunk gets fs.list. list(path, { types = true }) returns the names
-- and the types of the entries (the kernel reports them along), so the
-- walk needs one open per directory and no stat per entry.
--
-- No table constructors with named fields here: their bytecode would not
-- be reproducible (tests/test_bytecode.py).
local list = ...
local error = error

-- (shared by all walks: list only reads it)
local WITH_TYPES = {}
WITH_TYPES.types = true

-- The state of a walk is one array (see walkdir below), indexed by these.
-- One iterator function for all walks, instead of closures per walk:
-- creating a closure cannot be compiled by the JIT, and the trace aborts
-- that caused made the JIT's traces (and the cost of a walk) differ from
-- run to run.
local DIR = 1       -- the directory being walked, as yielded
local PREFIX = 2    -- the same as "<dir>/", to join names to
local NAMES = 3     -- the names and types of its entries
local TYPES = 4
local I = 5         -- the index of the entry yielded last
local DEPTH = 6     -- its depth (1: root)
local PENDING = 7   -- the directory yielded last, to descend into next
local MAX_DEPTH = 8 -- the arguments of walkdir (prune, on_error: or false)
local PRUNE = 9
local ON_ERROR = 10
local STACK = 11    -- DIR..I of its parents, suspended, 5 slots per depth
local NSAVED = 5

-- The iterator: yields dir, name, type. Both strings exist already (dir
-- once per directory, name from list), so an entry costs no new string:
-- joining them into a path (concatenating, hashing and interning a longer
-- string) would be most of the cost of a step. Only directories to
-- descend into get a path, which listing them needs anyway.
--
-- The common path (the next entry of the current directory) has no loop
-- on purpose: the iterator is called from the caller's loop, and a loop
-- in here would keep the JIT from compiling that one ("inner loop in root
-- trace").
local function iter(st)
  local sub = st[PENDING]
  if sub then -- make the contents of sub the current level
    st[PENDING] = false
    local n, t, c = list(sub, WITH_TYPES)
    if n then
      local s, k = st[STACK], (st[DEPTH] - 1) * NSAVED
      s[k + 1], s[k + 2], s[k + 3], s[k + 4], s[k + 5] =
        st[DIR], st[PREFIX], st[NAMES], st[TYPES], st[I]
      st[DEPTH] = st[DEPTH] + 1
      st[DIR], st[PREFIX], st[NAMES], st[TYPES], st[I] = sub, sub .. "/", n, t, 0
    elseif st[ON_ERROR] then
      st[ON_ERROR](sub, t, c)
    elseif c ~= "ENOENT" then -- (removed while walking: just skip it)
      error(t, 0)
    end
  end
  local i = st[I] + 1
  local name = st[NAMES][i]
  if name == nil then -- the current directory is done
    local depth = st[DEPTH] - 1
    if depth == 0 then return nil end -- (I unchanged: stays done)
    local s, k = st[STACK], (depth - 1) * NSAVED
    st[DIR], st[PREFIX], st[NAMES], st[TYPES], st[I] =
      s[k + 1], s[k + 2], s[k + 3], s[k + 4], s[k + 5]
    s[k + 1], s[k + 2], s[k + 3], s[k + 4] = nil, nil, nil, nil -- (garbage now)
    st[DEPTH] = depth
    return iter(st) -- (a tail call, the next entry of the parent)
  end
  st[I] = i
  local t = st[TYPES][i]
  if t == "directory" and st[DEPTH] < st[MAX_DEPTH] then
    local path = st[PREFIX] .. name
    local prune = st[PRUNE]
    if not (prune and prune(path)) then st[PENDING] = path end
  end
  return st[DIR], name, t
end

-- walkdir(root, max_depth, prune, on_error) -> iter, state | nil, err, code
--
-- Depth first, a directory before its contents. The contents of a
-- directory are listed when the iterator is called after yielding it, so
-- an error raised for it comes after the directory itself. dir is root as
-- given for its entries.
return function(root, max_depth, prune, on_error)
  local names, types, code = list(root, WITH_TYPES)
  if not names then return nil, types, code end
  local prefix = root:sub(-1) == "/" and root or root .. "/"
  return iter, { root, prefix, names, types, 0, 1, false, max_depth,
                 prune or false, on_error or false, {} }
end
