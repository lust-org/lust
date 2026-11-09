-- The doc-shortform.txt of a library lists exactly what its module exports.
-- usage: <interpreter> test_doc.lua <name>=<doc-shortform.txt> ...
--
-- Members of the module are documented by a block starting with
-- `name(args): result` or `name: type` at column 0. Other blocks (intro
-- text, methods of returned objects: `re:test(...)`) are not checked.
local T = require "t"

local function documented(path)
  local names = {}
  for line in assert(io.open(path)):lines() do
    local name = line:match("^([%a_][%w_]*)%(") or line:match("^([%a_][%w_]*): ")
    if name then names[name] = true end
  end
  return names
end

local tests = {}
for i = 1, #arg do
  local name, path = arg[i]:match("^([^=]+)=(.+)$")
  tests["doc_matches_exports_of_" .. name] = function()
    local mod = require("lust." .. name)
    local doc = documented(path)
    for k in pairs(mod) do
      assert(doc[k], name .. "." .. tostring(k) .. " is not documented in " .. path)
    end
    for k in pairs(doc) do
      assert(mod[k] ~= nil, name .. "." .. k .. " is documented but does not exist")
    end
  end
end
T.run(tests)
