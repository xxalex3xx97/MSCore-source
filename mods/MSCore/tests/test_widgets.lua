-- Plain Lua 5.4 tests for mscore_wbp widget contract (resolve by name, caps, kill switch). No game. Run: lua test_widgets.lua (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local pass, fail = 0, 0
local function check(name, cond)
    if cond then
        pass = pass + 1
        print("PASS " .. name)
    else
        fail = fail + 1
        print("FAIL " .. name)
    end
end

-- ===== fake UE4SS globals: nothing resolves
FName = function(s) return s end
FindAllOf = function() return {} end
StaticFindObject = function() return nil end

local function fresh()
    package.loaded.mscore_wbp = nil
    return require("mscore_wbp")
end

-- (b) unknown logical name
do
    local M = fresh()
    local w, why = M.widget("nope")
    check("unknown name -> nil", w == nil)
    check("unknown name reason starts 'unknown'", type(why) == "string" and why:sub(1, 7) == "unknown")
end

-- (c) unresolved contract widget (run before d)
do
    local M = fresh()
    local w = M.widget("pause_menu")
    check("pause_menu unresolved -> nil", w == nil)
    check("caps lists pause_menu unresolved", M.caps() == "off:unresolved pause_menu")
end

-- (d) kill switch
do
    local M = fresh()
    M.disable("x")
    local w, why = M.widget("pause_menu")
    check("disabled widget -> nil", w == nil)
    check("disabled widget reason 'off:x'", why == "off:x")
    check("disabled caps 'off:x'", M.caps() == "off:x")
end

-- (e) contract table shape
do
    local C = require("mscore_contract")
    for _, n in ipairs({ "inventory_grid", "item_amount", "fleet_ship_entry", "pause_menu" }) do
        local e = C.widgets[n]
        check(n .. " present", e ~= nil)
        check(n .. " path starts /Game/", e ~= nil and e.path:sub(1, 6) == "/Game/")
        check(n .. " cls ends _C", e ~= nil and e.cls:sub(-2) == "_C")
    end
end

-- (f) selftest is false once killed
do
    local M = fresh()
    M.disable("x")
    local allok = M.selftest()
    check("selftest false after disable", allok == false)
end

print(string.format("RESULT pass=%d fail=%d", pass, fail))
os.exit(fail == 0 and 0 or 1)
