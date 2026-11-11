# GC reentrancy

## The fact

Every Lua API call that creates an object may run a step of the garbage
collector, and that step may run `__gc` finalizers (of `newproxy`
userdata, `ffi.gc` cdata): any Lua code, this very library included.
The same calls may raise (out of memory). So each of them is a point
where the C code around it can be left (an error) or reentered (a
finalizer).

LuaJIT drops errors raised in finalizers: reentrancy cannot be refused
with an error, a library has to work when called from a finalizer.
PUC Lua 5.1 propagates them instead, out of the API call that ran the GC
step: there they are one more way for that call to raise, which rule 1
below already covers (as it does out of memory).

## Which calls (LuaJIT, `lj_api.c`)

Run a GC step:

- creating strings: `lua_pushstring`, `lua_pushlstring`,
  `lua_pushfstring`, `luaL_Buffer` adds and `luaL_pushresult`,
  `lua_concat`, `lua_tolstring`/`luaL_checklstring` of a *number*
  (converted to a string)
- creating other objects: `lua_createtable`/`lua_newtable`,
  `lua_newuserdata`, `lua_pushcclosure`/`lua_pushcfunction`,
  `lua_newthread`
- raising: `luaL_error`, `luaL_argerror` (they format a message)

Run Lua code: `lua_call`/`lua_pcall`, and the non-raw table accessors
(`lua_gettable`, `lua_getfield`, `lua_settable`, `lua_setfield`) through
metamethods.

Don't: the raw accessors (`lua_rawget(i)`, `lua_rawset(i)`; a table that
grows allocates, but runs no GC step), `lua_next`, pushing what is no
object (`lua_pushvalue`, `lua_pushnil`, `lua_pushnumber`,
`lua_pushinteger`, `lua_pushboolean`, `lua_pushlightuserdata`), stack
manipulation, type queries, `lua_tolstring` of a string,
`lua_getmetatable`/`lua_setmetatable`.

## The rules

Across a call that runs a GC step or Lua code:

1. **Hold no memory the GC doesn't own** (it may raise): hand it to a
   `__gc` userdata first, or return a status, clean up, then raise (see
   `docs/error-handling.md`).
2. **Use no state shared with other calls without a guard**: per Lua
   state (an upvalue), per object (a userdata the call works on), per
   process (`environ`, signal handlers). A nested call may change or
   free it.
3. **Keep no pointer into memory Lua code can change**: `lua_tostring`
   of a string anchored only by a table the caller can reach (a finalizer
   may remove it from there, and the GC free it), the `environ` array
   (a finalizer may `setenv`). Anchor the value on the stack, or take the
   pointers after the last such call.

A pointer from `lua_tostring` of a value on the stack (an argument) is
fine: nothing but the C function changes its stack.

## Patterns

In this order of preference:

1. **Per-call ownership.** One userdata per call owns everything the
   call holds (`Child` of `libs/sys/proc`). Safe by construction; use it
   unless an allocation per call costs much compared to the work.
2. **Shared scratch, replaced when busy** (`include/lust_scratch.h`).
   For tiny, hot calls that reuse a buffer, a descriptor, a parser state
   (`fs.list`, `json.decode`/`encode`). A call finding the scratch busy
   (reentered, or left so by a call that raised) uses a new one, which
   replaces it; free on the fast path.
3. **Copy out before the next allocation.** Results in memory others may
   overwrite (the match data of a PCRE2 regexp, used by every match of
   it) or views into process state (`environ`) are copied into the
   call's own memory before anything runs a GC step.
4. **State an invariant where code relies on it.** Code that is safe
   only because nothing in it runs a GC step says so in a comment, so
   that adding a call that does is not a silent bug.

## Tests

A library with state shared between calls (rule 2) tests a call that
runs within a call of its own: `T.during_gc(fn, inner)` (`tests/t.lua`)
runs `fn` while a finalizer runs `inner` (and returns how often it ran),
both check their results. Use big inputs, so that the finalizer runs
inside the library's allocations (see `libs/sys/fs/test.lua`,
`list_during_gc`).

Don't arm finalizers by hand: one still armed at exit runs after stock
luajit has unloaded the library's `.so`, and crashes it.
