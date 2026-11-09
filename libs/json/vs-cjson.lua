-- lust.json against lua-cjson on real documents: wall clock throughput
-- of decode and encode, best of several runs. Not part of `./dev.py test`
-- (no instruction counts: it needs lua-cjson and big data files).
--
--   LUA_CPATH="build/libs/?.so;;" luajit libs/json/vs-cjson.lua [dir-or-files...]
--
-- Defaults to the data of https://github.com/ibireme/yyjson_benchmark, in
-- ~/workspace/yyjson_benchmark/data/json. cjson is the `cjson` module of
-- the stock luajit (e.g. `lua51-cjson` on Arch). Both are used with their
-- defaults, so cjson formats numbers with 14 digits, yyjson with the
-- shortest string that reads back exactly, and cjson does not validate
-- UTF-8.
local yy = require "lust.json"
local cjson = require "cjson"

local MIN_TIME = tonumber(os.getenv("MIN_TIME")) or 1.0 -- s per measurement
local MIN_RUNS = 3
local MAX_RUNS = 200

local function read(path)
  local f = assert(io.open(path, "rb"))
  local s = f:read("*a")
  f:close()
  return s
end

-- Best time of fn() in seconds (CPU time), garbage collected beforehand.
local function best(fn)
  local best_t, total, runs = math.huge, 0, 0
  while runs < MIN_RUNS or (total < MIN_TIME and runs < MAX_RUNS) do
    collectgarbage()
    collectgarbage()
    local t0 = os.clock()
    fn()
    local dt = os.clock() - t0
    if dt < best_t then best_t = dt end
    total = total + dt
    runs = runs + 1
  end
  return best_t
end

-- Do both libraries decode to the same value? (numbers up to rounding)
local function same(a, b, depth)
  if type(a) ~= type(b) then return false end
  if type(a) == "number" then
    return a == b or math.abs(a - b) <= 1e-9 * math.max(math.abs(a), math.abs(b))
  end
  if type(a) ~= "table" then return a == b end
  if depth > 200 then return true end
  for k, v in pairs(a) do
    if not same(v, b[k], depth + 1) then return false end
  end
  for k in pairs(b) do
    if a[k] == nil then return false end
  end
  return true
end

local function list_files(args)
  local dir = os.getenv("HOME") .. "/workspace/yyjson_benchmark/data/json"
  if #args == 0 then args = { dir } end
  local files = {}
  for _, a in ipairs(args) do
    if a:match("%.json$") then
      files[#files + 1] = a
    else
      local p = assert(io.popen('ls -1 "' .. a .. '"/*.json'))
      for line in p:lines() do files[#files + 1] = line end
      p:close()
    end
  end
  table.sort(files, function(x, y) return x < y end)
  return files
end

local function mbs(bytes, t) return bytes / 1e6 / t end

print(("%-22s %8s | %28s | %28s"):format("", "size", "decode MB/s", "encode MB/s"))
print(("%-22s %8s | %8s %8s %9s | %8s %8s %9s"):format("file", "MB", "cjson", "json", "speedup", "cjson", "json", "speedup"))

local dec_ratios, enc_ratios = {}, {}
for _, path in ipairs(list_files(arg)) do
  local name = path:match("[^/]*$")
  local text = read(path)
  local size = #text

  local ok_c, vc = pcall(cjson.decode, text)
  local vy, err_y = yy.decode(text)
  local ok_y = vy ~= nil
  if not ok_y then vy = err_y end
  if not (ok_c and ok_y) then
    print(("%-22s %8.2f | decode failed: cjson %s, yyjson %s"):format(name, size / 1e6,
      ok_c and "ok" or tostring(vc):sub(1, 50), ok_y and "ok" or tostring(vy):sub(1, 50)))
  else
    -- (the decoded value of cjson needs a bit more room, drop what's not needed)
    local equal = same(vc, vy, 0)

    local dc = best(function() cjson.decode(text) end)
    local dy = best(function() yy.decode(text) end)

    -- Encode what was decoded; sizes differ (number formatting).
    local ec, ey, ok_e = math.huge, math.huge, true
    local ok1, outc = pcall(cjson.encode, vc)
    local outy, err_y2 = yy.encode(vy)
    local ok2 = outy ~= nil
    if not ok2 then outy = err_y2 end
    if ok1 and ok2 then
      -- the output must read back as the same value
      ok_e = same(yy.decode(outc), yy.decode(outy), 0)
      ec = best(function() cjson.encode(vc) end)
      ey = best(function() yy.encode(vy) end)
    else
      print(("  encode failed: cjson %s, yyjson %s"):format(ok1 and "ok" or tostring(outc):sub(1, 60), ok2 and "ok" or tostring(outy):sub(1, 60)))
    end

    local dr, er = dc / dy, ec / ey
    dec_ratios[#dec_ratios + 1] = dr
    if ec < math.huge then enc_ratios[#enc_ratios + 1] = er end
    print(("%-22s %8.2f | %8.0f %8.0f %8.2fx | %8.0f %8.0f %8.2fx%s"):format(
      name, size / 1e6, mbs(size, dc), mbs(size, dy), dr, mbs(#(outc or ""), ec), mbs(#(outy or ""), ey), er,
      (equal and ok_e) and "" or "  (!) results differ"))
    vc, vy, text, outc, outy = nil, nil, nil, nil, nil
    collectgarbage()
  end
end

local function geomean(t)
  local s = 0
  for _, x in ipairs(t) do s = s + math.log(x) end
  return math.exp(s / #t)
end
print(("geometric mean speedup: decode %.2fx, encode %.2fx"):format(geomean(dec_ratios), geomean(enc_ratios)))
