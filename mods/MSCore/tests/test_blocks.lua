-- Plain Lua 5.4 tests for mscore_blocks (no game). Run: lua test_blocks.lua  (from any cwd)
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

-- ===== fake UE4SS globals
local OBJS = {}          -- StaticFindObject paths
local REG                -- FindFirstOf("BuildingBlockRegistry")
local RT_LIST = {}       -- FindAllOf("ResearchTechnology")
local tickfn             -- captured MSCore.events.on("tick", fn)

FName = function(s) return s end
FindFirstOf = function(n) if n == "BuildingBlockRegistry" then return REG end return nil end
FindAllOf = function(n) if n == "ResearchTechnology" then return RT_LIST end return {} end
StaticFindObject = function(p) return OBJS[p] end
MSCore = { events = { on = function(name, fn) assert(name == "tick") tickfn = fn return 1 end } }

local function obj(full, extra)
    local o = { IsValid = function() return true end, GetFullName = function() return full end }
    for k, v in pairs(extra or {}) do o[k] = v end
    return o
end
local function ftext(s) return { ToString = function() return s end } end
local function categories(list)
    local elems = {}
    for i, c in ipairs(list) do elems[i] = { get = function() return c end } end
    return { ForEach = function(_, f) for i, e in ipairs(elems) do if f(i, e) then break end end end }
end

local BPU_PATH = "/Game/Blueprints/BPO_BlueprintUnit.BPO_BlueprintUnit_C"
local VAN_PATH = "/Game/Blueprints/BPO_Vanilla.BPO_Vanilla_C"
local RSC_PATH = "/Game/Data/Research/RSC_ComputationScience.RSC_ComputationScience"
local OTH_PATH = "/Game/Data/Research/RSC_Other.RSC_Other"
local BPU = obj("BlueprintGeneratedClass " .. BPU_PATH)
local VAN = obj("BlueprintGeneratedClass " .. VAN_PATH)
local OTHER_BLOCK = obj("BlueprintGeneratedClass /Game/Blueprints/BPO_Other.BPO_Other_C")

local cat, RSC, OTH
local function world(catName)
    cat = { Name = ftext(catName or "Functional Blocks"), Classes = { VAN } }
    REG = obj("BuildingBlockRegistry /Script/Game.BuildingBlockRegistry", { Categories = categories({ cat }) })
    OBJS = { [BPU_PATH] = BPU, [VAN_PATH] = VAN }
    RSC = obj("ResearchTechnology " .. RSC_PATH, { Unlocks = {} })
    OTH = obj("ResearchTechnology " .. OTH_PATH, { Unlocks = { { UnlockType = 7, bHidden = false, Block = OTHER_BLOCK } } })
    RT_LIST = { OTH }
    OBJS[RSC_PATH] = RSC
    OBJS[OTH_PATH] = OTH
end

-- fresh module state per scenario (package.loaded reset), tick captured through the fake events
local function fresh()
    package.loaded["mscore_blocks"] = nil
    tickfn = nil
    local B = require("mscore_blocks")
    B.install(MSCore)
    return B
end
local function tick(n) for _ = 1, n do tickfn() end end
local function startsWith(s, p) return type(s) == "string" and s:sub(1, #p) == p end

-- ===== 1. install and empty status
world()
local B = fresh()
check("install sets MSCore.blocks and starts tick", MSCore.blocks == B and tickfn ~= nil)
check("status empty before register", next(B.status()) == nil)

-- ===== 2. register validation
check("register: nil opts refused", select(1, B.register(nil)) == nil)
check("register: bad class refused", B.register({ class = "BPO_X_C" }) == nil)
check("register: class without _C refused", B.register({ class = "/Game/X/BPO_X" }) == nil)
check("register: research number refused", B.register({ class = BPU_PATH, research = 5 }) == nil)
check("register: category number refused", B.register({ class = BPU_PATH, category = 5 }) == nil)

-- ===== 3. register then tick appends once; second tick does not duplicate
world()
B = fresh()
check("register ok", B.register({ class = BPU_PATH }) == true)
tick(1)
check("tick appends block once", #cat.Classes == 2 and cat.Classes[2] == BPU)
check("status block after append", B.status()[BPU_PATH] == "block")
tick(40)
check("re-check does not duplicate", #cat.Classes == 2)
check("duplicate register ignored (true)", B.register({ class = BPU_PATH, research = "RSC_ComputationScience" }) == true)
tick(1)
check("duplicate register: no research append", #RSC.Unlocks == 0 and #cat.Classes == 2)

-- ===== 4. invalid class (not loaded) leaves pending
world()
B = fresh()
B.register({ class = "/Game/Blueprints/BPO_Missing.BPO_Missing_C" })
tick(45)
check("missing class stays pending", B.status()["/Game/Blueprints/BPO_Missing.BPO_Missing_C"] == "pending")
check("missing class: vanilla untouched", #cat.Classes == 1)

-- ===== 5. missing category gives failed
world()
B = fresh()
B.register({ class = BPU_PATH, category = "Nope" })
tick(1)
check("missing category -> failed", B.status()[BPU_PATH] == "failed:no category")
check("missing category: nothing appended", #cat.Classes == 1)

-- ===== 6. research append (fake enum copied from an existing block unlock)
world()
B = fresh()
B.register({ class = BPU_PATH, research = "RSC_ComputationScience" })
tick(1)
check("research: block appended", #cat.Classes == 2)
check("research: one unlock appended", #RSC.Unlocks == 1)
check("research: UnlockType copied (7)", RSC.Unlocks[1].UnlockType == 7)
check("research: Block set, bHidden false", RSC.Unlocks[1].Block == BPU and RSC.Unlocks[1].bHidden == false)
check("status block+research", B.status()[BPU_PATH] == "block+research")
tick(40)
check("research: re-check does not duplicate", #RSC.Unlocks == 1 and #cat.Classes == 2)

-- full path accepted for research
world()
B = fresh()
B.register({ class = BPU_PATH, research = RSC_PATH })
tick(1)
check("research full path works", #RSC.Unlocks == 1 and B.status()[BPU_PATH] == "block+research")

-- ===== 7. research fails (no enum source): block still appended
world()
RT_LIST = {}
B = fresh()
B.register({ class = BPU_PATH, research = "RSC_ComputationScience" })
tick(1)
check("research fail: block still appended", #cat.Classes == 2 and cat.Classes[2] == BPU)
check("research fail: no unlock appended", #RSC.Unlocks == 0)
check("research fail: status failed:research", startsWith(B.status()[BPU_PATH], "failed:research"))

-- ===== 8. research object not loaded: pending for research, block done
world()
OBJS[RSC_PATH] = nil
B = fresh()
B.register({ class = BPU_PATH, research = "RSC_ComputationScience" })
tick(1)
check("research not loaded: block appended", #cat.Classes == 2)
check("research not loaded: status block (not failed)", B.status()[BPU_PATH] == "block")

-- ===== 9. append refused by the game: failed:append
world()
local refuse = setmetatable({}, { __index = cat.Classes, __newindex = function() end, __len = function() return 1 end })
cat.Classes = refuse
B = fresh()
B.register({ class = BPU_PATH })
tick(1)
check("append refused -> failed:append", startsWith(B.status()[BPU_PATH], "failed:append"))

-- ===== 10. asset reloaded (block removed): 5 s re-check appends again
world()
B = fresh()
B.register({ class = BPU_PATH })
tick(1)
cat.Classes = { VAN }
tick(18)
check("reload: no append before 20th tick", #cat.Classes == 1)
tick(1)
check("reload: re-appended on 20th tick", #cat.Classes == 2 and cat.Classes[2] == BPU)

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
