-- mscore_fleets: ship fleets (tags). Standalone: require("mscore_fleets") works without mscore.lua.
-- Read = native MSCoreNative_fleets_read (text). Write = MSCoreNative_fleet_cmd, gated by caps fleets_write.
local M = {}
local TTL = 1.0
local offReason, cache, cacheAt

local function out(s) print("[MSCore.fleets] " .. s .. "\n") end

function M.disable(why)
    offReason = tostring(why or "disabled")
    cache = nil
    print("[MSCore] COMPAT CAPS fleets=off:" .. offReason .. " (runtime)\n")
end

function M.invalidate() cache = nil end

local function nativeCaps()
    local f = rawget(_G, "MSCoreNative_offsets")
    if type(f) ~= "function" then return nil end
    local ok, s = pcall(f)
    return ok and tostring(s) or nil
end

function M.caps()
    if offReason then return "off:" .. offReason end
    if type(rawget(_G, "MSCoreNative_fleets_read")) ~= "function" then return "off:native too old" end
    return "ok"
end

function M.write_caps()
    local s = nativeCaps()
    local v = s and s:match("fleets_write=(%S+)")
    if v == "ok" then return "ok" end
    return "off:unverified"
end

local function parse(text)
    local tags, byId = {}, {}
    local ofs = {}
    for line in tostring(text):gmatch("[^\r\n]+") do
        local f = {}
        for p in (line .. "\t"):gmatch("([^\t]*)\t") do f[#f + 1] = p end
        if f[1] == "T" then
            local t = { id = tonumber(f[2]), parent = tonumber(f[3]), name = f[4] or "", ships = {} }
            if t.id then tags[#tags + 1] = t; byId[t.id] = t end
        elseif f[1] == "S" then
            local sid = tonumber(f[2])
            if sid then
                ofs[sid] = ofs[sid] or {}
                for id in tostring(f[5] or ""):gmatch("%d+") do
                    id = tonumber(id)
                    ofs[sid][#ofs[sid] + 1] = id
                    local t = byId[id]
                    if t then t.ships[#t.ships + 1] = sid end
                end
            end
        end
    end
    table.sort(tags, function(a, b)
        local x, y = a.name:lower(), b.name:lower()
        if x ~= y then return x < y end
        return a.id < b.id
    end)
    return { list = tags, ofs = ofs }
end

-- force = bypass the native 3 s cache too. Returns cache table | nil, why
local function refresh(force)
    if offReason then return nil, "off:" .. offReason end
    local fn = rawget(_G, "MSCoreNative_fleets_read")
    if type(fn) ~= "function" then return nil, "off:native too old (needs MSCore 0.8.x with fleets)" end
    if not force and cache and os.clock() - cacheAt < TTL then return cache end
    local ok, text, good = pcall(fn, force and 0 or 3000)
    if not ok then
        M.disable("error: " .. tostring(text))
        return nil, "off:" .. offReason
    end
    if good ~= true then return nil, "off:" .. tostring(text) end
    cache, cacheAt = parse(text), os.clock()
    return cache
end

function M.list()
    local c, why = refresh()
    if not c then return nil, why end
    return c.list
end

function M.of_ship(id)
    local c, why = refresh()
    if not c then return nil, why end
    local r = {}
    for i, v in ipairs(c.ofs[id] or {}) do r[i] = v end
    return r
end

local function cmd(op, a, b, name)
    local fn = rawget(_G, "MSCoreNative_fleet_cmd")
    if type(fn) ~= "function" then return false, "off:native too old" end
    local ok, r, err = pcall(fn, op, a or 0, b or 0, name or "")
    if not ok then return false, tostring(r) end
    if r == true then cache = nil return true end
    return false, tostring(err or "failed")
end

function M.create(name) return cmd("create", 0, 0, name) end
function M.rename(id, name) return cmd("rename", id, 0, name) end
function M.delete(id) return cmd("delete", id) end
function M.assign(fleet_id, ship_id) return cmd("assign", fleet_id, ship_id) end
function M.unassign(fleet_id, ship_id) return cmd("unassign", fleet_id, ship_id) end

function M.probe()
    local f = rawget(_G, "MSCoreNative_fleets_probe")
    if type(f) ~= "function" then return nil end
    local ok, r = pcall(f)
    return ok and r or nil
end

-- dev tool: log the properties of FleetTagInput commands the game sends (never sends anything)
local capturing
function M.capture(on)
    if on == false or capturing then return end
    capturing = true
    pcall(RegisterHook, "/Script/MainSequence.DMSPlayerController:AddInputCommand", function(self, cmdp)
        pcall(function()
            local c = cmdp and cmdp.get and cmdp:get() or cmdp
            local cls = c:GetClass()
            if not tostring(cls:GetFName():ToString()):find("FleetTagInput") then return end
            local recv = self and self.get and self:get() or self
            local parts = { "CAPTURE FleetTagInput receiver=" .. tostring(recv) }
            cls:ForEachProperty(function(p)
                local n = p:GetFName():ToString()
                local ok, v = pcall(function() return c[n] end)
                parts[#parts + 1] = n .. "=" .. tostring(ok and v or "?")
            end)
            out(table.concat(parts, " "))
        end)
    end, function() end)
end

function M.install(MSCore) MSCore.fleets = M end

return M
