-- Plain Lua 5.4 tests for mscore_wbp ship hologram API (no game). Run: lua test_hologram.lua  (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local pass, fail = 0, 0
local function check(name, cond)
    if cond then pass = pass + 1; print("PASS " .. name) else fail = fail + 1; print("FAIL " .. name) end
end

-- ===== fake UE4SS globals
FName = function(s) return s end
FindAllOf = function() return {} end
StaticFindObject = function() return nil end

local M = require("mscore_wbp")

-- (a) invalid image
local r = M.set_ship_hologram(nil, 1)
check("set_ship_hologram(nil) false", r == false)

-- (c) standalone (no install, core nil): fake FindAllOf with ships, missing ship / class / controller soft-fail, no throw
local shipCalls = 0
FindAllOf = function() shipCalls = shipCalls + 1 return { { IsValid = function() return true end, ID = 7 } } end
local okc, tex, why = pcall(M.hologram_texture, 1)
check("standalone missing ship no throw", okc and tex == nil and (why == "no class" or why == "no ship" or why == "no controller" or why == "pending"))
pcall(M.hologram_texture, 1)
check("standalone ship scan rate limited (<= 1 FindAllOf)", shipCalls <= 1)
check("caps ok before disable", M.hologram_caps() == "ok")

-- (b) kill switch
M.hologram_disable("x")
local t2, w2 = M.hologram_texture(1)
check("disabled texture", t2 == nil and w2 == "off:x")
check("disabled caps", M.hologram_caps() == "off:x")

-- pure functions
check("ttl constant", M.HOLO_TTL == 30)
check("age<ttl not due", M.holo_due(29.9, 30, false) == false)
check("age==ttl due", M.holo_due(30, 30, false) == true)
check("never rendered due", M.holo_due(nil, 30, false) == true)
check("force due", M.holo_due(1, 30, true) == true)
check("default ttl", M.holo_due(29, nil, false) == false and M.holo_due(30, nil, false) == true)
local ok1, q = M.holo_take({}, 1, 0, 2)
check("take empty queue", ok1 == true and #q == 0)
ok1, q = M.holo_take({}, 1, 2, 2)
check("budget used -> queued", ok1 == false and #q == 1 and q[1] == 1)
ok1, q = M.holo_take(q, 1, 2, 2)
check("dedupe", ok1 == false and #q == 1)
ok1, q = M.holo_take(q, 2, 2, 2); ok1, q = M.holo_take(q, 3, 2, 2)
check("FIFO order", #q == 3 and q[1] == 1 and q[2] == 2 and q[3] == 3)
ok1, q = M.holo_take(q, 3, 0, 2)
check("newer id waits behind older", ok1 == false and #q == 3)
ok1, q = M.holo_take(q, 2, 0, 2)
check("2nd in line served with budget 2", ok1 == true and #q == 2 and q[1] == 1 and q[2] == 3)
ok1, q = M.holo_take(q, 1, 1, 2)
check("only max per window", ok1 == true and #q == 1)
ok1 = M.holo_take(q, 3, 2, 2)
check("window full", ok1 == false)
local st = M.hologram_stats()
check("stats table", type(st.renders) == "number" and type(st.reuses) == "number" and type(st.created) == "number" and type(st.queued) == "number")

-- M5: names, stale texture, wrapper
check("alias identity", M.hologram_texture == M.ship_hologram and type(M.ship_hologram) == "function")
local valid = true
local ftex = { IsValid = function() return valid end }
local fent = { ShipHologram = { Brush = { ResourceObject = ftex } } }
check("holoTex valid", M._holoTex(fent) == ftex)
valid = false
check("stale texture not returned", M._holoTex(fent) == nil)
local applied = nil
local fimg = { IsValid = function() return true end, SetBrushFromTexture = function(_, t) applied = t end }
local realSH = M.ship_hologram
M.ship_hologram = function() return ftex end
check("wrapper applies", M.set_ship_hologram(fimg, 1) == true and applied == ftex)
M.ship_hologram = function() return nil, "pending" end
local rr, ww = M.set_ship_hologram(fimg, 1)
check("wrapper false,why", rr == false and ww == "pending")
M.ship_hologram = function() return ftex end
fimg.SetBrushFromTexture = function() error("x") end
check("wrapper pcall", M.set_ship_hologram(fimg, 1) == false)
M.ship_hologram = realSH

-- M7: purge
local qa = M.holo_purge({ 1, 2, 3 }, { [1] = true, [3] = true }, { [1] = 0, [3] = 0 }, 5, 10)
check("purge: dead id dropped", #qa == 2 and qa[1] == 1 and qa[2] == 3)
qa = M.holo_purge({ 1, 2 }, { [1] = true, [2] = true }, { [1] = 0, [2] = 9 }, 12, 10)
check("purge: old id dropped", #qa == 1 and qa[1] == 2)
local qb = M.holo_purge({ 5, 6 }, { [6] = true }, {}, 0, 10)
local okb, qb2 = M.holo_take(qb, 6, 1, 2)
check("purged head: full budget for live id", okb == true)

-- M8: decide
check("decide never rendered -> create", M.holo_decide(nil, 30, false, true, false) == "create")
check("decide expired+valid, no refresh_ok -> reuse", M.holo_decide(99, 30, false, false, true) == "reuse")
check("decide expired+valid, refresh_ok -> render", M.holo_decide(99, 30, false, true, true) == "render")
check("decide young+valid, refresh_ok -> reuse", M.holo_decide(5, 30, false, true, true) == "reuse")
check("decide force without refresh_ok ignored", M.holo_decide(5, 30, true, false, true) == "reuse")
check("decide force with refresh_ok -> render", M.holo_decide(5, 30, true, true, true) == "render")
check("decide invalid texture -> render even without refresh_ok", M.holo_decide(5, 30, false, false, false) == "render")
check("stats has stale_reuses", type(M.hologram_stats().stale_reuses) == "number")

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
