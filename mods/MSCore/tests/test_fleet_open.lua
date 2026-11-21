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

-- (a) assign enabled: ShowPopup(cls, ship, nil, false)
do
    local popup, ship, sub, shown, throwPop
    local function setupA()
        setup()
        popup = fake({ tag = "popup", GetFName = function() return { ToString = function() return "WBP_TagSelectPopup_C" end } end })
        ship = fake({ tag = "ship", ID = 7 })
        shown = {}
        throwPop = false
        sub = fake({ ShowPopup = function(self, c, o1, o2, b)
            if throwPop then error("boom") end
            shown[#shown + 1] = { c, o1, o2, b }
        end })
        FindFirstOf = function(n) if n == "WidgetSubsystem" then return sub end return nil end
        local sf = StaticFindObject
        StaticFindObject = function(p) if p:find("WBP_TagSelectPopup", 1, true) then return popup end return sf(p) end
        FindAllOf = function(n) if n == "WidgetBlueprintGeneratedClass" then return {} end return { ctl } end
    end
    local function holoShips(M) M._holo_core = nil end
    setupA()
    local M, C = fresh(), require("mscore_contract")
    check("fleet_assign recipe popup", C.fleet_assign and C.fleet_assign.how == "popup" and C.fleet_assign.subsystem_fn == "ShowPopup")
    M.install({ ships = { list = function() return { { id = 7, obj = ship } } end } })
    check("assign caps ok", M.fleet_assign_caps() == "ok")
    local r = M.open_fleet_assign(7)
    check("assign -> true", r == true)
    check("ShowPopup got (cls, ship, nil, false)", #shown == 1 and shown[1][1] == popup and shown[1][2] == ship and shown[1][3] == nil and shown[1][4] == false)
    local r2, w2 = M.open_fleet_assign(8)
    check("unknown ship -> false 'no ship'", r2 == false and w2 == "no ship")
    local r3, w3 = M.open_fleet_assign("x")
    check("bad id -> false", r3 == false and w3 == "bad ship id")
    throwPop = true
    local r4, w4 = M.open_fleet_assign(7)
    check("throwing ShowPopup -> false + reason", r4 == false and w4 == "popup failed")
    throwPop = false
    check("no kill switch after throw", M.open_fleet_assign(7) == true)

    -- no subsystem
    setupA(); M = fresh(); M.install({ ships = { list = function() return { { id = 7, obj = ship } } end } })
    FindFirstOf = function() return nil end
    local r5, w5 = M.open_fleet_assign(7)
    check("no subsystem -> false", r5 == false and w5 == "no widget subsystem")
    check("caps off no subsystem", M.fleet_assign_caps() == "off:no widget subsystem")

    -- no class
    setupA(); M = fresh(); M.install({ ships = { list = function() return { { id = 7, obj = ship } } end } })
    StaticFindObject = function() return nil end
    local r6, w6 = M.open_fleet_assign(7)
    check("no class -> false", r6 == false and w6 == "no class")
    check("caps off no class", M.fleet_assign_caps() == "off:no class")
    check("no candidates list", #M.fleet_popup_candidates() == 0)

    -- class found by candidate scan (TagSelect preferred over FleetTag)
    setupA(); M = fresh(); M.install({ ships = { list = function() return { { id = 7, obj = ship } } end } })
    StaticFindObject = function() return nil end
    local function named(n) return fake({ GetFName = function() return { ToString = function() return n end } end }) end
    local cf, ct = named("WBP_FleetTagRename_C"), named("WBP_TagSelectPopup_C")
    FindAllOf = function(n) if n == "WidgetBlueprintGeneratedClass" then return { cf, ct } end return {} end
    check("candidate scan finds 2", #M.fleet_popup_candidates() == 2)
    check("candidate class opens", M.open_fleet_assign(7) == true and shown[1][1] == ct)

    -- no ship (standalone, nothing found)
    setupA(); M = fresh()
    local r7, w7 = M.open_fleet_assign(7)
    check("no ship -> false", r7 == false and w7 == "no ship")
end

-- (a2) probe logs the widget's own name
do
    setup()
    local hooks = {}
    RegisterHook = function(path, pre) hooks[path] = pre; return true end
    local M = fresh()
    local lines = {}
    local op = print
    print = function(s) lines[#lines + 1] = tostring(s) end
    M.probe_fleet_popup()
    local wrap = function(n) return { get = function() return fake({ GetFName = function() return { ToString = function() return n end } end, GetFullName = function() return "full." .. n end, GetClass = function() return fake({ GetFName = function() return { ToString = function() return "BP_NewShipObject_C" end } end }) end }) end } end
    local null = { get = function() return fake({ IsValid = function() return false end }) end }
    hooks["/Script/MainSequence.WidgetSubsystem:ShowPopup"](nil, wrap("WBP_TagSelectPopup_C"), wrap("x"), null)
    print = op
    local all = table.concat(lines, "|")
    check("probe logs widget own name", all:find("widget=WBP_TagSelectPopup_C full=full.WBP_TagSelectPopup_C", 1, true) ~= nil)
    check("probe logs o2 nil", all:find("o2=nil", 1, true) ~= nil)
    check("probe hooked TagSelect once", select(2, all:gsub("PROBE hooked TagSelect SetupWidget", "")) == 1)
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
