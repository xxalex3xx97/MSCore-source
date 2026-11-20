-- Plain Lua 5.4 tests for MSCore.items icon API (no game). Run: lua test_items_icon.lua  (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local pass, fail = 0, 0
local function check(name, cond)
    if cond then pass = pass + 1; print("PASS " .. name) else fail = fail + 1; print("FAIL " .. name) end
end

-- ===== fake UE4SS globals
local findCalls = 0
FName = function(s) return s end
FindAllOf = function() findCalls = findCalls + 1 return {} end
FindFirstOf = function() return nil end
StaticFindObject = function() return nil end
RegisterHook = function() return nil end

local MSCore = require("mscore")
local I = MSCore.items

-- missing key + rate limit
local t, why = I.icon("ITEM_Nope")
check("missing key nil", t == nil and why == "no item ITEM_Nope")
local calls1 = findCalls
I.icon("ITEM_Nope")
check("2nd lookup within 10 s: no FindAllOf", findCalls == calls1)
check("1 FindAllOf for first lookup", calls1 == 1)

-- set_icon bad image
local ok, w = I.set_icon(nil, "ITEM_Nope")
check("set_icon(nil) false", ok == false and w == "bad image")

-- caps + kill switch
check("caps ok", MSCore.caps().items_icon == "ok")
check("module_disable items true", MSCore.module_disable("items", "x") == true)
local t2, w2 = I.icon("ITEM_Nope")
check("off icon", t2 == nil and w2 == "off:x")
check("caps off", MSCore.caps().items_icon == "off:x")

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
