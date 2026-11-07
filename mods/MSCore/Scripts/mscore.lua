-- MSCore: shared helpers for Main Sequence UE4SS Lua mods. API v3. Read-only unless a command function is called.
-- Each UE4SS mod has its own Lua state: a consumer require()s its own copy of this file.
-- api = Lua module API (MSCore.require / MSCore.api). Separate counter from the native ABI (MSCore_api() in dllmain.cpp, now 4).
local MSCore = { api = 3, version = "0.7.5", verified_build = 149200896 }
-- C++ part (Mods/MSCore/dlls/main.dll) registers MSCoreNative_* globals before this file runs; nil if the DLL is not installed
MSCore.native = rawget(_G, "MSCoreNative_version") and {
    version = _G.MSCoreNative_version, alive = _G.MSCoreNative_alive, send_cmd = _G.MSCoreNative_send_cmd,
    view_send = _G.MSCoreNative_view_send, throttle = _G.MSCoreNative_throttle,
    offsets = _G.MSCoreNative_offsets, check_offsets = _G.MSCoreNative_check_offsets,
    new_order = _G.MSCoreNative_new_order, update_order = _G.MSCoreNative_update_order, delete_order = _G.MSCoreNative_delete_order,
    autopilot = _G.MSCoreNative_autopilot, skip_to = _G.MSCoreNative_skip_to, filter_send = _G.MSCoreNative_filter_send,
    warp_send = _G.MSCoreNative_warp_send, pinned = _G.MSCoreNative_pinned, pin_ensure = _G.MSCoreNative_pin_ensure,
    check = _G.MSCoreNative_check, loader_status = _G.MSCoreNative_loader_status,
    scan = _G.MSCoreNative_scan, chunk_hold = _G.MSCoreNative_chunk_hold, chunk_release = _G.MSCoreNative_chunk_release,
    send_typed = _G.MSCoreNative_send_typed, find_buildable = _G.MSCoreNative_find_buildable, build_place = _G.MSCoreNative_build_place,
    disassemble = _G.MSCoreNative_disassemble, set_recipe = _G.MSCoreNative_set_recipe, port_toggle = _G.MSCoreNative_port_toggle,
    inv_move = _G.MSCoreNative_inv_move, read_station = _G.MSCoreNative_read_station, read_inventory = _G.MSCoreNative_read_inventory, export_ok = _G.MSCoreNative_export_ok } or nil

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

-- typed send (API 4): sets cmdObj's type id at +0x28 when it is 0, then AddInputCommand(receiver, cmdObj).
-- typeId: BuildInput 0x2e, DisassembleInput 0x2f, InventoryInput 12, SetRecipeInput 20, MachinePortInput 17.
function cmd.send_typed(receiver, cmdObj, typeId)
    local N = MSCore.native
    if not N or not N.send_typed then return false, NO_NATIVE end
    return N.send_typed(receiver, cmdObj, typeId)
end

-- factory natives (API 4): thin wrappers over the MSCoreNative_* globals. Other factory helpers live in mscore_factory.lua.
local factory_native = {}
MSCore.factory_native = factory_native
local function fnative(name, ...)
    local N = MSCore.native
    if not N then return false, NO_NATIVE end
    local f = N[name]
    if not f then return false, "native " .. name .. " missing (MSCore API 4 DLL needed)" end
    return f(...)
end
-- Buildable data asset by name (e.g. "DAT_Refinery"), or nil. Cached per name in Lua; a cached object is reused only while IsValid.
-- ponytail: the cache lives in this Lua state. Native code must not keep the pointer across level or save loads.
local buildable_cache = {}
local CFG_MAX = 64 * 1024 -- cfg bytes cap (matches the native check)
function factory_native.find_buildable(datName)
    local N = MSCore.native
    if not N or not N.find_buildable then return nil end
    local b = buildable_cache[datName]
    if b then
        local okv, alive = pcall(function() return b:IsValid() end)
        if okv and alive == true then return b end
        buildable_cache[datName] = nil
    end
    b = N.find_buildable(datName)
    if b then buildable_cache[datName] = b end
    return b
end
-- cfgBytes: SerializedMachineConfig as a Lua string of raw bytes ("" or nil = none)
function factory_native.place(station, datName, x, y, h, rot, cfgBytes)
    if cfgBytes and #cfgBytes > CFG_MAX then return false, "cfg larger than 64 KiB" end
    local b = factory_native.find_buildable(datName)
    if not b then return false, "no Buildable named " .. tostring(datName) end
    return fnative("build_place", station, b, x, y, h, rot, cfgBytes or "")
end
function factory_native.disassemble(id, mass) return fnative("disassemble", id, mass) end
-- recipeObj: Recipe asset UObject; cfgBytes: "" or 4 bytes (int32 Config)
function factory_native.set_recipe(machine, recipeObj, amount, cfgBytes)
    if cfgBytes and #cfgBytes > CFG_MAX then return false, "cfg larger than 64 KiB" end
    return fnative("set_recipe", machine, recipeObj, amount, cfgBytes or "")
end
function factory_native.port(machine, port) return fnative("port_toggle", machine, port) end
function factory_native.inv_move(prim, sec, slot, action, receiver) return fnative("inv_move", prim, sec, slot, action, receiver) end
-- live station Blueprint: blocks, machines (see mscore_factory.lua parse_station) or nil, err
function factory_native.read_station(stationId)
    local N = MSCore.native
    if not N then return nil, NO_NATIVE end
    if not N.read_station then return nil, "native read_station missing (MSCore 0.7.2 DLL needed)" end
    local ok, text = N.read_station(stationId)
    if not ok then return nil, text end
    local F = package.loaded["mscore_factory"] or require("mscore_factory")
    return F.parse_station(text)
end
-- live Inventory ItemSlots by save id: slots {item,amount,max} ("" item = empty slot) or nil, err
function factory_native.read_inventory(invId)
    local N = MSCore.native
    if not N then return nil, NO_NATIVE end
    if not N.read_inventory then return nil, "native read_inventory missing (MSCore 0.7.3 DLL needed)" end
    local ok, text = N.read_inventory(invId)
    if not ok then return nil, text end
    local F = package.loaded["mscore_factory"] or require("mscore_factory")
    return F.parse_inventory(text)
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

---------------------------------------------------------------- world scan + chunk hold (native; see docs/mod-authors/lua-api/scan.md)
local SCAN_KINDS = { ship = 0, asteroid = 1, station = 2 }

-- splits s on "|" (empty fields kept)
local function splitBar(s)
    local out, i = {}, 1
    while true do
        local j = string.find(s, "|", i, true)
        if not j then out[#out + 1] = string.sub(s, i) break end
        out[#out + 1] = string.sub(s, i, j - 1)
        i = j + 1
    end
    return out
end

-- "x,y,z" -> {X=,Y=,Z=} numbers, nil when not three numbers
local function triple(s)
    local a, b, c = string.match(s, "^([^,]+),([^,]+),([^,]+)$")
    if not a then return nil end
    local X, Y, Z = tonumber(a), tonumber(b), tonumber(c)
    if not (X and Y and Z) then return nil end
    return { X = X, Y = Y, Z = Z }
end

-- one native row line -> table. hull/max_hull/order_idx are nil when -1; only ships have hull, docked, piloted, dock_id, order;
-- only rocks have loaded; world is nil when world_ok is 0.
local function scanRow(line)
    local f = splitBar(line)
    if #f < 18 then return nil end
    local kind = tonumber(f[2])
    local row = {
        id = tonumber(f[1]), kind = kind, sys = tonumber(f[3]),
        ch = triple(f[12]), loc = triple(f[13]),
        world = f[15] == "1" and triple(f[14]) or nil,
        name = f[16], type = f[17],
    }
    if kind == 0 then
        row.docked = f[5] == "1"
        row.piloted = f[6] == "1"
        row.dock_id = tonumber(f[7])
        row.hull = tonumber(f[8]) ~= -1 and tonumber(f[8]) or nil
        row.max_hull = tonumber(f[9]) ~= -1 and tonumber(f[9]) or nil
        row.order_idx = tonumber(f[10]) ~= -1 and tonumber(f[10]) or nil
        row.order_dock = tonumber(f[11])
        row.order = f[18] ~= "" and f[18] or nil
    elseif kind == 1 then
        row.loaded = f[4] == "1"
    end
    return row
end

-- pure-Lua scan, used only when the native scan is off (a native walk faulted once this session). Same row fields and filters as scanRow.
-- Rows are copied values read in one walk (no pointers kept), so no identity check is needed. Cache age is wall-clock seconds (os.time, 1 s steps).
local SCAN_MAX_AGE_MS = 5000   -- default max_age_ms and the cap, same as native SCAN_MAX_AGE_CAP_MS
local luaScanCache = {}
local function xyz(v) local t = vec3(v) return { X = t[1], Y = t[2], Z = t[3] } end
local function findAll(cls) local ok, l = pcall(FindAllOf, cls) return ok and l or {} end
local function classOf(o)
    local ok, c = pcall(function() return o:GetClass():GetFName():ToString() end)
    return ok and c or "?"
end
local function chunkOf(o)
    local ok, c = pcall(function() return o:GetChunkLocation() end)
    return xyz(ok and c or prop(o, "ChunkLocation"))
end
-- same order as native sys_id: GetSolarSystem, SolarSystemID / SolarSystemId, the SolarSystem object
local function sysOf(o)
    local ok, s = pcall(function() return o:GetSolarSystem() end)
    if ok and safe.valid(s) then return prop(s, "ID") or -1 end
    local id = prop(o, "SolarSystemID") or prop(o, "SolarSystemId")
    if id then return id end
    local sys = prop(o, "SolarSystem")
    return (safe.valid(sys) and prop(sys, "ID")) or 0
end
-- display text like native obj_display: DisplayName, then GetDisplayName; "" when neither
local function textOf(o)
    if not safe.valid(o) then return "" end
    local ok, t = pcall(function() return o.DisplayName:ToString() end)
    if ok and t and t ~= "" then return t end
    ok, t = pcall(function() return o:GetDisplayName():ToString() end)
    if ok and t then return t end
    return ""
end
-- current order of ship o, like native fill_order: idx = CurrentOrderIndex. Out of range: nil, 0, nil.
-- Returns order_idx, order_dock, order (class name; nil when the order object is gone).
local function orderOf(o, idx)
    local arr = prop(o, "Orders")
    if not arr or type(idx) ~= "number" or idx < 0 then return nil, 0, nil end
    local ord, i, n = nil, 0, 0
    pcall(function()
        arr:ForEach(function(_, el)
            if i == idx then ord = el:get() end
            i = i + 1
            n = n + 1
        end)
    end)
    if idx >= n then return nil, 0, nil end
    if not safe.valid(ord) then return idx, 0, nil end
    return idx, prop(ord, "DockID") or 0, classOf(ord)
end
local function worldOf(a)
    if not safe.valid(a) then return nil end
    local ok, p = pcall(function() return a:K2_GetActorLocation() end)
    return ok and p and xyz(p) or nil
end
-- ore items of ResourceItems[] (struct with Item, or the Item object itself)
local SCAN_ORES = 8   -- native MsScanRow.ores size
local function resOres(o)
    local out = {}
    local arr = prop(o, "ResourceItems")
    if arr then
        pcall(function()
            arr:ForEach(function(_, el)
                if #out >= SCAN_ORES then return end
                local v = el:get()
                local it = safe.valid(v.Item) and v.Item or v
                if safe.valid(it) then out[#out + 1] = it end
            end)
        end)
    end
    return out
end
-- lowercase names a type filter matches, like native row_matches: oname, row type, every ore display name
local function matchNames(row, oname, ores)
    local t = { string.lower(oname or ""), string.lower(row.type or "") }
    for _, it in ipairs(ores) do t[#t + 1] = string.lower(textOf(it)) end
    return t
end
-- kind: 0 ship, 1 asteroid (save objects, loaded = actor in the world), 2 station, 3 loaded rock actors only
-- Returns entries { row = <scan row>, names = <match names> }.
local function luaRows(kind)
    local out = {}
    local function add(row, oname, ores)
        out[#out + 1] = { row = row, names = matchNames(row, oname, ores or {}) }
    end
    if kind == 0 then
        local ctrl = safe.live("DMSPlayerController")
        local cid = safe.valid(ctrl) and prop(ctrl, "ID") or -1
        for _, cls in ipairs(R.ship) do
            for _, o in ipairs(findAll(cls)) do
                if safe.valid(o) then
                    local dock, pilot, mh = prop(o, "DockID") or 0, prop(o, "PilotID"), prop(o, "MaxHealth")
                    local okh, hull = pcall(function() return o:GetCurrentHull() end)
                    local ord, odock, oclass = orderOf(o, prop(o, "CurrentOrderIndex"))
                    local tx = textOf(o)
                    add({ id = prop(o, "ID") or -1, kind = 0, sys = sysOf(o), ch = chunkOf(o), loc = xyz(prop(o, "Location")),
                        world = worldOf(prop(o, "Actor")), name = tx ~= "" and tx or safe.objName(o), type = classOf(o),
                        docked = dock ~= 0, piloted = cid > 0 and pilot == cid,
                        dock_id = dock, hull = okh and type(hull) == "number" and hull or nil, max_hull = mh ~= -1 and mh or nil,
                        order_idx = ord, order_dock = odock, order = oclass }, safe.objName(o))
                end
            end
        end
    elseif kind == 1 or kind == 3 then
        local acts = {}   -- loaded rock actors by ObjectId (ids > 0, like native)
        for _, a in ipairs(findAll("BP_GenericResourceAsteroid_C")) do
            if safe.valid(a) then
                local id = prop(a, "ObjectId")
                if kind == 3 then
                    local okS, it = pcall(function() return a:GetScanItem() end)
                    local ore = (okS and safe.valid(it)) and it or nil
                    add({ id = id or 0, kind = 1, sys = sysOf(a), ch = chunkOf(a), loc = xyz(prop(a, "Location")), world = worldOf(a),
                        name = "", type = ore and textOf(ore) or "", loaded = true }, safe.objName(a), ore and { ore } or {})
                end
                if id and id > 0 then acts[id] = a end
            end
        end
        if kind == 1 then
            for _, o in ipairs(findAll("SmallResourceAsteroidObject")) do
                if safe.valid(o) and prop(o, "bAlive") ~= false then
                    local id = prop(o, "ID") or -1
                    local a = acts[id]
                    local ores = resOres(o)
                    add({ id = id, kind = 1, sys = sysOf(o), ch = chunkOf(o), loc = xyz(prop(o, "Location")), world = worldOf(a),
                        name = "", type = ores[1] and textOf(ores[1]) or "", loaded = a ~= nil }, safe.objName(o), ores)
                end
            end
        end
    elseif kind == 2 then
        for _, o in ipairs(findAll(R.dock)) do
            if safe.valid(o) then
                add({ id = prop(o, "ID") or -1, kind = 2, sys = sysOf(o), ch = chunkOf(o), loc = xyz(prop(o, "Location")),
                    world = worldOf(prop(o, "Actor")), name = textOf(o), type = classOf(o) }, safe.objName(o))
            end
        end
    end
    return out
end

local luaScanNoted = false
-- same contract as MSCore.scan (list of rows); the native cache is replaced by a plain max_age_ms cache here
local function luaScan(kind, f)
    local k = SCAN_KINDS[kind]
    local lk = (k == 1 and f.loaded_only) and 3 or k
    if not luaScanNoted then luaScanNoted = true; log("scan: native scan off, using the Lua fallback") end
    local maxAge = math.min(f.max_age_ms or SCAN_MAX_AGE_MS, SCAN_MAX_AGE_MS) / 1000
    local c = luaScanCache[lk]
    if not c or os.time() - c.t >= maxAge then
        c = { t = os.time(), rows = luaRows(lk) }
        luaScanCache[lk] = c
    end
    local want = string.lower(f.type or "")
    local sys = f.sys or 0
    local out = {}
    for _, e in ipairs(c.rows) do
        local okType = want == ""
        if not okType then
            for _, n in ipairs(e.names) do
                if n == want then okType = true break end
            end
        end
        if okType and (sys <= 0 or e.row.sys == sys) then out[#out + 1] = e.row end
    end
    return out
end

-- world objects of one kind: kind = "ship" | "asteroid" | "station". f (optional): max_age_ms (default 5000, capped at 5000 natively and in luaScan),
-- type (case-insensitive match on row type, oname or any ore name; "" = no filter), sys (solar system id; 0 = all),
-- loaded_only (asteroid: only rock actors in the world). Returns a list of rows (see scanRow), or nil, reason.
-- When the native scan is off (fault this session) the rows come from luaScan instead; the list shape is the same.
function MSCore.scan(kind, f)
    local k = SCAN_KINDS[kind]
    if k == nil then return nil, "unknown kind" end
    local N = MSCore.native
    if not N then return nil, NO_NATIVE end
    if not N.scan then return nil, "native scan missing" end
    f = f or {}
    local text, ok = N.scan(k, f.max_age_ms or SCAN_MAX_AGE_MS, f.type or "", f.sys or 0, f.loaded_only and 1 or 0)
    if ok == false then return luaScan(kind, f) end
    local out = {}
    for line in string.gmatch(text or "", "[^\n]+") do
        local row = scanRow(line)
        if row then out[#out + 1] = row end
    end
    return out
end

-- chunk hold: a 3x3x3 cube of chunks around ch (raw {X,Y,Z}) stays loaded for owner (integer). Returns ok, err.
-- ok is true for a new set and for an unchanged cube. Each owner holds one cube; a new call moves it.
MSCore.chunks = {}
function MSCore.chunks.hold(owner, sys, ch) return call("chunk_hold", owner, sys, ch.X, ch.Y, ch.Z) end
-- drops owner's cube (ok = true also when it had none)
function MSCore.chunks.release(owner) return call("chunk_release", owner) end

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

-- optional sibling modules (same Scripts dir, found by require). A failed load is logged, never thrown.
local function load_optional(name, install)
    local ok, mod = pcall(require, name)
    if not ok then log(name .. " load FAIL: " .. tostring(mod)) return end
    local oki, err = pcall(install, mod, MSCore)
    if not oki then log(name .. " install FAIL: " .. tostring(err)) end
end
load_optional("mscore_factory", function(mod, M) mod.install(M) end)
-- keep both names: MSCore.factory.native and MSCore.factory_native; the module's own fields win over the natives
if MSCore.factory then
    MSCore.factory.native = factory_native
    for _, k in ipairs({ "find_buildable", "place", "disassemble", "set_recipe", "port", "inv_move", "read_station", "read_inventory" }) do
        if MSCore.factory[k] == nil then MSCore.factory[k] = factory_native[k] end
    end
end
load_optional("mscore_inv", function(mod, M) mod.install(M) end)
load_optional("mscore_wbp", function(mod, M) mod.install(M) end)
load_optional("mscore_blocks", function(mod, M) mod.install(M) end)

return MSCore
