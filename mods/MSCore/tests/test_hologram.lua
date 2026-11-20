-- Plain Lua 5.4 tests for mscore_wbp ship hologram API (no game). Run: lua test_hologram.lua  (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local pass, fail = 0, 0
local function check(name, cond)
    if cond then pass = pass + 1; print("PASS " .. name) else fail = fail + 1; print("FAIL " .. name) end
end

-- ===== fake UE4SS globals
FName = function(s) return s end
FindAllOf = function() return {} end
StaticFindObject = function() return nil end

local M = require("mscore_wbp")

-- (a) invalid image
local r = M.set_ship_hologram(nil, 1)
check("set_ship_hologram(nil) false", r == false)

-- (c) standalone (no install, core nil): fake FindAllOf with ships, missing ship / class / controller soft-fail, no throw
local shipCalls = 0
FindAllOf = function() shipCalls = shipCalls + 1 return { { IsValid = function() return true end, ID = 7 } } end
local okc, tex, why = pcall(M.hologram_texture, 1)
check("standalone missing ship no throw", okc and tex == nil and (why == "no host" or why == "no class" or why == "no ship" or why == "no controller" or why == "pending"))
pcall(M.hologram_texture, 1)
check("standalone ship scan rate limited (<= 1 FindAllOf)", shipCalls <= 1)
check("caps ok before disable", M.hologram_caps() == "ok n=0")

-- M10: host-parented widgets (fakes are real userdata with a per-object metatable, so ok() accepts them)
do
    local Cc = require("mscore_contract")
    local function mkud(methods) local u = io.tmpfile(); debug.setmetatable(u, { __index = methods }); return u end
    local order, created, removed, vis = {}, 0, 0, nil
    local texU = mkud({ IsValid = function() return true end })
    local entry = mkud({ IsValid = function() return true end, ShipHologram = { Brush = { ResourceObject = texU } },
        SetShip = function() order[#order + 1] = "SetShip" end, SetVisibility = function(_, v) vis = v end,
        RemoveFromParent = function() removed = removed + 1 end })
    local clsU = mkud({ IsValid = function() return true end })
    local pcU = mkud({ IsValid = function() return true end, IsLocalPlayerController = function() return true end })
    local shipU = mkud({ IsValid = function() return true end, ID = 7 })
    local lib = { Create = function() created = created + 1 return entry end }
    StaticFindObject = function(p) if p == "/Script/UMG.Default__WidgetBlueprintLibrary" then return lib end return clsU end
    FindAllOf = function(c) if c == Cc.ship_class then return { shipU } end if c == Cc.controller_class then return { pcU } end return {} end
    local hostLive = true
    local function mkhost(name) return mkud({ IsValid = function() return hostLive end, GetFullName = function() return name end,
        AddChild = function() order[#order + 1] = "AddChild" end }) end
    local host, host2 = mkhost("H1"), mkhost("H2")
    local now = 100
    local realClock = os.clock
    os.clock = function() return now end
    local lines, realPrint = {}, print
    print = function(x) lines[#lines + 1] = tostring(x) end

    local nt, nw = M.ship_hologram(7)
    check("no host -> nil,'no host', nothing created", nt == nil and nw == "no host" and created == 0)
    nt, nw = M.ship_hologram(7, { host = {} })
    check("invalid host -> 'no host'", nt == nil and nw == "no host" and created == 0)
    M.ship_hologram(7, { host = host })          -- first call = stall settle
    now = 104
    local t1 = M.ship_hologram(7, { host = host })
    check("created with host, texture returned", t1 == texU and created == 1)
    check("AddChild before SetShip", order[1] == "AddChild" and order[2] == "SetShip" and #order == 2)
    check("widget Collapsed", vis == 1)
    check("caps shows cache size", M.hologram_caps() == "ok n=1")
    Cc.holo_debug = true
    local before = #lines
    local t2 = M.ship_hologram(7, { host = host, force = true, refresh_ok = true })
    local joined = table.concat(lines, "|", before + 1)
    check("reuse returns same texture, no new widget, no SetShip", t2 == texU and created == 1 and #order == 2)
    check("reuse markers with debug on", joined:find("holo reuse id=7", 1, true) and joined:find("holo reuse ok id=7", 1, true))
    Cc.holo_debug = false
    before = #lines
    M.ship_hologram(7, { host = host })
    check("no reuse markers with debug off", #lines == before)
    M.hologram_release(host)
    check("release: RemoveFromParent for live host, cache empty", removed == 1 and M.hologram_caps() == "ok n=0")
    now = 105
    check("after release a NEW widget is created", M.ship_hologram(7, { host = host }) == texU and created == 2)
    hostLive = false
    M.hologram_release(host)
    check("release with dead host: no calls on widgets", removed == 1 and M.hologram_caps() == "ok n=0")
    hostLive = true
    now = 106
    M.ship_hologram(7, { host = host })
    local c3 = created
    now = 107
    M.ship_hologram(7, { host = host2 })
    check("host change releases and recreates", created == c3 + 1)
    print = realPrint
    os.clock = realClock
    StaticFindObject = function() return nil end
    FindAllOf = function() return {} end
    M.hologram_release(host2)
end

-- (b) kill switch
M.hologram_disable("x")
local t2, w2 = M.hologram_texture(1)
check("disabled texture", t2 == nil and w2 == "off:x")
check("disabled caps", M.hologram_caps() == "off:x")

-- pure functions
check("ttl constant", M.HOLO_TTL == 30)
check("age<ttl not due", M.holo_due(29.9, 30, false) == false)
check("age==ttl due", M.holo_due(30, 30, false) == true)
check("never rendered due", M.holo_due(nil, 30, false) == true)
check("force due", M.holo_due(1, 30, true) == true)
check("default ttl", M.holo_due(29, nil, false) == false and M.holo_due(30, nil, false) == true)
local ok1, q = M.holo_take({}, 1, 0, 2)
check("take empty queue", ok1 == true and #q == 0)
ok1, q = M.holo_take({}, 1, 2, 2)
check("budget used -> queued", ok1 == false and #q == 1 and q[1] == 1)
ok1, q = M.holo_take(q, 1, 2, 2)
check("dedupe", ok1 == false and #q == 1)
ok1, q = M.holo_take(q, 2, 2, 2); ok1, q = M.holo_take(q, 3, 2, 2)
check("FIFO order", #q == 3 and q[1] == 1 and q[2] == 2 and q[3] == 3)
ok1, q = M.holo_take(q, 3, 0, 2)
check("newer id waits behind older", ok1 == false and #q == 3)
ok1, q = M.holo_take(q, 2, 0, 2)
check("2nd in line served with budget 2", ok1 == true and #q == 2 and q[1] == 1 and q[2] == 3)
ok1, q = M.holo_take(q, 1, 1, 2)
check("only max per window", ok1 == true and #q == 1)
ok1 = M.holo_take(q, 3, 2, 2)
check("window full", ok1 == false)
local st = M.hologram_stats()
check("stats table", type(st.renders) == "number" and type(st.reuses) == "number" and type(st.created) == "number" and type(st.queued) == "number")

-- M5: names, stale texture, wrapper
check("alias identity", M.hologram_texture == M.ship_hologram and type(M.ship_hologram) == "function")
local valid = true
local ftex = { IsValid = function() return valid end }
local fent = { ShipHologram = { Brush = { ResourceObject = ftex } } }
check("holoTex valid", M._holoTex(fent) == ftex)
valid = false
check("stale texture not returned", M._holoTex(fent) == nil)
local applied = nil
local fimg = { IsValid = function() return true end, SetBrushFromTexture = function(_, t) applied = t end }
local realSH = M.ship_hologram
M.ship_hologram = function() return ftex end
check("wrapper applies", M.set_ship_hologram(fimg, 1) == true and applied == ftex)
M.ship_hologram = function() return nil, "pending" end
local rr, ww = M.set_ship_hologram(fimg, 1)
check("wrapper false,why", rr == false and ww == "pending")
M.ship_hologram = function() return ftex end
fimg.SetBrushFromTexture = function() error("x") end
check("wrapper pcall", M.set_ship_hologram(fimg, 1) == false)
M.ship_hologram = realSH

-- M7: purge
local qa = M.holo_purge({ 1, 2, 3 }, { [1] = true, [3] = true }, { [1] = 0, [3] = 0 }, 5, 10)
check("purge: dead id dropped", #qa == 2 and qa[1] == 1 and qa[2] == 3)
qa = M.holo_purge({ 1, 2 }, { [1] = true, [2] = true }, { [1] = 0, [2] = 9 }, 12, 10)
check("purge: old id dropped", #qa == 1 and qa[1] == 2)
local qb = M.holo_purge({ 5, 6 }, { [6] = true }, {}, 0, 10)
local okb, qb2 = M.holo_take(qb, 6, 1, 2)
check("purged head: full budget for live id", okb == true)

-- M8: decide
check("decide never rendered -> create", M.holo_decide(nil, 30, false, true, false) == "create")
check("decide expired+valid, no refresh_ok -> reuse", M.holo_decide(99, 30, false, false, true) == "reuse")
check("refresh constant is OFF", M.refresh_enabled == false)
check("decide never refreshes (age 1000, force, refresh_ok)", M.holo_decide(1000, 30, true, true, true) == "reuse")
M.refresh_enabled = true
check("decide expired+valid, refresh_ok, refresh enabled -> render", M.holo_decide(99, 30, false, true, true) == "render")
check("decide young+valid, refresh_ok -> reuse", M.holo_decide(5, 30, false, true, true) == "reuse")
check("decide force without refresh_ok ignored", M.holo_decide(5, 30, true, false, true) == "reuse")
check("decide force with refresh_ok, refresh enabled -> render", M.holo_decide(5, 30, true, true, true) == "render")
M.refresh_enabled = false
check("decide invalid texture -> create (drop widget, new one)", M.holo_decide(5, 30, false, false, false) == "create" and M.holo_decide(5, 30, true, true, false) == "create")
check("stats has stale_reuses", type(M.hologram_stats().stale_reuses) == "number")

-- M9: step markers
do
    local Cc = require("mscore_contract")
    local lines, realPrint = {}, print
    print = function(x) lines[#lines + 1] = tostring(x) end
    Cc.holo_debug = true; M._hstep("a id=1")
    Cc.holo_debug = false; M._hstep("b id=2")
    print = realPrint
    check("hstep prints when holo_debug", #lines == 1 and lines[1]:find("HOLO STEP a id=1", 1, true) ~= nil)
    check("hstep silent when holo_debug=false", #lines == 1)
end

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
