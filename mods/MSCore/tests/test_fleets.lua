-- Plain Lua 5.4 tests for mscore_fleets (standalone, no game). Run: lua test_fleets.lua  (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local pass, fail = 0, 0
local function check(name, cond)
    if cond then pass = pass + 1; print("PASS " .. name) else fail = fail + 1; print("FAIL " .. name) end
end
local function same(a, b)
    if type(a) ~= "table" or #a ~= #b then return false end
    for i = 1, #b do if a[i] ~= b[i] then return false end end
    return true
end

local now = 100
os.clock = function() return now end
local calls = 0
local TEXT = "T\t10\t3\tZeta\nT\t11\t3\tAlpha\nS\t100\t2\t2\t10,11\nS\t101\t1\t1\t11\n"
_G.MSCoreNative_fleets_read = function() calls = calls + 1 return TEXT, true end

local F = require("mscore_fleets")
local l = F.list()
check("list sorted", l and #l == 2 and l[1].name == "Alpha" and l[2].name == "Zeta")
check("Alpha ships", same(l[1].ships, { 100, 101 }))
check("of_ship(100)", same(F.of_ship(100), { 10, 11 }))
check("of_ship(999) empty", same(F.of_ship(999), {}))
check("cache within 1 s", calls == 1)
now = now + 2
F.list()
check("refetch after 2 s", calls == 2)

_G.MSCoreNative_fleets_read = function() return "fleets module off", false end
F.invalidate()
local r, why = F.list()
check("native off", r == nil and why == "off:fleets module off")

_G.MSCoreNative_fleets_read = nil
r, why = F.list()
check("native absent", r == nil and tostring(why):find("^off:native too old") ~= nil)

_G.MSCoreNative_fleets_read = function() error("boom") end
r, why = F.list()
check("throw -> nil off:error", r == nil and tostring(why):find("^off:error") ~= nil)
r, why = F.list()
check("later calls off:error", r == nil and tostring(why):find("^off:error") ~= nil and F.caps():find("^off:error") ~= nil)

local ok, e = F.create("x")
check("write absent", ok == false and e == "off:native too old")
_G.MSCoreNative_fleet_cmd = function() return false, "x" end
ok, e = F.rename(1, "y")
check("write fail passthrough", ok == false and e == "x")
check("write_caps unverified", F.write_caps() == "off:unverified")
_G.MSCoreNative_offsets = function() return "fleets=ok fleets_write=ok" end
check("write_caps ok", F.write_caps() == "ok")

-- fresh module state for cache-invalidation check
package.loaded["mscore_fleets"] = nil
F = require("mscore_fleets")
calls = 0
_G.MSCoreNative_fleets_read = function() calls = calls + 1 return TEXT, true end
_G.MSCoreNative_fleet_cmd = function() return true, "" end
F.list()
ok = F.assign(10, 101)
F.list()
check("success invalidates cache", ok == true and calls == 2)

print(("%d pass, %d fail"):format(pass, fail))
os.exit(fail == 0 and 0 or 1)
