-- Plain Lua 5.4 tests for the blocks contract (names from mscore_contract, caps, kill switch). No game. Run: lua test_blocks_contract.lua (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local pass, fail = 0, 0
local function check(name, cond)
    if cond then
        pass = pass + 1
        print("PASS " .. name)
    else
        fail = fail + 1
        print("FAIL " .. name)
    end
end

-- ===== fake UE4SS globals: nothing resolves
FName = function(s) return s end
FindFirstOf = function() return nil end
FindAllOf = function() return {} end
StaticFindObject = function() return nil end

-- contract names
local C = require("mscore_contract")
for _, k in ipairs({ "asteroid", "asteroid_actor", "floating_ore", "station", "player_station", "container", "container_actor", "item_crate", "ship" }) do
    check("classes." .. k .. " non-empty string", type(C.classes[k]) == "string" and C.classes[k] ~= "")
end
check("blocks.research_dir starts /Game/", type(C.blocks.research_dir) == "string" and C.blocks.research_dir:sub(1, 6) == "/Game/")

-- blocks module: caps, kill switch, refusal after kill
package.loaded.mscore_blocks = nil
local B = require("mscore_blocks")
B.install({ events = { on = function() end } })
check("caps ok after install", B.caps() == "ok")
local allok = B.selftest()
check("selftest false without registry", allok == false)
B.disable("x")
check("caps off after disable", B.caps() == "off:x")
local w, why = B.register({ class = "/Game/A/B.B_C" })
check("register refused when off", w == nil and why == "off:x")

print(("RESULT %d pass, %d fail"):format(pass, fail))
os.exit(fail == 0 and 0 or 1)
