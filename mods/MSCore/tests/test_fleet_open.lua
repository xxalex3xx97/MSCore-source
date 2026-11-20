-- Plain Lua 5.4 tests for mscore_wbp fleet UI openers (no game). Run: lua test_fleet_open.lua (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local pass, fail = 0, 0
local function check(name, cond)
    if cond then pass = pass + 1; print("PASS " .. name) else fail = fail + 1; print("FAIL " .. name) end
end

local function fake(t) local u = io.tmpfile(); t.IsValid = t.IsValid or function() return true end; debug.setmetatable(u, { __index = t }); return u end

local cls, po, ctl, calls, throwShow
local function setup()
    calls = {}
    cls = fake({ tag = "cls" })
    po = fake({ tag = "po" })
    ctl = fake({ IsLocalPlayerController = function() return true end, PlayerObject = po,
        ShowWidget = function(self, c, o1, o2)
            if throwShow then error("boom") end
            calls[#calls + 1] = { c, o1, o2 }
        end })
    FName = function(s) return s end
    RegisterHook = function() end
    FindFirstOf = function() return nil end
    StaticFindObject = function(p) if p:find("WBP_Fleet.WBP_Fleet_C", 1, true) then return cls end return nil end
    FindAllOf = function() return { ctl } end
    throwShow = false
end
local function fresh() package.loaded.mscore_wbp = nil; return require("mscore_wbp") end

-- (a) assign is flag-gated
do
    setup()
    local M, C = fresh(), require("mscore_contract")
    check("fleet_assign recipe nil", C.fleet_assign == nil)
    local r, why = M.open_fleet_assign(1)
    check("assign off -> false", r == false)
    check("assign reason starts 'unverified'", type(why) == "string" and why:sub(1, 10) == "unverified")
    check("assign caps off", M.fleet_assign_caps() == "off:unverified")
end

-- (b) nothing resolvable
do
    setup()
    StaticFindObject = function() return nil end
    local M = fresh()
    local r, why = M.open_fleets_window()
    check("unresolved -> false", r == false)
    check("unresolved has reason", type(why) == "string" and why ~= "")
end

-- (c) happy path
do
    setup()
    local M = fresh()
    local r = M.open_fleets_window()
    check("open -> true", r == true)
    check("ShowWidget got (cls, po, nil)", #calls == 1 and calls[1][1] == cls and calls[1][2] == po and calls[1][3] == nil)
end

-- (d) throwing ShowWidget: false, no kill switch
do
    setup()
    throwShow = true
    local M = fresh()
    local r, why = M.open_fleets_window()
    check("throw -> false", r == false)
    check("throw reason", why == "ShowWidget failed")
    throwShow = false
    check("kill switch not tripped", M.open_fleets_window() == true)
end

-- (e) kill switch
do
    setup()
    local M = fresh()
    M.disable("x")
    local r, why = M.open_fleets_window()
    check("disabled -> nil", r == nil)
    check("disabled reason 'off:x'", why == "off:x")
end

print(string.format("RESULT pass=%d fail=%d", pass, fail))
os.exit(fail == 0 and 0 or 1)
