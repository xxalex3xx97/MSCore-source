-- Plain Lua 5.4 tests for mscore_fleets (standalone, no game). Run: lua test_fleets.lua  (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local rprint = print
local pass, fail = 0, 0
local function check(name, cond)
    if cond then pass = pass + 1; rprint("PASS " .. name) else fail = fail + 1; rprint("FAIL " .. name) end
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

-- capture probe
local lines, hooks = {}, {}
local realPrint = print
local function fresh(withHook)
    package.loaded["mscore_fleets"] = nil
    lines, hooks = {}, {}
    print = function(s) lines[#lines + 1] = tostring(s) end
    _G.RegisterHook = withHook and function(path, cb) hooks[#hooks + 1] = { path, cb } end or nil
    return require("mscore_fleets")
end
local function has(s) for _, x in ipairs(lines) do if x:find(s, 1, true) then return true end end return false end
local function count(s) local n = 0 for _, x in ipairs(lines) do if x:find(s, 1, true) then n = n + 1 end end return n end
local function wrap(v) return { get = function() return v end } end
local function obj(name) return { GetFName = function() return { ToString = function() return name end } end } end
local function cls(name, props, super)
    local c = obj(name)
    c.IsValid = function() return true end
    c.ForEachProperty = function(_, cb)
        for _, p in ipairs(props) do
            local pr = obj(p[1]); pr.GetClass = function() return obj(p[2]) end
            cb(pr)
        end
    end
    c.GetSuperStruct = function() return super end
    return c
end
local Obj = cls("Object", {}, nil)
local TagInput = cls("TagInput", { { "Name", "StrProperty" } }, Obj)
local FTI = cls("FleetTagInput", { { "FleetId", "IntProperty" }, { "ShipId", "IntProperty" }, { "bAdd", "BoolProperty" } }, TagInput)
local function mkcmd(c) return { IsValid = function() return true end, GetClass = function() return c end, FleetId = 5, ShipId = 7, bAdd = false, Name = "x" } end

local C = fresh(false)
local okc, rc = pcall(C.capture, true)
check("capture no RegisterHook", okc and rc == false)

C = fresh(true)
check("capture arms", C.capture(true) == true and #hooks == 1)
check("capture path", hooks[1][1] == "/Script/MainSequence.DMSPlayerController:AddInputCommand")
C.capture(true)
check("capture armed once", #hooks == 1 and has("capture armed"))
local cb = hooks[1][2]
cb(nil, wrap(11), wrap(mkcmd(FTI)))
check("dump line", count("receiver=11") == 1 and has("FleetTagInput.FleetId:IntProperty=5") and has("FleetTagInput.ShipId:IntProperty=7")
    and has("TagInput.Name") and has("bAdd:BoolProperty=false"))
local SM = cls("ShipMove", {}, Obj)
cb(nil, wrap(1), wrap(mkcmd(SM))); cb(nil, wrap(1), wrap(mkcmd(SM)))
check("other class rate-limited", count("CAPTURE cmd class=ShipMove") == 1)
now = now + 31
cb(nil, wrap(1), wrap(mkcmd(SM)))
check("other class again after 31 s", count("CAPTURE cmd class=ShipMove") == 2)
local n0 = #lines
check("nil args no throw", pcall(cb, nil, nil, nil) and pcall(cb, nil, wrap(nil), wrap(nil)) and #lines == n0)
C.capture(false)
cb(nil, wrap(1), wrap(mkcmd(FTI)))
check("capture(false) mutes", #lines == n0)
C.capture(true)
cb(nil, wrap(1), wrap(mkcmd(FTI)))
check("capture(true) re-enables", #lines == n0 + 1 and #hooks == 1)
C = fresh(true); C.capture(true)
for _ = 1, 21 do hooks[1][2](nil, wrap(1), wrap(mkcmd(FTI))) end
check("dump cap 20", count("CAPTURE FleetTagInput") == 20)
print = realPrint
_G.RegisterHook = nil

print(("%d pass, %d fail"):format(pass, fail))
os.exit(fail == 0 and 0 or 1)
