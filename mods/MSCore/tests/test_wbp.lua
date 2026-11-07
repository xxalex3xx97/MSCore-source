-- Plain Lua 5.4 tests for mscore_wbp CharacterInputEnabled block (no game). Run: lua test_wbp.lua  (from any cwd)
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

-- ===== fake UE4SS globals (module load needs none of them; kept so require is safe)
FName = function(s) return s end
FindAllOf = function() return {} end
StaticFindObject = function() return nil end

local M = require("mscore_wbp")

-- fake PlayerController: CharacterInputEnabled is a plain bool, AllowCharacterInput sets it
local function mkpc(cie)
    local pc = { CharacterInputEnabled = cie }
    function pc:IsValid() return true end
    function pc:AllowCharacterInput(b) self.CharacterInputEnabled = b end
    return pc
end
-- fake window handle; w.ok = false simulates a destroyed widget (IsValid false)
local function mkh()
    local w = { ok = true }
    function w:IsValid() return self.ok end
    return { wbp = w }
end

local function reset() M._cie.holders = {}; M._cie.prev = nil; M._cie.pc = nil end

-- (1) acquire -> false, release -> true
reset()
local pc1, h1 = mkpc(true), mkh()
M._cie_acquire(h1, pc1)
check("1 acquire blocks", pc1.CharacterInputEnabled == false)
M._cie_release(h1, pc1)
check("1 release restores true", pc1.CharacterInputEnabled == true)

-- (2) prev false stays false after release
reset()
local pc2, h2 = mkpc(false), mkh()
M._cie_acquire(h2, pc2)
check("2 acquire keeps false", pc2.CharacterInputEnabled == false)
M._cie_release(h2, pc2)
check("2 release leaves false (prev was false)", pc2.CharacterInputEnabled == false)

-- (3) double release idempotent
reset()
local pc3, h3 = mkpc(true), mkh()
M._cie_acquire(h3, pc3)
M._cie_release(h3, pc3)
local okRel = pcall(M._cie_release, h3, pc3)
check("3 second release no error", okRel)
check("3 stays true after double release", pc3.CharacterInputEnabled == true)

-- (4) two windows: restore only when the last one closes
reset()
local pc4, hA, hB = mkpc(true), mkh(), mkh()
M._cie_acquire(hA, pc4)
M._cie_acquire(hB, pc4)
M._cie_release(hA, pc4)
check("4 A closed, B open: still blocked", pc4.CharacterInputEnabled == false)
M._cie_release(hB, pc4)
check("4 B closed: restored true", pc4.CharacterInputEnabled == true)

-- (5a) stale holder (window destroyed without close) on same pc: dropped, prev kept (not re-read as the stale false)
reset()
local pc5, hS, hN = mkpc(true), mkh(), mkh()
M._cie_acquire(hS, pc5)
hS.wbp.ok = false
M._cie_acquire(hN, pc5)
check("5a new acquire after stale holder: blocked", pc5.CharacterInputEnabled == false)
M._cie_release(hN, pc5)
check("5a release restores true (stale holder dropped)", pc5.CharacterInputEnabled == true)

-- (5b) stale holder on old pc, new pc: prev re-read from new pc
reset()
local pcOld, pcNew, hO, hP = mkpc(true), mkpc(false), mkh(), mkh()
M._cie_acquire(hO, pcOld)
hO.wbp.ok = false
M._cie_acquire(hP, pcNew)
check("5b new pc: blocked", pcNew.CharacterInputEnabled == false)
M._cie_release(hP, pcNew)
check("5b new pc prev false: stays false", pcNew.CharacterInputEnabled == false)
check("5b old pc untouched", pcOld.CharacterInputEnabled == false)

-- (6) AllowCharacterInput errors: property fallback blocks and restores
reset()
local pc6, h6 = mkpc(true), mkh()
pc6.AllowCharacterInput = function() error("not callable") end
M._cie_acquire(h6, pc6)
check("6 fallback property blocks", pc6.CharacterInputEnabled == false)
M._cie_release(h6, pc6)
check("6 fallback property restores true", pc6.CharacterInputEnabled == true)

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
