-- Compiling patterns (new), plus quote: the cost of building regexps at
-- run time, e.g. from user input.
local re = require "lust.pcre2"

local patterns = {
  "a+b", "^\\d{4}-\\d{2}-\\d{2}$", "(\\w+)@([\\w.]+)", "(?i)error|warn(ing)?",
  "[A-Za-z_][A-Za-z0-9_]*", "(?P<key>\\w+)=(?P<value>[^ ]*)", "\\s+",
  "^(?:GET|POST|PUT|DELETE) (/[^ ]*) HTTP/1\\.[01]$", "(\\d+)\\.(\\d+)\\.(\\d+)",
  "[\\x{100}-\\x{10FFFF}]+", "(a|b|c|d|e|f|g|h)*xyz", "^#[0-9a-fA-F]{6}$",
}

local n = 0
for i = 1, 1000 do
  local p = patterns[i % #patterns + 1]
  local r = assert(re.new(p, i % 2 == 0 and { caseless = true } or nil))
  n = n + r:groups() + #r:pattern()
  local q = assert(re.new(re.quote(p .. i)))
  n = n + (q:test(p .. i) and 1 or 0)
end
-- The result, so the work can neither be skipped nor silently go wrong.
assert(n == 21413, n)
