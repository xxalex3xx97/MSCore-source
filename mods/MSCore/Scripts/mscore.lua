-- MSCore: shared helpers for Main Sequence UE4SS Lua mods. API v1. Read-only unless a command function is called.
-- Each UE4SS mod has its own Lua state: a consumer require()s its own copy of this file.
local MSCore = { api = 1, version = "0.5.1", verified_build = 149200896 }
-- C++ part (Mods/MSCore/dlls/main.dll) registers MSCoreNative_* globals before this file runs; nil if the DLL is not installed
MSCore.native = rawget(_G, "MSCoreNative_version") and {
    version = _G.MSCoreNative_version, alive = _G.MSCoreNative_alive, send_cmd = _G.MSCoreNative_send_cmd,
    view_send = _G.MSCoreNative_view_send, throttle = _G.MSCoreNative_throttle,
    offsets = _G.MSCoreNative_offsets, check_offsets = _G.MSCoreNative_check_offsets,
    new_order = _G.MSCoreNative_new_order, update_order = _G.MSCoreNative_update_order, delete_order = _G.MSCoreNative_delete_order,
    autopilot = _G.MSCoreNative_autopilot, skip_to = _G.MSCoreNative_skip_to, filter_send = _G.MSCoreNative_filter_send,
    warp_send = _G.MSCoreNative_warp_send, pinned = _G.MSCoreNative_pinned, pin_ensure = _G.MSCoreNative_pin_ensure,
    check = _G.MSCoreNative_check, loader_status = _G.MSCoreNative_loader_status } or nil

function MSCore.require(n)
    if type(n) ~= "number" or n > MSCore.api then
        error("MSCore api " .. tostring(n) .. " needed, installed api " .. MSCore.api, 2)
    end
    return MSCore
end

-- every game class/path name lives here; wrappers and consumers read it from here
MSCore.resolve = {
    player = { "DMSPlayerObject", "BP_PlayerObject_C" },
    controller = { "DMSPlayerController", "BP_PlayerController_C" },
    controllerPath = "/Script/MainSequence.DMSPlayerController",
    invWidget = "WBP_CharacterStatus_C",
    missionsWidget = "WBP_MissionsPanel_C",
    item = "Item",
    research = "ResearchTechnology",
    umg = "/Script/UMG.",
    clickHook = "/Script/UMG.Widget:ForceLayoutPrepass",
    ship = { "DMSPlayerShipObject" },
    console = "PilotConsoleObject",
    benchBlocks = { "BPO_MiningBenchBlock_C", "BPO_MinerModuleBlock_C" },
    shipProps = { id = "ID", dock = "Dock", dockId = "DockID", maxHull = "MaxHealth" },
    dock = "Dock",
    sector = "SolarSystemSector",
    hullFns = { "GetCurrentHull" },
}
local R = MSCore.resolve

function MSCore.logger(tag)
    local function log(...)
        local parts = {}
        for i = 1, select("#", ...) do parts[i] = tostring((select(i, ...))) end
        print(tag .. table.concat(parts, " ") .. "\n")
    end
    local function try(label, f, ...)
        local ok, result = pcall(f, ...)
        if ok then log(label .. " ok") else log(label .. " FAIL: " .. tostring(result)) end
        return ok, result
    end
    return log, try
end
local log, try = MSCore.logger("[MSCore] ")

---------------------------------------------------------------- safe
local safe = { try = try }
MSCore.safe = safe

function safe.val(v)
    if type(v) == "userdata" then
        for _, m in ipairs({ "get", "Get" }) do
            local ok, r = pcall(function() return v[m](v) end)
            if ok and r ~= nil then return r end
        end
    end
    return v
end

function safe.nameOf(x)
    local ok, s = pcall(function() return x:ToString() end)
    if ok and s then return s end
    return tostring(x)
end

-- native AVs on null UObject wrappers can't be caught by pcall: check IsValid before any method call
function safe.valid(o)
    if o == nil or type(o) ~= "userdata" then return false end
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v == true
end

function safe.objName(o)
    if not safe.valid(o) then return "?" end
    local ok, s = pcall(function() return safe.nameOf(o:GetFName()) end)
    if ok and s then return s end
    return "?"
end

-- first alive object among the class names (no logging; safe to call every tick)
function safe.live(...)
    for _, cls in ipairs({ ... }) do
        local ok, o = pcall(FindFirstOf, cls)
        if ok and o and o:IsValid() then return o end
    end
    return nil
end

-- game value for display; "?" when missing
function safe.rd(f)
    local ok, v = pcall(f)
    if ok and v ~= nil then return tostring(v) end
    return "?"
end

---------------------------------------------------------------- units (new, no consumer yet; from DevReference VERIFIED)
local units = { RAW_PER_M = 65536, CHUNK_M = 2048 }
MSCore.units = units
-- chunk {X,Y,Z} ints, loc {X,Y,Z} plain raw numbers (unwrap .Value / safe.val first) -> metres
function units.abs_pos(chunk, loc)
    return {
        X = chunk.X * units.CHUNK_M + loc.X / units.RAW_PER_M,
        Y = chunk.Y * units.CHUNK_M + loc.Y / units.RAW_PER_M,
        Z = chunk.Z * units.CHUNK_M + loc.Z / units.RAW_PER_M,
    }
end
function units.dist_m(a, b)
    local dx, dy, dz = a.X - b.X, a.Y - b.Y, a.Z - b.Z
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

---------------------------------------------------------------- ui
local ui = {}
MSCore.ui = ui

local function sf(path) local o = StaticFindObject(path) if o and o:IsValid() then return o end end
function ui.mk(cls, outer) return StaticConstructObject(sf(R.umg .. cls), outer, 0, 0, 0, nil, false, false, nil) end
function ui.setText(tb, s) tb:SetText(FText(s)) end

local BTN, hookIds = {}, nil
-- WHY: the click hook fires for EVERY widget in the game; callers that drop their UI call ui.clearClicks() so the button map does not grow and the hook is removed (re-registered on the next onClick).
function ui.clearClicks()
    BTN = {}
    if hookIds then pcall(UnregisterHook, R.clickHook, hookIds[1], hookIds[2]); hookIds = nil end
end
function ui.onClick(btn, fn)
    if not hookIds then
        local s, pre, post = pcall(RegisterHook, R.clickHook, function(self)
            local okn, key = pcall(function() return self:get():GetFullName() end)
            local f = okn and BTN[key]
            if f then pcall(ExecuteInGameThread, function() pcall(f) end) end
        end)
        if s then hookIds = { pre, post } end
    end
    BTN[btn:GetFullName()] = fn
    btn.OnClicked:Add(btn, FName("ForceLayoutPrepass"))
end

-- sRGB 0-255 -> UMG linear color (UMG colors are linear; sRGB values look washed out)
function ui.lin(c)
    c = c / 255
    if c <= 0.04045 then return c / 12.92 end
    return ((c + 0.055) / 1.055) ^ 2.4
end
function ui.rgba(r, g, b, a) return { R = ui.lin(r), G = ui.lin(g), B = ui.lin(b), A = a or 1 } end

local fontWarned = false
function ui.smallFont(tb, size)
    local ok, err = pcall(function()
        local f = tb.Font
        f.Size = size
        tb:SetFont(f)
    end)
    if not ok and not fontWarned then
        fontWarned = true
        log("SetFont FAIL: " .. tostring(err))
    end
end

function ui.hadd(hb, w)
    local s = hb:AddChildToHorizontalBox(w)
    pcall(function() s:SetPadding({ Left = 4, Top = 0, Right = 4, Bottom = 0 }) end)
end

---------------------------------------------------------------- events: one shared 250 ms poll loop per Lua state
local events = {}
MSCore.events = events
local ev = { fns = {}, n = 0, started = false }
function events.on(name, fn)
    assert(name == "tick", "MSCore.events: api 1 only has 'tick' (250 ms)")
    ev.n = ev.n + 1
    ev.fns[ev.n] = fn
    if not ev.started then
        ev.started = true
        LoopAsync(250, function()
            pcall(ExecuteInGameThread, function()
                for i = 1, ev.n do
                    local f = ev.fns[i]
                    if f then pcall(f) end
                end
            end)
            return false
        end)
    end
    return ev.n
end
function events.off(id) ev.fns[id] = nil end
function events.running() return ev.started end

---------------------------------------------------------------- ships (read-only; FindAllOf cached 5 s, every entry re-checked with safe.valid)
local ships = {}
MSCore.ships = ships
local shipCache = { t = -math.huge, list = {} }
local function shipObjs()
    if os.clock() - shipCache.t < 5 then return shipCache.list end
    local out = {}
    for _, cls in ipairs(R.ship) do
        local ok, l = pcall(FindAllOf, cls)
        if ok and l then for _, o in ipairs(l) do out[#out + 1] = o end end
    end
    shipCache = { t = os.clock(), list = out }
    return out
end
-- property read; nil when the owner is invalid or the read fails
local function prop(o, name)
    if not safe.valid(o) then return nil end
    local ok, v = pcall(function() return o[name] end)
    if ok then return v end
end
-- display name: DisplayName text first, GetDisplayName fallback (same order as MiningBenchMod obj_display)
local function nameOf(o)
    local ok, t = pcall(function() return o.DisplayName:ToString() end)
    if ok and t then return t end
    ok, t = pcall(function() return o:GetDisplayName():ToString() end)
    if ok and t then return t end
    return "?"
end
local function entryOf(o)
    if not safe.valid(o) then return nil end
    local id = prop(o, R.shipProps.id)
    if type(id) ~= "number" then return nil end
    return { id = id, name = nameOf(o), obj = o }
end
function ships.list()
    local out = {}
    for _, o in ipairs(shipObjs()) do
        local e = entryOf(o)
        if e then out[#out + 1] = e end
    end
    return out
end
function ships.get(id)
    for _, e in ipairs(ships.list()) do
        if e.id == id and safe.valid(e.obj) then return e end
    end
end
-- one bench/miner block (class name match) inside the ship's Subobjects or BuildingBlocks
local function isBench(e)
    if not safe.valid(e) then return false end
    local ok, cls = pcall(function() return e:GetClass() end)
    if not ok or not safe.valid(cls) then return false end
    local nm = safe.objName(cls)
    for _, b in ipairs(R.benchBlocks) do if nm == b then return true end end
    return false
end
local function hasBench(o)
    for _, arrName in ipairs({ "Subobjects", "BuildingBlocks" }) do
        local arr = prop(o, arrName)
        if arr then
            local found = false
            pcall(function()
                arr:ForEach(function(_, el)
                    if isBench(el:get()) then found = true return true end
                end)
            end)
            if found then return true end
        end
    end
    return false
end
-- ships that carry at least one mining bench or miner block
function ships.mine()
    local out = {}
    for _, e in ipairs(ships.list()) do
        if hasBench(e.obj) then out[#out + 1] = e end
    end
    return out
end
-- true/false, or nil + reason when the ship is unknown
function ships.is_docked(id)
    local e = ships.get(id)
    if not e then return nil, "no ship " .. tostring(id) end
    local dock, did = prop(e.obj, R.shipProps.dock), prop(e.obj, R.shipProps.dockId)
    return safe.valid(dock) and type(did) == "number" and did ~= 0
end
-- { current, max } from GetCurrentHull + MaxHealth; nil fields when unreadable
function ships.hull(id)
    local e = ships.get(id)
    if not e then return nil, "no ship " .. tostring(id) end
    local ok, cur = pcall(function() return e.obj[R.hullFns[1]](e.obj) end)
    return { current = ok and cur or nil, max = prop(e.obj, R.shipProps.maxHull) }
end
-- first PilotConsoleObject with ShipID == id, else ParentID == id (same order as MiningBenchMod pilot_console_of); object or nil
function ships.pilot_console(id)
    local ok, all = pcall(FindAllOf, R.console)
    if not ok or not all then return nil end
    for _, key in ipairs({ "ShipID", "ParentID" }) do
        for _, o in ipairs(all) do
            if safe.valid(o) and prop(o, key) == id then return o end
        end
    end
    return nil
end
-- GetAvailableJumpFuel: WARP fuel items in the WarpDrive inventory (1 item = 50 km). Hydrogen/deuterium not wrapped.
function ships.warp_fuel(id)
    local e = ships.get(id)
    if not e then return nil, "no ship " .. tostring(id) end
    local ok, v = pcall(function() return e.obj:GetAvailableJumpFuel() end)
    if ok then return v end
    return nil, "GetAvailableJumpFuel unreadable"
end

---------------------------------------------------------------- cmd + steer (native part; every call returns ok, err)
local NO_NATIVE = "native DLL not loaded"
local cmd = {}
MSCore.cmd = cmd
-- receiver: ship id; cmdObj: command UObject (NewObject or CreateInput*)
function cmd.send(receiver, cmdObj)
    local N = MSCore.native
    if not N then return false, NO_NATIVE end
    return N.send_cmd(receiver, cmdObj)
end

local steer = {}
MSCore.steer = steer
-- aim: sets the view offset (yaw, pitch in degrees) through the ship's pilot console
function steer.aim(id, yaw, pitch)
    local N = MSCore.native
    if not N then return false, NO_NATIVE end
    local c = ships.pilot_console(id)
    if not c then return false, "no pilot console for ship " .. tostring(id) end
    local cid = prop(c, "ID")
    if type(cid) ~= "number" then return false, "console ID unreadable" end
    return N.view_send(cid, yaw, pitch)
end
-- stops rotation only (aim 0,0). The throttle step is not zeroed.
function steer.stop(id) return steer.aim(id, 0, 0) end
function steer.throttle(id, delta)
    local N = MSCore.native
    if not N then return false, NO_NATIVE end
    return N.throttle(id, delta)
end

---------------------------------------------------------------- orders, filters, warp, docks, items (commands need the native DLL)
-- one native call by name; no DLL -> false, reason. Returns whatever the native returns.
local function call(name, ...)
    local N = MSCore.native
    if not N then return false, NO_NATIVE end
    local f = N[name]
    if not f then return false, "native " .. name .. " missing" end
    return f(...)
end

-- every command returns ok, err (skip_to also returns before, after = CurrentOrderIndex)
local orders = {}
MSCore.orders = orders
function orders.new(receiver, cls) return call("new_order", receiver, cls) end
function orders.update(oid, target, item, cmp, amount) return call("update_order", oid, target, item, cmp, amount) end
function orders.delete(sid, oid) return call("delete_order", sid, oid) end
function orders.autopilot(sid, on) return call("autopilot", sid, on and 1 or 0) end
function orders.skip_to(sid, idx) return call("skip_to", sid, idx) end

local filters = {}
MSCore.filters = filters
-- item: UObject or nil (empty row); remove: true removes the row at idx
function filters.send(sid, idx, item, amount, action, remove) return call("filter_send", sid, idx, item, amount, action, remove and 1 or 0) end

-- one number from a plain int or a PNum-style struct (Value); nil when unreadable
local function num(v)
    if type(v) == "number" then return v end
    local ok, r = pcall(function() return v.Value end)
    if ok and type(r) == "number" then return r end
end
-- {X,Y,Z} of a struct as numbers (0 when unreadable)
local function vec3(v)
    local out = {}
    for i, k in ipairs({ "X", "Y", "Z" }) do
        local ok, f = pcall(function() return v[k] end)
        out[i] = (ok and num(f)) or 0
    end
    return out
end
-- ore item objects of GenParams.ResourceRichness[].Item
local function oresOf(o)
    local out = {}
    local gp = prop(o, "GenParams")
    local rr = safe.valid(gp) and prop(gp, "ResourceRichness")
    if rr then
        pcall(function()
            rr:ForEach(function(_, el)
                local it = el:get().Item
                if safe.valid(it) then out[#out + 1] = it end
            end)
        end)
    end
    return out
end

local warp = {}
MSCore.warp = warp
-- ch, arr: {X=,Y=,Z=} raw numbers. wrap / set_target: booleans (piloting wrapper, bSetTarget)
function warp.send(sid, sys, ch, arr, wrap, set_target)
    return call("warp_send", sid, sys, ch.X, ch.Y, ch.Z, arr.X, arr.Y, arr.Z, wrap and 1 or 0, set_target and 1 or 0)
end
-- last map-pinned sector id (0 = none)
function warp.pinned_sector()
    local N = MSCore.native
    if not N then return 0 end
    return N.pinned()
end

-- sector list cache (FindAllOf every 5 s); plain tables, the ore objects are re-checked by the caller
local secCache = { t = -math.huge, list = {} }
-- every SolarSystemSector: {id, sys, ch={X,Y,Z}, he={X,Y,Z}, name, warp_dist, ores={item objects}}
function warp.sectors()
    if os.clock() - secCache.t < 5 then return secCache.list end
    local out = {}
    local ok, all = pcall(FindAllOf, R.sector)
    for _, o in ipairs(ok and all or {}) do
        if safe.valid(o) then
            local id, ch = prop(o, "ID"), prop(o, "ChunkLocation")
            if type(id) == "number" and ch then
                local gp = prop(o, "GenParams")
                local wd = safe.valid(gp) and num(prop(gp, "WarpArrivalDistance")) or nil
                local okn, nm = pcall(function() return o:GetDisplayName():ToString() end)
                out[#out + 1] = { id = id, sys = prop(o, "ParentID"), ch = vec3(ch), he = vec3(prop(o, "HalfExtents") or {}),
                    name = (okn and nm) or ("Sector " .. id), warp_dist = wd, ores = oresOf(o) }
            end
        end
    end
    secCache = { t = os.clock(), list = out }
    return out
end

-- docks in the world: {id, name, obj}. FindAllOf each call (user action only).
function MSCore.docks()
    local out = {}
    local ok, all = pcall(FindAllOf, R.dock)
    for _, o in ipairs(ok and all or {}) do
        local e = entryOf(o)
        if e then out[#out + 1] = e end
    end
    return out
end

-- Item by object name, display name or numeric ID (case-insensitive). Returns obj, or nil + reason on zero or several matches. Item list cached 5 s.
local itemCache = { t = -math.huge, list = {} }
function MSCore.find_item(res)
    if os.clock() - itemCache.t >= 5 then
        local ok, all = pcall(FindAllOf, R.item)
        itemCache = { t = os.clock(), list = ok and all or {} }
    end
    local want, numId = string.lower(tostring(res)), tonumber(res)
    local hit, n = nil, 0
    for _, it in ipairs(itemCache.list) do
        if safe.valid(it) then
            local m = string.lower(safe.objName(it)) == want or string.lower(nameOf(it)) == want
            if not m and numId then m = prop(it, "ID") == numId end
            if m then n, hit = n + 1, it end
        end
    end
    if n == 1 then return hit end
    if n == 0 then return nil, "no Item matches '" .. tostring(res) .. "'" end
    return nil, "'" .. tostring(res) .. "' matches " .. n .. " Items (ambiguous)"
end

---------------------------------------------------------------- loader: mod update checks (C++ thread does the network, Lua reads rows)
-- row = {name, installed, remote, state}; state = pending | uptodate | update | skipped:min_native | error:<msg>
local loader = {}
MSCore.loader = loader
local function loaderRows()
    local N = MSCore.native
    if not N or not N.loader_status then return {}, NO_NATIVE end
    local out = {}
    for line in tostring(N.loader_status()):gmatch("[^\n]+") do
        local name, inst, remote, state = line:match("^([^|]*)|([^|]*)|([^|]*)|([^|]*)$")
        if name then out[#out + 1] = { name = name, installed = inst, remote = remote, state = state } end
    end
    return out
end
-- every mod row (empty list when the scan has not run or the DLL is missing)
function loader.status() return (loaderRows()) end
-- rows with a newer version (the DLL only checks; the manager downloads)
function loader.updates()
    local out = {}
    for _, r in ipairs(loaderRows()) do if r.state == "update" then out[#out + 1] = r end end
    return out
end
local updateFns, loaderDone = {}, false
-- fn(row) once per mod that has an update, on the first tick after every mod was checked
function loader.on_update(fn) updateFns[#updateFns + 1] = fn end
events.on("tick", function()
    if loaderDone then return end
    local rows = loaderRows()
    if #rows == 0 then return end
    for _, r in ipairs(rows) do if r.state == "pending" then return end end
    loaderDone = true
    for _, r in ipairs(rows) do
        if r.state == "update" then
            for _, fn in ipairs(updateFns) do pcall(fn, r) end
        end
    end
end)

---------------------------------------------------------------- hacks registry (every workaround = named entry)
local hacks, hlist = {}, {}
MSCore.hacks = hacks
function hacks.add(h) hlist[#hlist + 1] = h return h end   -- {name, what, why, verified_build}
function hacks.list() return hlist end
hacks.add({ name = "click_hook", what = "Button clicks via OnClicked -> ForceLayoutPrepass + RegisterHook lookup by FullName",
    why = "Lua cannot bind a delegate to a Lua function directly", verified_build = 149200896 })
hacks.add({ name = "valid_before_call", what = "check IsValid() before any method call on a UObject",
    why = "native AV on null wrappers cannot be caught by pcall", verified_build = 149200896 })
hacks.add({ name = "linear_colors", what = "convert sRGB to linear for UMG colors",
    why = "UMG colors are linear, raw sRGB looks washed out", verified_build = 149200896 })
hacks.add({ name = "shipmove_typeid", what = "set cmd+0x28 type id 0x3a for NewObject ShipMovementInputCommand",
    why = "flush drops commands whose type id is 0", verified_build = 149200896 })
hacks.add({ name = "map_pin_hook", what = "pre-hook on DMSPlayerController.AddInputCommand stores the ObjectId of a MapPinInputCommand with bShow=1",
    why = "the map pin is the only pinned-sector source the game exposes; the hook only reads memory", verified_build = 149200896 })
hacks.add({ name = "teleport_warp", what = "warp = AddInputCommand(ShipTeleportInput{bSetTarget, NewLocation, NewQuat W=65536, NewChunkLocation, NewSolarSystem}), wrapped in SetPilotingInput on/off",
    why = "the player's manual warp sends exactly this (probe 2026-10-29); instant, no warp fuel used", verified_build = 149200896 })

return MSCore
