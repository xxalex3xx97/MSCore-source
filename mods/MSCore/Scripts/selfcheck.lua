-- Selfcheck: read-only checks of the API and the game state, written to Mods\MSCore\selfcheck.log. Sends no game command.
-- World checks run in C++ (MSCoreNative_check). Statuses: OK, BROKEN, SKIP (precondition missing), CHANGED (exe size differs).
local M = {}
local SRC = ((debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""):gsub("\\", "/"))  -- .../Mods/MSCore/Scripts/
local LOGF = SRC .. "../selfcheck.log"
local EXE = SRC .. "../../../../MainSequence-Win64-Shipping.exe"

function M.run(MSCore)
    local S, UI, U, R = MSCore.safe, MSCore.ui, MSCore.units, MSCore.resolve
    local lines, n = {}, { OK = 0, BROKEN = 0, SKIP = 0, CHANGED = 0 }
    local function add(name, st, detail)
        n[st] = (n[st] or 0) + 1
        lines[#lines + 1] = string.format("%-24s %-8s %s", name, st, detail or "")
    end
    local function check(name, f)
        local ok, st, detail = pcall(f)
        if ok then add(name, st, detail) else add(name, "BROKEN", tostring(st)) end
    end
    local function yes(cond, detail) return cond and "OK" or "BROKEN", detail end
    local function live(k) return S.live(table.unpack(R[k])) end

    check("core.api", function() return yes(MSCore.api == 3, "api=" .. tostring(MSCore.api) .. " v" .. MSCore.version) end)
    check("safe.try", function() local ok, r = S.try("selfcheck", function() return 42 end) return yes(ok and r == 42) end)
    check("safe.valid", function() return yes(S.valid(nil) == false and S.valid(5) == false) end)
    check("safe.rd", function() return yes(S.rd(function() return 1 end) == "1" and S.rd(function() return nil end) == "?") end)
    check("safe.live player", function()
        local p = live("player") if not p then return "SKIP", "no player" end
        return yes(S.valid(p), S.objName(p)) end)
    check("safe.live controller", function()
        local c = live("controller") if not c then return "SKIP", "no controller" end
        return yes(S.valid(c), S.objName(c)) end)
    check("safe.val", function()
        local p = live("player") if not p then return "SKIP", "no player" end
        local v = S.val(p.bCreative) return yes(type(v) == "boolean", "bCreative=" .. tostring(v)) end)
    check("safe.nameOf/objName", function()
        local p = live("player") if not p then return "SKIP", "no player" end
        return yes(S.objName(p) ~= "?", S.nameOf(p:GetFName())) end)
    check("resolve.controllerPath", function()
        local o = StaticFindObject(R.controllerPath) return yes(S.valid(o), R.controllerPath) end)
    check("resolve.item (FindAllOf)", function()
        local l = FindAllOf(R.item) return yes(l and #l > 0, l and (#l .. " items") or "nil") end)
    check("items.icon", function()
        local I = MSCore.items
        if not (I and I.icon and I.set_icon) then return "BROKEN", "items API missing in mscore" end
        local l = FindAllOf(R.item) if not (l and #l > 0) then return "SKIP", "no items" end
        local withIcon = 0
        for _, it in ipairs(l) do local ok, ic = pcall(function() return it.Icon end) if ok and S.valid(ic) then withIcon = withIcon + 1 end end
        local t, why = I.icon("ITEM_TitaniumOre")
        local d = withIcon .. "/" .. #l .. " items have Icon; ITEM_TitaniumOre=" .. (t and "icon" or tostring(why))
        if tostring(why):find("^off:") then return "BROKEN", d end
        if withIcon > 0 then return "OK", d end
        return "SKIP", d .. " (0: ores have no runtime Icon, Mining keeps its baked textures)"
    end)
    check("items.list", function()
        local W = MSCore.wbp
        if not (W and W.item_list) then return "BROKEN", "wbp.item_list missing" end
        local l, why = W.item_list()
        if not l then if tostring(why):find("^off:") then return "BROKEN", tostring(why) end return "SKIP", tostring(why) end
        return yes(#l > 0, #l .. " items, first=" .. tostring(l[1] and l[1].name))
    end)
    check("ui.mk/setText", function()
        local c = live("controller") if not c then return "SKIP", "no controller" end
        local tb = UI.mk("TextBlock", c) UI.setText(tb, "selfcheck") return yes(S.valid(tb)) end)
    check("ui.rgba", function() return yes(math.abs(UI.rgba(255, 255, 255).R - 1) < 1e-6 and UI.rgba(0, 0, 0, 0.5).A == 0.5) end)
    check("units", function()
        return yes(U.abs_pos({ X = 1, Y = 0, Z = 0 }, { X = 65536, Y = 0, Z = 0 }).X == 2049
            and math.abs(U.dist_m({ X = 0, Y = 0, Z = 0 }, { X = 3, Y = 4, Z = 0 }) - 5) < 1e-9) end)
    check("events", function() return yes(MSCore.events.running(), "tick loop running") end)
    check("native.version", function()
        local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded (Mods/MSCore/dlls/main.dll missing?)" end
        local v = N.version() return yes(v == MSCore.version, "dll=" .. tostring(v) .. " lua=" .. MSCore.version) end)
    check("native.alive", function()
        local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded" end
        if N.alive(nil) ~= false or N.alive(5) ~= false then return "BROKEN", "alive(nil/5) must be false" end
        local p = live("player") if not p then return "SKIP", "no player (nil/5 checks passed)" end
        return yes(N.alive(p) == true, "alive(player)=true") end)
    check("native.offsets", function()
        local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded" end
        local s = N.offsets() return yes(type(s) == "string" and s:find("0x28", 1, true) ~= nil, tostring(s)) end)
    check("native.caps no runtime off", function()   -- module_disable adds "<module> exception" to the caps text
        local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded" end
        local s = tostring(N.offsets()) return yes(not s:find("exception", 1, true), s) end)
    check("widgets.contract", function()
        local W = MSCore.wbp
        if not (W and W.selftest and W.widget) then return "BROKEN", "widgets API missing in mscore_wbp" end
        local allok, detail = W.selftest()
        if allok then return "OK", detail .. " caps=" .. W.caps() end
        if tostring(W.caps()):find("^off:error") or tostring(detail):find("^killed") then return "BROKEN", detail end
        return "SKIP", detail
    end)
    check("wbp.fleet_open", function()   -- resolves only, never opens a window
        local W = MSCore.wbp
        if not (W and W.open_fleets_window and W.fleets_window_open and W.close_fleets_window and W.open_fleet_assign and W.fleet_assign_caps and W.probe_fleet_popup) then return "BROKEN", "fleet open API missing in mscore_wbp" end
        local a, b = W.widget("fleet_window"), W.widget("fleet_tag_popup")
        local d = "fleet_window=" .. tostring(a ~= nil) .. " tag_popup=" .. tostring(b ~= nil) .. " assign=" .. tostring(W.fleet_assign_caps())
        if a and b then return "OK", d end
        if a and not b then
            local names = {}
            for i, c in ipairs(W.fleet_popup_candidates() or {}) do names[i] = c.name end
            return "SKIP", d .. " candidates=" .. (#names > 0 and table.concat(names, ",") or "none")
        end
        return "SKIP", d
    end)
    check("wbp.hologram", function()
        local W = MSCore.wbp
        if not (W and W.hologram_texture and W.set_ship_hologram and W.hologram_caps) then return "BROKEN", "hologram API missing in mscore_wbp" end
        local sh = MSCore.ships.list()[1] if not sh then return "SKIP", "no ships" end
        local t, why = W.hologram_texture(sh.id)
        if t then return yes(S.valid(t), "ship " .. sh.id .. " " .. S.objName(t)) end
        if tostring(why):find("^off:") then return "BROKEN", why end
        return "SKIP", tostring(why)
    end)
    -- the three wbp.hologram checks below need a consumer host (opts.host, a panel of a live window). The selfcheck state has none and MSCore never
    -- creates unrooted widgets, so they SKIP with "no host" here; run them from a consumer that passes its host.
    check("wbp.hologram ttl", function()
        local W = MSCore.wbp
        if not (W and W.hologram_stats) then return "BROKEN", "hologram_stats missing" end
        local sh = MSCore.ships.list()[1] if not sh then return "SKIP", "no ships" end
        local t, why = W.hologram_texture(sh.id)
        if not t then return tostring(why):find("^off:") and "BROKEN" or "SKIP", tostring(why) end
        local a = W.hologram_stats()
        W.hologram_texture(sh.id)
        local b = W.hologram_stats()
        if b.reuses == a.reuses + 1 and b.renders == a.renders then return "OK", "reuse, renders=" .. b.renders end
        return "BROKEN", "second call did not reuse"
    end)
    check("wbp.hologram refresh", function()
        local W = MSCore.wbp
        if not (W and W.hologram_stats) then return "BROKEN", "hologram_stats missing" end
        local sh = MSCore.ships.list()[1] if not sh then return "SKIP", "no ships" end
        if not W.hologram_texture(sh.id) then return "SKIP", "no picture yet" end
        local a = W.hologram_stats()
        local t, why = W.hologram_texture(sh.id, { force = true, refresh_ok = true })
        local b = W.hologram_stats()
        if tostring(why):find("^off:") then return "BROKEN", why end
        local flag = W.refresh_enabled and "ON" or "OFF"
        if W.refresh_enabled then return "SKIP", "refresh constant is ON, not asserted" end
        if b.renders == a.renders and b.created == a.created then return "OK", "no re-render (refresh " .. flag .. " by design)" end
        return "BROKEN", "re-rendered although refresh is " .. flag
    end)
    check("wbp.hologram consumer", function()
        -- one Lua state only: a true second-state test (a mod requiring mscore_wbp alone) is not possible here; this calls the same module table a mod gets
        local W = MSCore.wbp
        if not (W and W.ship_hologram and W.hologram_texture) then return "BROKEN", "ship_hologram missing" end
        local sh = MSCore.ships.list()[1] if not sh then return "SKIP", "no ships" end
        local t, why = W.ship_hologram(sh.id)
        if not t then return tostring(why):find("^off:") and "BROKEN" or "SKIP", tostring(why) end
        if not S.valid(t) then return "BROKEN", "returned texture not valid" end
        local t2, why2 = W.ship_hologram(sh.id)
        if not t2 then return "SKIP", "second call: " .. tostring(why2) end
        if not S.valid(t2) then return "BROKEN", "refreshed texture not valid" end
        return "OK", "old handle after refresh: " .. (S.valid(t) and "still valid" or "invalid (stale)")
    end)
    check("blocks.contract", function()
        local B = MSCore.blocks
        if not (B and B.selftest and B.caps) then return "BROKEN", "blocks API missing in mscore_blocks" end
        local allok, detail = B.selftest()
        if allok then return "OK", detail .. " caps=" .. B.caps() end
        if tostring(detail):find("^killed") then return "BROKEN", detail end
        return "SKIP", detail
    end)
    check("rocks.caps", function() local c = MSCore.caps().rocks return yes(c == "ok", tostring(c)) end)
    check("native.check_offsets", function()
        local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded" end
        local ok, d = N.check_offsets()
        if not ok and tostring(d):find("^SKIP") then return "SKIP", tostring(d) end
        return yes(ok == true, tostring(d)) end)
    check("native.send_cmd guard", function()
        local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded" end
        local ok, why = N.send_cmd(1, nil) return yes(ok == false, tostring(why)) end)
    -- API 4 factory exports: C exports resolve by GetProcAddress (export_ok), Lua natives are globals. Sends nothing.
    for _, n in ipairs({ "MSCore_send_typed", "MSCore_find_buildable", "MSCore_build_place", "MSCore_disassemble",
                         "MSCore_set_recipe", "MSCore_port_toggle", "MSCore_inv_move", "MSCore_inv_move_to", "MSCore_inv_split", "MSCore_chunk_hold2",
                         "MSCore_class", "MSCore_scan_rocks" }) do
        check("export." .. n, function()
            local N = MSCore.native if not N or not N.export_ok then return "BROKEN", "native DLL not loaded or API < 4" end
            return yes(N.export_ok(n) == true, "GetProcAddress") end)
    end
    for _, n in ipairs({ "send_typed", "find_buildable", "build_place", "disassemble", "set_recipe", "port_toggle", "inv_move", "read_station", "read_docks", "rename_dock", "read_inventory", "inv_split" }) do
        check("native." .. n, function()
            local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded" end
            return yes(type(N[n]) == "function", "MSCoreNative_" .. n .. " registered") end)
    end
    check("factory_native.api", function()
        local f = MSCore.factory_native
        return yes(type(f) == "table" and type(f.place) == "function" and type(f.set_recipe) == "function"
            and type(MSCore.cmd.send_typed) == "function", "place/set_recipe/cmd.send_typed exist; nothing sent") end)
    check("factory_native.rename_ship", function() return yes(type(MSCore.factory_native.rename_ship) == "function", "Lua wrapper over native.rename_dock") end)
    -- ship checks use the first ship; SKIP when none
    local firstShip = nil
    check("ships.list", function()
        local l = MSCore.ships.list() if #l == 0 then return "SKIP", "no ships" end
        firstShip = l[1] return yes(#l > 0, #l .. " ships, first id=" .. tostring(firstShip.id) .. " " .. firstShip.name) end)
    local function needShip() if not firstShip then return nil end return firstShip.id end
    check("ships.mine", function()
        local id = needShip() if not id then return "SKIP", "no ships" end
        return yes(type(MSCore.ships.mine()) == "table", #MSCore.ships.mine() .. " mining ships") end)
    check("ships.get", function()
        local id = needShip() if not id then return "SKIP", "no ships" end
        local e = MSCore.ships.get(id) return yes(e ~= nil and e.id == id, "get(" .. id .. ")") end)
    check("ships.is_docked", function()
        local id = needShip() if not id then return "SKIP", "no ships" end
        local d = MSCore.ships.is_docked(id) return yes(type(d) == "boolean", "docked=" .. tostring(d)) end)
    check("ships.hull", function()
        local id = needShip() if not id then return "SKIP", "no ships" end
        local h = MSCore.ships.hull(id) return yes(type(h) == "table", "current=" .. tostring(h and h.current) .. " max=" .. tostring(h and h.max)) end)
    check("ships.pilot_console", function()
        local id = needShip() if not id then return "SKIP", "no ships" end
        local c = MSCore.ships.pilot_console(id) if not c then return "SKIP", "no pilot console for ship " .. id end
        return yes(S.valid(c), S.objName(c)) end)
    check("ships.warp_fuel", function()
        local id = needShip() if not id then return "SKIP", "no ships" end
        local f, why = MSCore.ships.warp_fuel(id) return yes(type(f) == "number", "warp_fuel=" .. tostring(f) .. " " .. tostring(why or "")) end)
    check("steer.api", function()
        local st = MSCore.steer
        return yes(type(st.aim) == "function" and type(st.stop) == "function" and type(st.throttle) == "function"
            and type(MSCore.cmd.send) == "function", "aim/stop/throttle/cmd.send exist; nothing sent") end)
    check("orders.api", function()
        local o, f, w = MSCore.orders, MSCore.filters, MSCore.warp
        return yes(type(o.new) == "function" and type(o.update) == "function" and type(o.delete) == "function" and type(o.autopilot) == "function"
            and type(o.skip_to) == "function" and type(f.send) == "function" and type(w.send) == "function", "orders/filters/warp.send exist; nothing sent")
    end)
    check("loader.api", function()
        local l = MSCore.loader
        return yes(type(l.status) == "function" and type(l.updates) == "function" and type(l.on_update) == "function", "status/updates/on_update exist")
    end)
    check("loader.scan", function()
        local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded" end
        local rows = MSCore.loader.status()
        if #rows == 0 then return "SKIP", "no mod rows (scan not finished, or no mscore.mod installed)" end
        return yes(type(N.loader_status()) == "string", #rows .. " rows, first=" .. rows[1].name .. " " .. rows[1].state)
    end)
    -- world checks run in C++ (MSCoreNative_check): read-only, SKIP when the world they need is not loaded
    local function nativeCheck(name)
        return function()
            local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded" end
            local ok, d = N.check(name)
            if not ok and tostring(d):find("^SKIP") then return "SKIP", tostring(d) end
            return yes(ok == true, tostring(d))
        end
    end
    for _, n in ipairs({ "classes", "controller", "sectors", "arrival", "jump_cost", "pin_hook", "filters_read", "find_item", "find_dock", "scan", "rocks", "ships", "flight", "repair", "chunks", "paused", "stations", "fleets", "fleets_write", "rockreg" }) do
        check("world." .. n, nativeCheck(n))
    end
    check("ships.mining_caps", nativeCheck("mining_caps"))   -- API 15, C++ check_one "mining_caps" (first live ship)
    check("fleets.list", function()
        if not MSCore.fleets then return "BROKEN", "MSCore.fleets missing" end
        local l, why = MSCore.fleets.list()
        if not l then return "BROKEN", tostring(why) end
        if #l == 0 then return "SKIP", "no fleets in this save" end
        local o = MSCore.fleets.of_ship(l[1].ships[1] or 0)
        return yes(type(o) == "table", #l .. " fleets, first=" .. l[1].name .. " ships=" .. #l[1].ships)
    end)
    check("scan.api", function()
        if type(MSCore.scan) ~= "function" or type(MSCore.chunks.hold) ~= "function" or type(MSCore.chunks.release) ~= "function" then
            return "BROKEN", "MSCore.scan / chunks.hold / chunks.release missing"
        end
        local rows, err = MSCore.scan("ship")
        if not rows then return "BROKEN", tostring(err) end
        if #rows == 0 then return "SKIP", "no ship rows" end
        return yes(type(rows[1].id) == "number", #rows .. " ship rows, first id=" .. tostring(rows[1].id)) end)
    check("warp.sectors", function()
        local l = MSCore.warp.sectors() if #l == 0 then return "SKIP", "no sectors" end
        return yes(#l > 0, #l .. " sectors, first id=" .. tostring(l[1].id) .. " " .. tostring(l[1].name)) end)
    check("warp.pinned", function()
        if not MSCore.native then return "BROKEN", "native DLL not loaded" end
        local p = MSCore.warp.pinned_sector() return yes(type(p) == "number", "pinned=" .. tostring(p)) end)
    check("docks", function()
        local d = MSCore.docks() if #d == 0 then return "SKIP", "no docks" end
        return yes(#d > 0, #d .. " docks, first id=" .. tostring(d[1].id) .. " " .. d[1].name) end)
    -- 0.8.3 native docks. sampleDock = first dock with a numeric ParentID (Lua read): its station id and the Dock object.
    local function sampleDock()
        for _, e in ipairs(MSCore.docks()) do
            local ok, p = pcall(function() return e.obj.ParentID end)
            if ok and type(p) == "number" then return p, e.obj end
        end
    end
    check("native.read_docks", function()
        local N = MSCore.native if not N or not N.read_docks then return "BROKEN", "native read_docks missing (MSCore 0.8.3 DLL needed)" end
        local sid = sampleDock() if not sid then return "SKIP", "no dock with a readable ParentID" end
        local rows, err = MSCore.factory_native.read_docks(sid)
        if not rows then return "BROKEN", tostring(err) end
        if #rows == 0 then return "BROKEN", "station " .. sid .. ": Lua sees docks, read_docks gave none" end
        local r = rows[1]
        local blocks = MSCore.factory_native.read_station(sid) or {}
        local hits = 0
        for _, b in ipairs(blocks) do
            for _, d in ipairs(rows) do if b.bid and b.bid >= 0 and b.bid == d.block_id then hits = hits + 1 end end
        end
        -- 0.8.3 block mids (9th column): a dock block's child machines should list a dock machine_id (dock BlockID may be -1)
        local dockIds, mhits = {}, 0
        for _, d in ipairs(rows) do if d.machine_id then dockIds[d.machine_id] = true end end
        for _, b in ipairs(blocks) do
            for _, m in ipairs(b.mids or {}) do if dockIds[m] then mhits = mhits + 1 break end end
        end
        local firstMids = blocks[1] and table.concat(blocks[1].mids or {}, ",") or "(no block)"
        return yes(true, #rows .. " docks of station " .. sid .. ", first machine_id=" .. tostring(r.machine_id) .. " raw_id=" .. tostring(r.raw_id)
            .. " block_id=" .. tostring(r.block_id) .. " " .. tostring(r.class) .. " '" .. tostring(r.name) .. "'; blocks whose bid is a dock block_id: " .. hits
            .. "; blocks whose mids contain a dock machine_id: " .. mhits .. " of " .. #rows .. " named dock rows; first block mids: " .. firstMids)
    end)
    check("hacks", function() return yes(#MSCore.hacks.list() > 0, #MSCore.hacks.list() .. " entries") end)
    check("blocks.status", function()
        local B = MSCore.blocks if not B then return "BROKEN", "mscore_blocks not loaded" end
        local st, keys, bad, pend = B.status(), {}, 0, 0
        for k in pairs(st) do keys[#keys + 1] = k end
        table.sort(keys)
        if #keys == 0 then return "SKIP", "nothing registered" end
        local parts = {}
        for _, k in ipairs(keys) do
            local v = st[k]
            parts[#parts + 1] = k:match("([^/%.]+)_C$") .. "=" .. v
            if v:sub(1, 7) == "failed:" then bad = bad + 1 elseif v == "pending" then pend = pend + 1 end
        end
        local d = table.concat(parts, " ")
        if bad > 0 then return "BROKEN", d end
        if pend > 0 then return "SKIP", d end
        return yes(true, d) end)
    check("exe size", function()
        local fh = io.open(EXE, "rb") if not fh then return "SKIP", "cannot open " .. EXE end
        local size = fh:seek("end") fh:close()
        if size == MSCore.verified_build then return "OK", "size=" .. size end
        return "CHANGED", "size=" .. size .. " last verified=" .. MSCore.verified_build .. " (game updated? re-verify)"
    end)

    local head = string.format("MSCore %s api %d selfcheck %s", MSCore.version, MSCore.api, os.date("%Y-%m-%d %H:%M:%S"))
    local tail = string.format("SUMMARY ok=%d broken=%d changed=%d skip=%d", n.OK, n.BROKEN, n.CHANGED, n.SKIP)
    print("[MSCore] " .. head .. "\n")
    for _, l in ipairs(lines) do print("[MSCore] " .. l .. "\n") end
    print("[MSCore] " .. tail .. "\n")
    local fh = io.open(LOGF, "w")
    if fh then
        fh:write(head .. "\n" .. table.concat(lines, "\n") .. "\n" .. tail .. "\n")
        fh:close()
    else
        print("[MSCore] cannot write " .. LOGF .. "\n")
    end
    return n
end

return M
