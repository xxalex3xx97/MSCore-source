-- mscore_blocks: adds blocks to the game's BuildingBlockRegistry and (opt-in) research unlocks at runtime (no pak).
-- Mods must not ship DAT_BuildingBlockRegistry or RSC_* overrides: two paks with the same asset lose to each other.
-- API: M.register{class='/Game/.../BPO_X.BPO_X_C', research='RSC_ComputationScience' or full path (optional),
--                  category='Functional Blocks' (default)} -> true | nil, err   (same class twice: ignored, true)
--      M.allow_research = false (default). Set true before register() to let research= append. See docs blocks.md.
--      M.status() -> {[class] = 'pending' | 'block' | 'block+research' | 'failed:<why>'}
--      M.debug_dump()   console only, bound nowhere: registry categories and the first Unlocks element fields (probes)
--      M.install(MSCore) -> MSCore.blocks = M. Defines the API only. The 250 ms tick starts on the first successful register().
-- Timing: retried every tick until every class is found and appended, then re-checked every 20th tick (5 s).
-- Failure is sticky: a write (append) that throws or does not grow the array by exactly one marks that part dead.
--   A dead part is never written again (no retry, no repeat log). The block and research parts are independent.
-- UNPROVEN in game: TArray append on the registry, research append (UnlockType copy, ResearchState recompute).
--   Verify with M.debug_dump() and one trial append in a dev build before relying on it. Every game call goes through pcall.

local M = {}
M.allow_research = false
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

-- the block step: returns "ok" or "failed:<why>". A write that fails sets s.bdead (never retried).
local function block_step(s, reg, cls)
    local full = fullname(cls)
    if not full then return "failed:class name" end
    local cat = find_category(reg, s.category)
    if not valid(cat) then return "failed:no category" end
    local arr = rd(cat, "Classes")
    local n = count(arr)
    if n == nil then return "failed:no Classes array" end
    if has_full(arr, n, full) then return "ok" end
    -- single write. Not retried: any result other than exactly n+1 is final.
    local okw = pcall(function() arr[n + 1] = cls end)
    local n2 = count(arr)
    if okw and n2 == n + 1 then return "ok" end
    s.bdead = true
    if not okw then return "failed:append threw (array may have grown)" end
    return "failed:append (count " .. tostring(n2) .. ", expected " .. tostring(n + 1) .. ")"
end

-- UnlockType of an existing unlock that has a Block set (copied; vanilla value for "unlocks a block").
-- Negative result is cached: search again only every 12 calls, 5 searches at most, then give up (logs once).
local enum_cache
local enum_calls, enum_next, enum_tries, enum_gaveup = 0, 0, 0, false
local function unlock_enum()
    if enum_cache ~= nil then return enum_cache end
    if enum_gaveup then return nil end
    enum_calls = enum_calls + 1
    if enum_calls < enum_next then return nil end
    enum_tries = enum_tries + 1
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
    if enum_tries >= 5 then
        enum_gaveup = true
        L("UnlockType source not found after 5 tries: research append gives up until reload")
        return nil
    end
    enum_next = enum_calls + 12
    return nil
end

local function has_block(u, n, full)
    for i = 1, n do
        local s, e = pcall(function() return u[i] end)
        if s and fullname(rd(e, "Block")) == full then return true end
    end
    return false
end

-- the research step: returns "ok", "pending" (research asset not loaded) or "failed:<why>". A failed write sets s.rdead.
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
    local n2 = count(u)
    if okw and n2 == n + 1 then return "ok" end
    s.rdead = true
    if not okw then return "failed:research append threw (array may have grown)" end
    return "failed:research append (count " .. tostring(n2) .. ", expected " .. tostring(n + 1) .. ")"
end

local specs, order = {}, {}
local tickN, anyPending, ticking = 0, false, false
local MS -- set by install()

local function status_of(s)
    if s.b and s.b:sub(1, 7) == "failed:" then return s.b end
    if s.r and s.r:sub(1, 7) == "failed:" then return s.r end
    if s.b ~= "ok" then return "pending" end
    if s.research and s.r == "ok" then return "block+research" end
    return "block"
end

local function research_on(s) return s.research ~= nil and M.allow_research == true end

local function pass_one(s, reg)
    local rwant = research_on(s)
    if s.research and not M.allow_research and not s.rnote then
        s.rnote = true
        L(s.class .. " research append disabled (set MSCore.blocks.allow_research=true; every player needs the mod)")
    end
    if not rwant then s.r = nil end
    if s.bdead and (not rwant or s.rdead) then return end -- everything final: no reads, no writes
    local cls = reg and find_class(s) or nil
    if not valid(cls) then
        if not s.bdead then s.b = "pending" end
        if rwant and not s.rdead then s.r = "pending" end
        if reg and tickN >= 40 and not s.waited then s.waited = true; L("waiting for class " .. s.class) end
        return
    end
    if not s.bdead then
        local okb, rb = pcall(block_step, s, reg, cls)
        s.b = okb and rb or ("failed:" .. tostring(rb))
    end
    if rwant and not s.rdead then
        local okr, rr = pcall(research_step, s, cls)
        s.r = okr and rr or ("failed:" .. tostring(rr))
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

-- starts the one tick handler. false when MSCore.events is missing (install not run).
local function ensure_tick()
    if ticking then return true end
    if not (MS and MS.events and MS.events.on) then return false end
    ticking = true
    MS.events.on("tick", function() pcall(step) end)
    return true
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
    if not ensure_tick() then return nil, "register: MSCore.events missing, tick not started (call install first)" end
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
    MS = MSCore
    MSCore.blocks = M
    return M
end

return M
