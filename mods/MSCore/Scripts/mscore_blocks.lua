-- mscore_blocks: adds blocks to the game's BuildingBlockRegistry and research unlocks at runtime (no pak).
-- Mods must not ship DAT_BuildingBlockRegistry or RSC_* overrides: two paks with the same asset lose to each other.
-- API: M.register{class='/Game/.../BPO_X.BPO_X_C', research='RSC_ComputationScience' or full path (optional),
--                  category='Functional Blocks' (default)} -> true | nil, err   (same class twice: ignored, true)
--      M.status() -> {[class] = 'pending' | 'block' | 'block+research' | 'failed:<why>'}
--      M.debug_dump()   console only, bound nowhere: registry categories and the first Unlocks element fields (probes)
--      M.install(MSCore) -> MSCore.blocks = M; starts one 250 ms tick step (once)
-- Timing: retried every tick until every class is found and appended, then re-checked every 20th tick (5 s).
-- The block and research steps are independent: a research failure keeps the block append.
-- UNPROVEN in game: research append (UnlockType copy, ResearchState recompute). Every game call goes through pcall.

local M = {}
local TAG = "[MSCore.blocks] "
local function L(m) print(TAG .. m .. "\n") end
local function valid(o)
    if type(o) ~= "userdata" and type(o) ~= "table" then return false end
    local s, v = pcall(function() return o:IsValid() end)
    return s and v == true
end
local function rd(o, k) local s, v = pcall(function() return o[k] end) if s then return v end return nil end
local function count(a) local s, n = pcall(function() return #a end) if s and type(n) == "number" then return n end return nil end
local function fullname(o) if not valid(o) then return nil end local s, v = pcall(function() return o:GetFullName() end) return s and v or nil end
local function sfo(path) local s, o = pcall(StaticFindObject, path) if s and valid(o) then return o end return nil end
local function find_all(cls) local s, l = pcall(FindAllOf, cls) if s and type(l) == "table" then return l end return {} end
local function find_reg() local s, r = pcall(FindFirstOf, "BuildingBlockRegistry") if s and valid(r) then return r end return nil end

-- AssetRegistryHelpers fallback, same as mscore_wbp.lua (class not loaded yet)
local ARH
local function getAsset(pkg, name)
    if not valid(ARH) then ARH = sfo("/Script/AssetRegistry.Default__AssetRegistryHelpers") end
    if not valid(ARH) then return nil end
    local s, r = pcall(function() return ARH:GetAsset({ PackageName = FName(pkg), AssetName = FName(name) }) end)
    if s and valid(r) then return r end
    return nil
end

local function find_class(s)
    local c = sfo(s.class)
    if valid(c) then return c end
    return getAsset(s.pkg, s.name)
end

local function research_path(r)
    if r:find("/", 1, true) then
        if r:find(".", 1, true) then return r end
        return r .. "." .. r:match("([^/]+)$")
    end
    return "/Game/Data/Research/" .. r .. "." .. r
end

local function find_category(reg, name)
    local found
    local function cb(i, e)
        local s1, c = pcall(function() return e:get() end)
        if not s1 or c == nil then return false end
        local s2, nm = pcall(function() return c.Name:ToString() end)
        if s2 and nm == name then found = c return true end
        return false
    end
    pcall(function() reg.Categories:ForEach(cb) end)
    return found
end

local function has_full(arr, n, full)
    for i = 1, n do
        local s, e = pcall(function() return arr[i] end)
        if s and fullname(e) == full then return true end
    end
    return false
end

-- the block step: returns "ok" or "failed:<why>"
local function block_step(s, reg, cls)
    local full = fullname(cls)
    if not full then return "failed:class name" end
    local cat = find_category(reg, s.category)
    if not cat then return "failed:no category" end
    local arr = rd(cat, "Classes")
    local n = count(arr)
    if n == nil then return "failed:no Classes array" end
    if has_full(arr, n, full) then return "ok" end
    local okw = pcall(function() arr[n + 1] = cls end)
    if not okw or count(arr) ~= n + 1 then return "failed:append, native arr_push fallback needed" end
    return "ok"
end

-- UnlockType of an existing unlock that has a Block set (copied; vanilla value for "unlocks a block")
local enum_cache
local function unlock_enum()
    if enum_cache ~= nil then return enum_cache end
    for _, rt in ipairs(find_all("ResearchTechnology")) do
        local u = rd(rt, "Unlocks")
        local n = count(u) or 0
        for i = 1, n do
            local s, e = pcall(function() return u[i] end)
            if s and valid(rd(e, "Block")) then
                local t = rd(e, "UnlockType")
                if t ~= nil then enum_cache = t return t end
            end
        end
    end
    return nil
end

local function has_block(u, n, full)
    for i = 1, n do
        local s, e = pcall(function() return u[i] end)
        if s and fullname(rd(e, "Block")) == full then return true end
    end
    return false
end

-- the research step: returns "ok", "pending" (research asset not loaded) or "failed:<why>"
local function research_step(s, cls)
    local full = fullname(cls)
    if not full then return "failed:class name" end
    local rsc = sfo(research_path(s.research))
    if not rsc then return "pending" end
    local u = rd(rsc, "Unlocks")
    local n = count(u)
    if n == nil then return "failed:no Unlocks array" end
    if has_block(u, n, full) then return "ok" end
    local en = unlock_enum()
    if en == nil then return "failed:research enum unknown (need P2)" end
    local okw = pcall(function() u[n + 1] = { UnlockType = en, bHidden = false, Block = cls } end)
    if not okw or count(u) ~= n + 1 then return "failed:research append" end
    return "ok"
end

local specs, order = {}, {}
local tickN, anyPending, installed = 0, false, false

local function status_of(s)
    if s.b and s.b:sub(1, 7) == "failed:" then return s.b end
    if s.r and s.r:sub(1, 7) == "failed:" then return s.r end
    if s.b ~= "ok" then return "pending" end
    if s.research and s.r == "ok" then return "block+research" end
    return "block"
end

local function pass_one(s, reg)
    local cls = reg and find_class(s) or nil
    if not valid(cls) then
        s.b = "pending"
        s.r = s.research and "pending" or nil
        if reg and tickN >= 40 and not s.waited then s.waited = true; L("waiting for class " .. s.class) end
        return
    end
    local okb, rb = pcall(block_step, s, reg, cls)
    s.b = okb and rb or ("failed:" .. tostring(rb))
    if s.research then
        local okr, rr = pcall(research_step, s, cls)
        s.r = okr and rr or ("failed:" .. tostring(rr))
    else
        s.r = nil
    end
end

local function step()
    if #order == 0 then return end
    tickN = tickN + 1
    if not anyPending and tickN % 20 ~= 0 then return end
    local reg = find_reg()
    anyPending = false
    for _, s in ipairs(order) do
        pcall(pass_one, s, reg)
        local st = status_of(s)
        if st ~= s.last then s.last = st; L(s.class .. " " .. st) end
        if st == "pending" then anyPending = true end
    end
end

function M.register(opts)
    if type(opts) ~= "table" then return nil, "register: opts must be a table" end
    local class = opts.class
    if type(class) ~= "string" or not class:match("^/Game/.+%.[%w_]+_C$") then
        return nil, "register: class must be /Game/.../<Name>.<Name>_C, got " .. tostring(class)
    end
    local research = opts.research
    if research ~= nil and type(research) ~= "string" then return nil, "register: research must be a string or nil" end
    local category = opts.category or "Functional Blocks"
    if type(category) ~= "string" then return nil, "register: category must be a string" end
    if specs[class] then return true end
    local pkg, name = class:match("^(.-)%.([^%.]+)$")
    local s = { class = class, pkg = pkg, name = name, research = research, category = category }
    specs[class] = s
    order[#order + 1] = s
    anyPending = true
    return true
end

function M.status()
    local t = {}
    for _, s in ipairs(order) do t[s.class] = status_of(s) end
    return t
end

function M.debug_dump()
    local reg = find_reg()
    if not reg then L("dump: BuildingBlockRegistry not found (main menu?)") return end
    pcall(function()
        reg.Categories:ForEach(function(i, e)
            local s1, c = pcall(function() return e:get() end)
            if not s1 or c == nil then return false end
            local _, nm = pcall(function() return c.Name:ToString() end)
            L(("cat %d '%s' classes=%s"):format(i, tostring(nm), tostring(count(rd(c, "Classes")))))
            return false
        end)
    end)
    for _, rt in ipairs(find_all("ResearchTechnology")) do
        local u = rd(rt, "Unlocks")
        local n = count(u) or 0
        if n > 0 then
            L("research " .. tostring(fullname(rt)) .. " unlocks=" .. n)
            local e = rd(u, 1)
            for _, k in ipairs({ "UnlockType", "bHidden", "Buildable", "Recipe", "Item", "Block" }) do
                local v = rd(e, k)
                if valid(v) then v = fullname(v) end
                L("  [1]." .. k .. " = " .. tostring(v))
            end
            return
        end
    end
    L("dump: no ResearchTechnology with Unlocks")
end

function M.install(MSCore)
    MSCore.blocks = M
    if installed then return M end
    if not (MSCore.events and MSCore.events.on) then L("no MSCore.events: tick not started") return M end
    installed = true
    MSCore.events.on("tick", function() pcall(step) end)
    return M
end

return M
