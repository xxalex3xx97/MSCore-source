-- DevEval: runtime eval/probe tool inside MSCore's Lua side. DEV ONLY. Never opens sockets.
-- Gate: Mods/MSCore/deveval_on.txt must exist (checked at load and every poll); deveval_off.txt present = disabled even when on-marker exists.
-- Protocol (Mods/MSCore/deveval/): write eval_in/NAME.lua, then append "NAME" as a line to eval_in/_go.txt (no dir listing in Lua;
--   avoids spawning cmd). Shortcut: eval_in/now.lua runs by itself. Request runs once on the game thread, file renamed to NAME.lua.done.
--   Output: eval_out/NAME.txt (<=64 KB) + one tagged summary line in eval_out.txt (ring, ~200 KB).
-- Sandbox helpers (all pcall-safe, return nil on failure): see USAGE.txt. Names: log print find findall first obj_ok get set pnum
--   call dump ships enemies native hook unhook hooklog watch timer stop csv csv_row fmt MSCore
-- Long loops cannot be interrupted: use timer()/watch() instead of loops.
local M = { polls = 0, runs = 0, loaded = false, enabled = false }
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local ROOT = SRC:gsub("\\", "/"):gsub("Scripts/$", "")   -- Mods/MSCore/
local DIR = ROOT .. "deveval/"
local MAX_REQ_OUT, MAX_RING = 64 * 1024, 200 * 1024
local MSCore, worldTicks = nil, 0

local function exists(p) local f = io.open(p, "rb") if f then f:close() return true end return false end
local function on() return exists(ROOT .. "deveval_on.txt") and not exists(ROOT .. "deveval_off.txt") end
local function stamp() return os.date("%Y-%m-%d %H:%M:%S") end
local function writeFile(p, s, mode) local f = io.open(p, mode or "wb") if not f then return false end f:write(s) f:flush() f:close() return true end
local function readFile(p) local f = io.open(p, "rb") if not f then return nil end local s = f:read("a") f:close() return s end
local function ensureDirs()
    if exists(DIR .. "eval_in/_go.txt") then return end
    pcall(os.execute, 'mkdir "' .. (DIR .. "eval_in"):gsub("/", "\\") .. '" "' .. (DIR .. "eval_out"):gsub("/", "\\") .. '" 2>nul')
    writeFile(DIR .. "eval_in/_go.txt", "")
end

local function ring(line)
    local p = DIR .. "eval_out.txt"
    local f = io.open(p, "ab")
    if not f then return end
    local sz = f:seek("end") f:write(line .. "\n") f:flush() f:close()
    if sz and sz > MAX_RING then
        local s = readFile(p)
        if s then writeFile(p, s:sub(-math.floor(MAX_RING / 2))) end
    end
end

-- value formatter (depth limited; UObjects by full name)
local function fmt(v, d)
    d = d or 0
    local t = type(v)
    if t == "nil" then return "nil" elseif t ~= "table" and t ~= "userdata" then return tostring(v) end
    if t == "table" then
        if d >= 3 then return "{...}" end
        local parts, n = {}, 0
        for k, x in pairs(v) do
            n = n + 1
            if n > 40 then parts[#parts + 1] = "..." break end
            parts[#parts + 1] = tostring(k) .. "=" .. fmt(x, d + 1)
        end
        return "{" .. table.concat(parts, ", ") .. "}"
    end
    local ok, s = pcall(function() return v:GetFullName() end)
    if ok and s then return s end
    ok, s = pcall(function() return v:ToString() end)
    if ok and s then return s end
    ok, s = pcall(function() local g = v:get() if type(g) ~= "userdata" then return tostring(g) end return fmt(g, d + 1) end)
    if ok and s then return s end
    ok, s = pcall(function() return tostring(v.Value) end)  -- PNum-style struct
    if ok and s and s ~= "nil" then return "Value=" .. s end
    return tostring(v)
end
M.fmt = fmt

local function valid(o) if o == nil then return false end local ok, r = pcall(function() return o:IsValid() end) return ok and r == true end

local active = { timers = {}, hooks = {}, csvs = {} }

-- builds one sandbox env; out = capture buffer for the current request
local function makeEnv(out)
    local env = setmetatable({}, { __index = _G })
    local function log(...)
        local a = {}
        for i = 1, select("#", ...) do a[#a + 1] = fmt((select(i, ...))) end
        out[#out + 1] = table.concat(a, "\t")
    end
    env.print, env.log, env.MSCore, env.fmt = log, log, MSCore, fmt
    env.findall = function(cls)
        local ok, l = pcall(FindAllOf, cls)
        if not ok or not l then return {} end
        local r = {}
        for _, o in ipairs(l) do if valid(o) then r[#r + 1] = o end end
        return r
    end
    env.first = function(cls) local ok, o = pcall(FindFirstOf, cls) if ok and valid(o) then return o end end
    env.find = function(path) local ok, o = pcall(StaticFindObject, path) if ok and valid(o) then return o end end
    env.obj_ok = valid
    env.get = function(o, name)
        if not valid(o) then return nil end
        local ok, v = pcall(function() return o[name] end)
        if not ok then return nil end
        if type(v) == "userdata" then
            local ok2, g = pcall(function() return v:get() end)
            if ok2 and g ~= nil then return g end
        end
        return v
    end
    -- set(o, "Prop", v) or set(o, "Struct.Field", v); PNum: pass the raw int32 (pnum() reads raw/65536)
    env.set = function(o, name, v)
        if not valid(o) then return false end
        local ok, err = pcall(function()
            local t = o
            for seg in name:gmatch("([^.]+)%.") do t = t[seg] end
            t[name:match("([^.]+)$")] = v
        end)
        if not ok then log("set failed: " .. tostring(err)) end
        return ok
    end
    env.pnum = function(raw)
        if type(raw) == "userdata" or type(raw) == "table" then raw = env.get(raw, "Value") or raw end
        return type(raw) == "number" and raw / 65536 or nil
    end
    env.call = function(o, fn, ...)
        if not valid(o) then return false, "invalid object" end
        local args = table.pack(...)
        return pcall(function() return o[fn](o, table.unpack(args, 1, args.n)) end)
    end
    env.dump = function(o)
        if not valid(o) then log("dump: invalid object") return end
        local ok, cls = pcall(function() return o:GetClass() end)
        local c, depth = ok and cls or nil, 0
        while c and valid(c) and depth < 12 do
            log("== class " .. fmt(c))
            pcall(function()
                c:ForEachProperty(function(p)
                    local nm = p:GetFName():ToString()
                    local okv, v = pcall(function() return o[nm] end)
                    log("  " .. nm .. " = " .. (okv and fmt(v) or "<unreadable>"))
                end)
            end)
            local okS, sup = pcall(function() return c:GetSuperStruct() end)
            c = okS and sup or nil
            depth = depth + 1
        end
    end
    env.ships = function() local ok, l = pcall(function() return MSCore.ships.list() end) return ok and l or {} end
    env.enemies = function()
        local r = {}
        for _, o in ipairs(env.findall("EnemyShipObject")) do r[#r + 1] = { id = env.get(o, "ID"), name = fmt(o), obj = o } end
        return r
    end
    -- native("strafe", ...) -> MSCore.native.strafe, else global MSCoreNative_strafe, else a global of that exact name (Fleet/ShipControl exports)
    env.native = function(name, ...)
        local f = (MSCore.native and MSCore.native[name]) or rawget(_G, "MSCoreNative_" .. name) or rawget(_G, name)
        if type(f) ~= "function" then return nil, "no native '" .. tostring(name) .. "'" end
        local r = table.pack(pcall(f, ...))
        return table.unpack(r, 2, r.n)
    end
    -- hooks: only once a world is up (>= 8 s). unhook() only DISABLES the logger (UnregisterHook crashed the game before); unhook(id,true) really unregisters.
    env.hook = function(path, fn)
        if worldTicks < 32 then log("hook refused: world not ready (" .. worldTicks .. "/32 ticks)") return nil end
        local id = path
        if active.hooks[id] then
            active.hooks[id].fn = fn or active.hooks[id].fn
            active.hooks[id].on = true
            return id
        end
        local h = { on = true, fn = fn, ring = {}, n = 0 }
        local ok, a, b = pcall(RegisterHook, path, function(self, ...)
            if not h.on then return end
            local args = table.pack(...)
            local okc, e = pcall(function()
                local row = { t = stamp(), self = fmt(self) }
                for i = 1, args.n do row[#row + 1] = fmt(args[i]) end
                h.n = h.n + 1
                h.ring[(h.n - 1) % 200 + 1] = row
                if h.fn then h.fn(self, table.unpack(args, 1, args.n)) end
                ring("[hook " .. path:match("[^:.]+$") .. "] " .. row.t .. " " .. table.concat(row, " | "))
            end)
            if not okc then ring("[hook " .. path .. "] ERR " .. tostring(e)) end
        end)
        if not ok then log("RegisterHook failed: " .. tostring(a)) return nil end
        h.pre, h.post = a, b
        active.hooks[id] = h
        return id
    end
    env.unhook = function(id, real)
        local h = active.hooks[id]
        if not h then return false end
        h.on = false
        if real and h.pre then pcall(UnregisterHook, id, h.pre, h.post) active.hooks[id] = nil end
        return true
    end
    env.hooklog = function(id, n)
        local h = active.hooks[id]
        if not h then return {} end
        local r, tot = {}, math.min(h.n, 200)
        for i = math.max(1, tot - (n or 20) + 1), tot do r[#r + 1] = h.ring[(h.n - tot + i - 1) % 200 + 1] end
        return r
    end
    -- timer(name, ms, fn, maxRuns=600): resolution = 250 ms poll. fn errors stop the timer.
    env.timer = function(name, ms, fn, maxRuns)
        active.timers[name] = { ms = ms, fn = fn, last = 0, n = 0, max = maxRuns or 600 }
        return name
    end
    env.stop = function(name) active.timers[name] = nil end
    -- watch(name, fn, ms): fn() returns a value; logs to eval_out.txt when its formatted value changes
    env.watch = function(name, fn, ms)
        local lastS
        env.timer(name, ms or 250, function()
            local ok, v = pcall(fn)
            local s = ok and fmt(v) or ("ERR " .. tostring(v))
            if s ~= lastS then lastS = s ring("[watch " .. name .. "] " .. stamp() .. " " .. s) end
        end, 2400)
        return name
    end
    env.csv = function(name, header)
        active.csvs[name] = true
        writeFile(DIR .. "eval_out/" .. name .. ".csv", table.concat(header or {}, ",") .. "\n")
    end
    env.csv_row = function(name, ...)
        local a = {}
        for i = 1, select("#", ...) do a[#a + 1] = fmt((select(i, ...))) end
        local f = io.open(DIR .. "eval_out/" .. name .. ".csv", "ab")
        if f then f:write(table.concat(a, ",") .. "\n") f:flush() f:close() end
    end
    return env
end

local function runRequest(name)
    local path = DIR .. "eval_in/" .. name .. ".lua"
    if not exists(path) then return end
    local out = {}
    local f, err = loadfile(path, "t", makeEnv(out))
    local status = "OK"
    if not f then
        status = "LOADERR"
        out[#out + 1] = tostring(err)
    else
        local ok, e = xpcall(f, debug.traceback)
        if not ok then status = "ERROR" out[#out + 1] = tostring(e) end
    end
    local text = table.concat(out, "\n")
    if #text > MAX_REQ_OUT then text = text:sub(1, MAX_REQ_OUT) .. "\n[truncated]" end
    writeFile(DIR .. "eval_out/" .. name .. ".txt", "# " .. stamp() .. " " .. name .. " " .. status .. "\n" .. text .. "\n")
    ring("[" .. name .. "] " .. stamp() .. " " .. status .. " " .. text:gsub("\n", " | "):sub(1, 300))
    pcall(os.remove, path .. ".done")
    pcall(os.rename, path, path .. ".done")
    M.runs = M.runs + 1
end

local function pollOnce()
    M.polls = M.polls + 1
    local go = readFile(DIR .. "eval_in/_go.txt")
    local names = {}
    if go and #go > 0 then
        writeFile(DIR .. "eval_in/_go.txt", "")
        for n in go:gmatch("[^\r\n]+") do
            n = n:gsub("%.lua$", "")
            if n:match("^[%w_%-%.]+$") then names[#names + 1] = n end
        end
        table.sort(names)
    end
    if exists(DIR .. "eval_in/now.lua") then names[#names + 1] = "now" end
    for _, n in ipairs(names) do
        local ok, e = pcall(runRequest, n)
        if not ok then ring("[" .. n .. "] runRequest ERR " .. tostring(e)) end
    end
    local now = os.clock()
    for k, t in pairs(active.timers) do
        if now - t.last >= t.ms / 1000 - 0.05 then
            t.last = now
            t.n = t.n + 1
            local ok, e = pcall(t.fn)
            if not ok then ring("[timer " .. k .. "] ERR " .. tostring(e)) active.timers[k] = nil
            elseif t.n >= t.max then ring("[timer " .. k .. "] max runs reached, stopped") active.timers[k] = nil end
        end
    end
end

-- called from main.lua on every 250 ms tick (game thread)
function M.tick()
    local ok, e = pcall(function()
        if not on() then
            M.enabled = false
            if next(active.timers) then active.timers = {} end
            for _, h in pairs(active.hooks) do h.on = false end
            return
        end
        M.enabled = true
        local R, S = MSCore.resolve, MSCore.safe
        if S.live(table.unpack(R.controller)) then worldTicks = worldTicks + 1 else worldTicks = 0 end
        pollOnce()
    end)
    if not ok then pcall(print, "[DevEval] tick ERR " .. tostring(e) .. "\n") end
end

function M.init(core)
    MSCore = core
    M.loaded = true
    M.enabled = on()
    pcall(print, "[DevEval] loaded (dev_on marker present: " .. (M.enabled and "yes" or "no") .. ")\n")
    if M.enabled then pcall(ensureDirs) ring("[load] " .. stamp() .. " DevEval started") end
end

return M
