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

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
