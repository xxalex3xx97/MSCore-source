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
check("standalone empty map = pending (not cached)", t0 == nil and w0 == "pending" and findCalls == 1)
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

-- item_list: fresh standalone module, fake items
do
    local function setmetatable_u(t) local u = io.tmpfile(); debug.setmetatable(u, { __index = t }); return u end  -- nm() needs userdata
    local function fake(n, label) return setmetatable_u { GetFName = function() return { ToString = function() return n end } end, IsValid = function() return true end, DisplayName = { ToString = function() return label end } } end
    local fl = 0
    FindAllOf = function() fl = fl + 1 return { fake("ITEM_B", "Beta"), fake("ITEM_A", "alpha"), fake("ITEM_X_Filter", "Zed") } end
    local W2 = require("mscore_wbp")
    os.clock = function() return now end
    now = now + 100
    local l0, w0b = W2.item_list()
    check("list hold: nil pending, 0 FindAllOf", l0 == nil and w0b == "pending" and fl == 0)
    now = now + 4
    local l1 = W2.item_list()
    check("list sorted, Filter excluded", l1 and #l1 == 2 and l1[1].key == "ITEM_A" and l1[1].name == "alpha" and l1[2].name == "Beta" and fl == 1)
    check("list cached", W2.item_list() == l1 and fl == 1)
    W2.items_disable("z")
    local l3, w3 = W2.item_list()
    check("list off", l3 == nil and w3 == "off:z")
    os.clock = realClock
    package.loaded["mscore_wbp"] = nil
    FindAllOf = function() findCalls = findCalls + 1 return {} end
end

-- empty map is never cached: nil/empty first, throttled 30 s, then items
do
    local function fake(n, label) local u = io.tmpfile(); debug.setmetatable(u, { __index = { GetFName = function() return { ToString = function() return n end } end, IsValid = function() return true end, DisplayName = { ToString = function() return label end } } }); return u end
    local fl, mode = 0, "nil"
    FindAllOf = function() fl = fl + 1 if mode == "nil" then return nil elseif mode == "items" then return { fake("ITEM_A", "alpha") } end return {} end
    local W3 = require("mscore_wbp")
    os.clock = function() return now end
    now = now + 100
    W3.item_list()   -- stall hold
    now = now + 4
    local l1, w1 = W3.item_list()
    check("empty nil: list pending, 1 FindAllOf", l1 == nil and w1 == "pending" and fl == 1)
    mode = "items"
    now = now + 5
    local l2, w2 = W3.item_list(); local t2, tw2 = W3.item_icon("ITEM_A")
    check("throttled: list+icon pending, no new FindAllOf", l2 == nil and w2 == "pending" and t2 == nil and tw2 == "pending" and fl == 1)
    for _ = 1, 6 do now = now + 5; W3.item_list() end   -- steady calls (gap < 8 s) so the stall guard stays quiet
    local l3 = W3.item_list()
    check("after throttle: list built", l3 and #l3 == 1 and l3[1].key == "ITEM_A" and fl == 2)
    package.loaded["mscore_wbp"] = nil
    local W4 = require("mscore_wbp")   -- icon path: empty table then items
    mode = "empty"; fl = 0
    now = now + 100; W4.item_icon("ITEM_A"); now = now + 4
    local ti, wi = W4.item_icon("ITEM_A")
    check("empty {}: icon pending", ti == nil and wi == "pending" and fl == 1)
    mode = "items"
    for _ = 1, 7 do now = now + 5; W4.item_icon("ITEM_A") end
    local _, wi2 = W4.item_icon("ITEM_A")
    check("icon after throttle: map built (no icon on fake)", wi2 == "no icon" and fl == 2)
    os.clock = realClock
    package.loaded["mscore_wbp"] = nil
    FindAllOf = function() findCalls = findCalls + 1 return {} end
end

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
check("empty map = pending", t == nil and why == "pending")
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
