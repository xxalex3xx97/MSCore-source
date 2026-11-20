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

-- (c) installed with an empty core: no throw, soft failure
local core = { ships = { list = function() return {} end }, events = { on = function() end } }
M.install(core)
local okc, tex, why = pcall(M.hologram_texture, 1)
check("missing ship no throw", okc and tex == nil and (why == "no class" or why == "no ship" or why == "no controller"))
check("caps ok before disable", M.hologram_caps() == "ok")

-- (b) kill switch
M.hologram_disable("x")
local t2, w2 = M.hologram_texture(1)
check("disabled texture", t2 == nil and w2 == "off:x")
check("disabled caps", M.hologram_caps() == "off:x")

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
