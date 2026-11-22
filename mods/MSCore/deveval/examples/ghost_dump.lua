-- Ghost enemy probe: sim EnemyShipObject vs its presentation Actor. Read-only, pcall everywhere.
-- Usage: copy to eval_in\ghost.lua, append `ghost` to eval_in\_go.txt, read eval_out\ghost.txt
IDS = {41312, 189748}   -- empty = first 6 of enemies()
local function n(v) if type(v) == "number" then return v end local x = get(v, "Value") return type(x) == "number" and x or nil end
local function vec(v) if v == nil then return nil end return { n(get(v, "X")), n(get(v, "Y")), n(get(v, "Z")) } end
local function sub(a, b) if a and b and a[1] and b[1] and a[2] and b[2] and a[3] and b[3] then return { a[1]-b[1], a[2]-b[2], a[3]-b[3] } end end
local function len(a) if a and a[1] and a[2] and a[3] then return math.sqrt(a[1]^2 + a[2]^2 + a[3]^2) end end
local function f3(a) if not a then return "?" end return string.format("%.1f,%.1f,%.1f", a[1] or 0/0, a[2] or 0/0, a[3] or 0/0) end
local function fname(o) local ok, s = pcall(function() return o:GetFullName() end) return ok and s or nil end
local function same(a, b) if a == b then return true end local x, y = fname(a), fname(b) return x ~= nil and x == y end
-- sim position in metres: chunk*2048 + loc/65536 (loc via property, else GetLocation())
local function simpos(o)
    local ch = vec(get(o, "ChunkLocation"))
    if not ch then local ok, v = call(o, "GetChunkLocation") ch = ok and vec(v) or nil end
    local lv = get(o, "Location")
    if lv == nil then local ok, v = call(o, "GetLocation") lv = ok and v or nil end
    local lo = vec(lv)
    if not (ch and lo and ch[1] and lo[1]) then return nil, ch, lo end
    return { ch[1]*2048 + lo[1]/65536, ch[2]*2048 + lo[2]/65536, ch[3]*2048 + lo[3]/65536 }, ch, lo
end
local function actorpos(a)   -- cm -> m
    for _, fn in ipairs({ "K2_GetActorLocation", "GetActorLocation" }) do
        local ok, v = call(a, fn)
        if ok and v then local p = vec(v) if p and p[1] then return { p[1]/100, p[2]/100, p[3]/100 }, p end end
    end
end
local function actor_of(o)
    for _, p in ipairs({ "Actor", "ActorRef" }) do local a = get(o, p) if a and obj_ok(a) then return a, p end end
    local ok, a = call(o, "GetActor") if ok and a and obj_ok(a) then return a, "GetActor" end
    for _, a in ipairs(findall("Enemy")) do
        if not (fname(a) or ""):find("Default__", 1, true) then
            local ok2, s = call(a, "GetSimulationObject")
            if ok2 and s and same(s, o) then return a, "Enemy.GetSimulationObject" end
        end
    end
end
local function player_actor()
    for _, pc in ipairs(findall("PlayerController")) do
        local ok, p = call(pc, "GetPawn") if ok and p and obj_ok(p) then return p end
    end
end

local ps = ships()[1]
local po = ps and ps.obj
local psim, pch = nil, nil
if po then psim, pch = simpos(po) end
local pact = player_actor()
local pap = pact and actorpos(pact)
log("player ship", ps and ps.id, ps and ps.name, "sim_m=" .. f3(psim), "chunk=" .. f3(pch), "actor_m=" .. f3(pap), "pawn=" .. tostring(fname(pact)))

local all = enemies()
local ids = {}
for _, i in ipairs(IDS) do ids[#ids + 1] = i end
if #ids == 0 then for i = 1, math.min(6, #all) do ids[#ids + 1] = all[i].id end end
log("enemies total", #all, "ids", table.concat(ids, ","))

local summaries, dumpobj = {}, nil
for _, id in ipairs(ids) do
    log("=========== id " .. tostring(id))
    local o
    for _, e in ipairs(all) do if e.id == id then o = e.obj break end end
    if not o then log("not found in enemies()") goto continue end
    local full = fname(o) or "?"
    if full:find("Default__", 1, true) then log("CDO skipped", full) goto continue end
    local cls = "?"
    pcall(function() cls = o:GetClass():GetFullName() end)
    log("class", cls) log("full", full)
    for _, p in ipairs({ "ID", "Health", "MaxHealth", "CurrentTask", "Spawner", "SpawnerID", "AttackWave", "AttackWaveID", "Target", "TargetID", "TargetLocation", "CachedVelocity", "ChunkLocation", "Location" }) do
        log("  " .. p, get(o, p))
    end
    -- any property with 'Detail' in its name
    local detail = "?"
    pcall(function()
        local c = o:GetClass()
        while c and obj_ok(c) do
            c:ForEachProperty(function(pr)
                local nm = pr:GetFName():ToString()
                if nm:find("Detail", 1, true) then detail = fmt(get(o, nm)) log("  " .. nm, detail) end
            end)
            c = c:GetSuperStruct()
        end
    end)
    local sp, ch, lo = simpos(o)
    log("  sim chunk raw", f3(ch), "loc raw", f3(lo), "sim_m", f3(sp))
    local srel = sub(sp, psim)
    local a, via = actor_of(o)
    local arel, delta, avalid = nil, nil, "none"
    if a then
        avalid = obj_ok(a) and "valid" or "invalid"
        log("  actor via", via, "full", fname(a))
        local am, acm = actorpos(a)
        log("  actor loc cm", f3(acm), "m", f3(am))
        for _, p in ipairs({ "IsHidden", "IsActorTickEnabled" }) do local _, r = call(a, p) log("  " .. p, r) end
        log("  bHidden", get(a, "bHidden"))
        arel = sub(am, pap)
        delta = len(sub(srel, arel))
    end
    log("  sim_rel_m", f3(srel), "actor_rel_m", f3(arel), "delta_m", delta)
    dumpobj = dumpobj or o
    summaries[#summaries + 1] = string.format("GHOST? id=%s class=%s actor=%s sim_rel_m=%s actor_rel_m=%s delta_m=%s detail=%s health=%s",
        tostring(id), cls:match("[^%.]+$") or cls, avalid, f3(srel), f3(arel), delta and string.format("%.1f", delta) or "?", detail, fmt(get(o, "Health")))
    ::continue::
end
log("=========== SUMMARY")
for _, s in ipairs(summaries) do log(s) end
if dumpobj then log("=========== dump(o) first object (reader: only the first ~80 lines matter)") dump(dumpobj) end
