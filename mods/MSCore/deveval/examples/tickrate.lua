-- tickrate: read-only. Sim ticks/s + bullet m/s from wall clock. Run while cannons FIRE continuously (45 s).
-- Trigger: copy to eval_in\tickrate.lua, append `tickrate` to eval_in\_go.txt.
-- Output: UE4SS.log "PROBE tickrate ...", eval_out\tickrate.csv, eval_out.txt [watch tickrate_result].
local ASSUMED, MIN_DT, MAX_DT, KEEP, MAXB, RUN_S, MIN_FR = 60, 0.5, 1.4, 8, 150, 45, 10
local P = _G.print
local function sget(o, k)
    if o == nil then return nil end
    local ok, v = pcall(function() return o[k] end)
    if not ok then return nil end
    if type(v) == "userdata" then
        local ok2, g = pcall(function() return v:get() end)
        if ok2 and g ~= nil then return g end
    end
    return v
end
local function num(v) if type(v) == "number" then return v end local x = sget(v, "Value") return type(x) == "number" and x or nil end
local function vec3(v)
    if v == nil then return nil end
    local x, y, z = num(sget(v, "X")), num(sget(v, "Y")), num(sget(v, "Z"))
    if x and y and z then return { x, y, z } end
end
local function dist(a, b) return math.sqrt((a[1]-b[1])^2 + (a[2]-b[2])^2 + (a[3]-b[3])^2) end
local function median(t)
    if #t == 0 then return nil end
    local s = {} for i, x in ipairs(t) do s[i] = x end
    table.sort(s)
    return s[math.ceil(#s / 2)]
end
local function f2(x) return x and string.format("%.2f", x) or "nil" end

local function mgrs()
    local r, seen = {}, {}
    for _, c in ipairs({ "BulletManager", "BP_BulletManager_C" }) do
        for _, m in ipairs(findall(c)) do
            local ok, nm = pcall(function() return m:GetFullName() end)
            if ok and nm and not nm:find("Default__", 1, true) and not seen[nm] then seen[nm] = true r[#r + 1] = m end
        end
    end
    return r
end
local function readBullets()
    local out, nm = {}, 0
    for _, m in ipairs(mgrs()) do
        nm = nm + 1
        local ok, arr = pcall(function() return m.Bullets end)
        if ok and arr then
            pcall(function()
                arr:ForEach(function(_, el)
                    if #out >= MAXB then return end
                    local b = el:get()
                    local lt = num(sget(b, "RemainingLifetime"))
                    local lo, ch, ve = vec3(sget(b, "Location")), vec3(sget(b, "ChunkLocation")), vec3(sget(b, "Velocity"))
                    if lt and lo and ch and ve then
                        out[#out + 1] = { id = sget(b, "InstigatorID"), lt = lt,
                            v = { ve[1]/65536, ve[2]/65536, ve[3]/65536 },
                            p = { ch[1]*2048 + lo[1]/65536, ch[2]*2048 + lo[2]/65536, ch[3]*2048 + lo[3]/65536 } }
                    end
                end)
            end)
        end
    end
    return out, nm
end
-- A older sample, B newer. Returns median dLT (frames), median displacement (m), median m/frame, matched count.
local function match(A, B)
    local used, dl, ds, mf = {}, {}, {}, {}
    for _, a in ipairs(A) do
        local best, bj, bd
        local sp = math.sqrt(a.v[1]^2 + a.v[2]^2 + a.v[3]^2)
        for j, b in ipairs(B) do
            local d = a.lt - b.lt
            if not used[j] and a.id == b.id and d >= MIN_FR and d <= 300 then
                local e = { a.p[1] + a.v[1]*d, a.p[2] + a.v[2]*d, a.p[3] + a.v[3]*d }
                local err = dist(e, b.p)
                if err <= 0.02 * sp * d + 0.1 and (not best or err < best) then best, bj, bd = err, j, d end
            end
        end
        if bj then
            used[bj] = true
            local dd = dist(a.p, B[bj].p)
            dl[#dl + 1] = bd ds[#ds + 1] = dd mf[#mf + 1] = dd / bd
        end
    end
    if #dl == 0 then return nil end
    return median(dl), median(ds), median(mf), #dl
end

-- cannon LastShotTime tracker (fallback / cross-check)
local can = {}
local function sampleCannons(t)
    for _, cls in ipairs({ "BP_BasicCannonObject_B_C", "BP_BasicCannonObject_C", "CannonObject" }) do
        for _, c in ipairs(findall(cls)) do
            local id, ls = sget(c, "ID"), num(sget(c, "LastShotTime"))
            if id and ls then
                local s = can[id]
                if not s then can[id] = { ls = ls, n = 0, tbs = num(sget(c, "TimeBetweenShots")) }
                elseif ls ~= s.ls then
                    s.first = s.first or { t = t, ls = ls }
                    s.last = { t = t, ls = ls }
                    s.n = s.n + 1
                    s.ls = ls
                end
            end
        end
    end
end
local function cannonRate()
    local r = {}
    for _, s in pairs(can) do
        if s.first and s.n >= 3 and s.last.t - s.first.t >= 8 then r[#r + 1] = (s.last.ls - s.first.ls) / (s.last.t - s.first.t) end
    end
    return median(r), #r
end

local hist, rates, mps, mpf = {}, {}, {}, {}
local sumF, sumT, seenMax, nmg, sane, shown = 0, 0, 0, 0, false, nil
local T0 = os.clock()
csv("tickrate", { "t", "dt", "dLT", "tps", "disp_m", "mps", "m_per_frame", "n" })
local function summary(tag)
    local rb = sumT > 0 and sumF / sumT or nil
    local cr, cn = cannonRate()
    local line = string.format("PROBE tickrate %s pairs=%d tps_ratio=%s tps_median=%s factor_vs_%d=%s bullet_mps=%s m_per_frame=%s cannon_tps=%s(cannons=%d) mgr=%d bullets_max=%d",
        tag, #rates, f2(rb), f2(median(rates)), ASSUMED, rb and f2(rb / ASSUMED) or "nil", f2(median(mps)), f2(median(mpf)), f2(cr), cn, nmg, seenMax)
    P(line)
    shown = line
    return line
end
watch("tickrate_result", function() return shown end, 1000)
local lastP = T0
timer("tickrate", 250, function()
    local t = os.clock()
    local b, nm = readBullets()
    nmg = math.max(nmg, nm)
    seenMax = math.max(seenMax, #b)
    if not sane and #b > 0 then
        sane = true
        local x = b[1]
        P(string.format("PROBE tickrate sanity lt=%s |v|_m_per_frame=%.3f", tostring(x.lt), math.sqrt(x.v[1]^2 + x.v[2]^2 + x.v[3]^2)))
    end
    pcall(sampleCannons, t)
    if #b > 0 then
        for _, s in ipairs(hist) do
            local dt = t - s.t
            if dt >= MIN_DT and dt <= MAX_DT then
                local dLT, disp, mfm, n = match(s.b, b)
                if dLT then
                    rates[#rates + 1] = dLT / dt mps[#mps + 1] = disp / dt mpf[#mpf + 1] = mfm
                    sumF = sumF + dLT sumT = sumT + dt
                    csv_row("tickrate", string.format("%.2f", t - T0), string.format("%.3f", dt), dLT, string.format("%.2f", dLT / dt), string.format("%.2f", disp), string.format("%.2f", disp / dt), string.format("%.3f", mfm), n)
                end
            end
        end
        hist[#hist + 1] = { t = t, b = b }
        if #hist > KEEP then table.remove(hist, 1) end
    end
    if t - lastP >= 5 then lastP = t summary("PROGRESS") end
    if t - T0 >= RUN_S or #rates >= 150 then
        stop("tickrate")
        local line = summary("FINAL")
        if #rates == 0 then
            P("PROBE tickrate NOMATCH bullets_seen_max=" .. seenMax .. " managers=" .. nmg .. " (0 managers: BulletManager not reachable; 0 bullets: not firing; else velocity unit assumption wrong, see sanity line)")
        end
        csv("tickrate_summary", { "result" }) csv_row("tickrate_summary", line)
    end
end, 200)
