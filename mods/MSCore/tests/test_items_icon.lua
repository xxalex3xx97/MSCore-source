-- Plain Lua 5.4 tests for the item icon API (mscore_wbp standalone + MSCore.items aliases) (no game). Run: lua test_items_icon.lua  (from any cwd)
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
LoopAsync = function() end
ExecuteInGameThread = function() end
ExecuteWithDelay = function() end
NotifyOnNewObject = function() end

-- standalone: mscore_wbp alone (mod state), no mscore.lua loaded
local W = require("mscore_wbp")
-- fake clock (os.clock is CPU time): stall guard = first call in standalone holds 3 s
local realClock, now = os.clock, 100
os.clock = function() return now end
local t00, w00 = W.item_icon("ITEM_Nope")
check("stall hold: no FindAllOf, pending", t00 == nil and w00 == "pending" and findCalls == 0)
now = now + 4
local t0, w0 = W.item_icon("ITEM_Nope")
check("standalone missing key nil", t0 == nil and w0 == "no item ITEM_Nope" and findCalls == 1)
W.item_icon("ITEM_Nope")
check("standalone rate limit", findCalls == 1)
for _ = 1, 7 do now = now + 5; W.item_icon("ITEM_Nope") end   -- steady calls (gap < 8 s), 35 s pass
check("30 s refresh: one more FindAllOf", findCalls == 2)
now = now + 20
W.item_icon("ITEM_Nope")
check("gap > 8 s = new hold: no FindAllOf during hold", findCalls == 2)
os.clock = realClock
check("standalone set_icon(nil)", select(1, W.set_item_icon(nil, "x")) == false)
check("standalone caps ok", W.items_caps() == "ok")
W.items_disable("y")
check("standalone off", select(2, W.item_icon("x")) == "off:y" and W.items_caps() == "off:y")
package.loaded["mscore_wbp"] = nil
findCalls = 0

local MSCore = require("mscore")
local I = MSCore.items
do
    local W0 = MSCore.wbp; MSCore.wbp = nil
    local gi, gw = I.icon("x"); local gs, gsw = I.set_icon(nil, "x")
    check("nil guard", gi == nil and gw == "off:missing" and gs == false and gsw == "off:missing")
    MSCore.wbp = W0
end

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
