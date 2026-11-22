-- Ghost watch: 20 s, every 500 ms, sim pos vs actor pos vs CachedVelocity for IDS. CSV eval_out\ghost_<id>.csv
-- Usage (result also in eval_out\ghost_summary.csv + eval_out.txt [watch ghostw_result]): copy to eval_in\ghostw.lua, append `ghostw` to eval_in\_go.txt; result lines in eval_out.txt ([watch..] / [timer..]) + csv
IDS = {41312, 189748}   -- empty = first 6 of enemies()
local function n(v) if type(v) == "number" then return v end local x = get(v, "Value") return type(x) == "number" and x or nil end
local function vec(v) if v == nil then return nil end return { n(get(v, "X")), n(get(v, "Y")), n(get(v, "Z")) } end
local function sub(a, b) if a and b and a[1] and b[1] and a[2] and b[2] and a[3] and b[3] then return { a[1]-b[1], a[2]-b[2], a[3]-b[3] } end end
local function len(a) if a and a[1] and a[2] and a[3] then return math.sqrt(a[1]^2 + a[2]^2 + a[3]^2) end end
local function fname(o) local ok, s = pcall(function() return o:GetFullName() end) return ok and s or nil end
local function same(a, b) if a == b then return true end local x, y = fname(a), fname(b) return x ~= nil and x == y end
local function simpos(o)
    local ch = vec(get(o, "ChunkLocation"))
    if not ch then local ok, v = call(o, "GetChunkLocation") ch = ok and vec(v) or nil end
    local lv = get(o, "Location")
    if lv == nil then local ok, v = call(o, "GetLocation") lv = ok and v or nil end
    local lo = vec(lv)
    if not (ch and lo and ch[1] and lo[1]) then return nil end
    return { ch[1]*2048 + lo[1]/65536, ch[2]*2048 + lo[2]/65536, ch[3]*2048 + lo[3]/65536 }
end
local function actorpos(a)
    for _, fn in ipairs({ "K2_GetActorLocation", "GetActorLocation" }) do
        local ok, v = call(a, fn)
        if ok and v then local p = vec(v) if p and p[1] then return { p[1]/100, p[2]/100, p[3]/100 } end end
    end
end
local function actor_of(o)
    for _, p in ipairs({ "Actor", "ActorRef" }) do local a = get(o, p) if a and obj_ok(a) then return a end end
    local ok, a = call(o, "GetActor") if ok and a and obj_ok(a) then return a end
    for _, a in ipairs(findall("Enemy")) do
        if not (fname(a) or ""):find("Default__", 1, true) then
            local ok2, s = call(a, "GetSimulationObject")
            if ok2 and s and same(s, o) then return a end
        end
    end
end
local function player_actor()
    for _, pc in ipairs(findall("PlayerController")) do
        local ok, p = call(pc, "GetPawn") if ok and p and obj_ok(p) then return p end
    end
end

local all = enemies()
local ids = {}
for _, i in ipairs(IDS) do ids[#ids + 1] = i end
if #ids == 0 then for i = 1, math.min(6, #all) do ids[#ids + 1] = all[i].id end end
local objs, acts, st = {}, {}, {}
for _, id in ipairs(ids) do
    for _, e in ipairs(all) do if e.id == id then objs[id] = e.obj break end end
    if objs[id] then acts[id] = actor_of(objs[id]) end
    st[id] = { first = nil, last = nil, afirst = nil, alast = nil, vs = {0, 0, 0}, vn = 0 }
    csv("ghost_" .. id, { "t", "sim_x", "sim_y", "sim_z", "actor_x", "actor_y", "actor_z", "vx", "vy", "vz", "vraw_x", "health", "task" })
end
log("ghost watch ids", table.concat(ids, ","), "objs found", (function() local c = 0 for _ in pairs(objs) do c = c + 1 end return c end)())

local ps = ships()[1]
local pact = player_actor()
local t0, runs, final = nil, 0, nil
csv("ghost_summary", { "result" })
watch("ghostw_result", function() return final end, 500)
timer("ghostw", 500, function()
    runs = runs + 1
    t0 = t0 or os.clock()
    local t = os.clock() - t0
    local po = ps and ps.obj
    local psim = po and simpos(po)
    local pap = pact and actorpos(pact)
    for _, id in ipairs(ids) do
        local o = objs[id]
        if o and obj_ok(o) then
            local s = st[id]
            local sp = simpos(o)
            local srel = sub(sp, psim)
            local ap = acts[id] and obj_ok(acts[id]) and actorpos(acts[id])
            local arel = sub(ap, pap)
            local vv = get(o, "CachedVelocity")
            local vraw = vec(vv)
            local v = vraw and vraw[1] and { vraw[1]/65536*60, vraw[2]/65536*60, vraw[3]/65536*60 }   -- raw is per-frame 16.16
            if srel then s.first = s.first or srel s.last = srel end
            if arel then s.afirst = s.afirst or arel s.alast = arel end
            if v then for i = 1, 3 do s.vs[i] = s.vs[i] + v[i] end s.vn = s.vn + 1 end
            srel, arel, v = srel or {}, arel or {}, v or {}
            csv_row("ghost_" .. id, string.format("%.1f", t), srel[1], srel[2], srel[3], arel[1], arel[2], arel[3], v[1], v[2], v[3], vraw and vraw[1], get(o, "Health"), get(o, "CurrentTask"))
        end
    end
    if runs >= 40 or t >= 20 then
        stop("ghostw")
        for _, id in ipairs(ids) do
            local s = st[id]
            local moved = len(sub(s.last, s.first))
            local amoved = len(sub(s.alast, s.afirst))
            local vm = s.vn > 0 and len({ s.vs[1]/s.vn, s.vs[2]/s.vn, s.vs[3]/s.vn }) or nil
            local ok = moved and vm and math.abs(moved - vm * 20) <= 0.3 * vm * 20
            local line = string.format("WATCH id=%s moved_m=%s actor_moved_m=%s v_mean=%s consistent=%s", tostring(id),
                moved and string.format("%.1f", moved) or "?", amoved and string.format("%.1f", amoved) or "?", vm and string.format("%.1f", vm) or "?", ok and "yes" or "no")
            final = (final and final .. " || " or "") .. line
            csv_row("ghost_summary", line)
        end
    end
end, 40)
