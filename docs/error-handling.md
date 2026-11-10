# Error handling

## Rules

1. **Misuse raises.** Wrong argument types, unknown or invalid options,
   wrong `self`: bugs in the caller. Use `luaL_check*`/`luaL_argerror`.
   Option errors read `unknown option '<key>'`, `<key> must be a number`,
   `<key> must be an integer in [<min>, <max>]`.
2. **Bad input returns `nil, err`.** Malformed data, unencodable values,
   later missing files, refused connections: like `io.open`. The caller
   picks `local v, err = f(s)` or `assert(f(s))`, so there are no "safe"
   variants; `pcall` would also swallow rule 1 errors. Such a function
   never returns `nil` on success (`json.decode("null")` is `json.null`).
3. **Out of memory raises**, also for a library's own allocations.
4. **If `nil` is a normal result, failures raise.** `re:match` returns
   `nil` for no match, so a hit match limit raises; otherwise
   `if re:match(s)` would silently take it for "no match".

## Error value

- A lowercase string without trailing period or `lust.<name>:` prefix.
- Positions are 1-based like everywhere in Lua, at the end:
  `unexpected end of data (at position 7)`; `#s + 1` if the input ended
  early. Wrapped C libraries count from 0: add 1 when building the
  message. The position is where the library detected the error (PCRE2:
  often right behind the culprit); don't correct it.
- A stable code as third value (`nil, msg, "ECONNREFUSED"`) only when
  callers must branch on it.

## C side

- Never raise while holding memory the GC doesn't own; most Lua API
  calls can raise. Hand it to a `__gc` userdata first, or return a
  status up the recursion, clean up, then raise or return `nil, err`.
- On `nil, err`, reset the stack (`lua_settop`); no partial results.
- `ffi` C functions never raise: they return a status, the Lua part
  applies the rules above.
- A failed system call returns `nil, "<what>: <reason>", "<errno name>"`
  built by `lust_push_errno` (`include/lust_errno.h`).

## Tests

Test both paths: misuse raises (`T.raises`), bad input returns `nil` and
the message without raising (see `libs/json/test.lua`).
