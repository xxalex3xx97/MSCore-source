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
                         "MSCore_set_recipe", "MSCore_port_toggle", "MSCore_inv_move" }) do
        check("export." .. n, function()
            local N = MSCore.native if not N or not N.export_ok then return "BROKEN", "native DLL not loaded or API < 4" end
            return yes(N.export_ok(n) == true, "GetProcAddress") end)
    end
    for _, n in ipairs({ "send_typed", "find_buildable", "build_place", "disassemble", "set_recipe", "port_toggle", "inv_move" }) do
        check("native." .. n, function()
            local N = MSCore.native if not N then return "BROKEN", "native DLL not loaded" end
            return yes(type(N[n]) == "function", "MSCoreNative_" .. n .. " registered") end)
    end
    check("factory_native.api", function()
        local f = MSCore.factory_native
        return yes(type(f) == "table" and type(f.place) == "function" and type(f.set_recipe) == "function"
            and type(MSCore.cmd.send_typed) == "function", "place/set_recipe/cmd.send_typed exist; nothing sent") end)
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
    for _, n in ipairs({ "classes", "controller", "sectors", "arrival", "jump_cost", "pin_hook", "filters_read", "find_item", "find_dock", "scan", "chunks" }) do
        check("world." .. n, nativeCheck(n))
    end
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
