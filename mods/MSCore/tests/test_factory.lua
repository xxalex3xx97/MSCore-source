-- Plain Lua 5.4 tests for mscore_factory and mscore_inv (no game). Run: lua test_factory.lua  (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local F = require("mscore_factory")
local I = require("mscore_inv")

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
local function hex(s) return F.to_hex(s) end

-- ===== config bytes (examples from rea-machine-config.md)
local REFINERY = "01 00 0f 00 00 00 74 69 74 61 6e 69 75 6d 70 6c 61 74 65 73 00 04 00 00 00 01 01 01 01"
local ASSEMBLER = "01 00 0b 00 00 00 68 75 6c 6c 70 6c 61 74 65 73 00 08 00 00 00 01 01 01 01 01 01 01 01"
check("encode refinery example", hex(F.encode_cfg({ recipe = "titaniumplates", ports = { true, true, true, true } })) == REFINERY)
check("encode assembler example", hex(F.encode_cfg({ recipe = "hullplates", ports = { 1, 1, 1, 1, 1, 1, 1, 1 } })) == ASSEMBLER)
check("encode disabled port = 00", hex(F.encode_cfg({ recipe = "", ports = { false, false, false, true } })) ==
    "01 00 00 00 00 00 04 00 00 00 00 00 00 01")
local dec = F.decode_cfg(F.cfg_hex(REFINERY))
check("decode refinery example", dec and dec.recipe == "titaniumplates" and #dec.ports == 4 and dec.ports[4] == true)
local rt = F.decode_cfg(F.encode_cfg({ recipe = "hullplates", ports = { true, false, true } }))
check("round trip recipe + ports", rt and rt.recipe == "hullplates" and rt.ports[1] == true and rt.ports[2] == false and rt.ports[3] == true and #rt.ports == 3)
check("decode rejects trailing byte", F.decode_cfg(F.cfg_hex(REFINERY) .. "\0") == nil)
check("decode rejects version 2", F.decode_cfg(F.cfg_hex("02 00 00 00 00 00 00 00 00 00")) == nil)
check("decode rejects utf-16 fstring", F.decode_cfg(F.cfg_hex("01 00 fe ff ff ff 00 00 00 00")) == nil)

local SPLIT = "01 00 03 00 00 00 " .. string.rep("00 ", 12) .. "ff ff ff ff ff ff ff ff 03 00 00 00 01 01 01"
check("encode splitter example", hex(F.encode_splitter({ filters = { "", "", "" }, ports = { true, true, true } })) == SPLIT)

check("encode_ug 3 = 01 00 + i32 3", hex(F.encode_ug(3)) == "01 00 03 00 00 00")
check("encode_ug Mk2 max 10 ok", F.encode_ug(10, 2) ~= nil)
check("encode_ug Mk2 11 refused", F.encode_ug(11, 2) == nil)
check("encode_ug Mk1 9 refused", F.encode_ug(9, 1) == nil)
check("encode_ug 0 refused", F.encode_ug(0) == nil)

check("cfg_hex / to_hex round trip", hex(F.cfg_hex("0a ff 00")) == "0a ff 00")
check("cfg_hex rejects non-hex", F.cfg_hex("0g") == nil and F.cfg_hex("abc") == nil)

-- ===== footprints
local w, h = F.footprint("DAT_BeltBalancer_01", 0)
check("footprint rot 0", w == 1 and h == 2)
w, h = F.footprint("DAT_BeltBalancer_01", 1)
check("footprint rot 1 swaps", w == 2 and h == 1)
w, h = F.footprint("DAT_SignWall", 3)
check("footprint SignWall rot 3 swaps", w == 6 and h == 2)
check("footprint unknown dat", F.footprint("DAT_Nope", 0) == nil)

-- ===== floors
local st1 = { { class = "BPO_Station_01", gx = 0, gy = 0, h = 0, rot = 0 } }
local fr = F.floor_rects(st1)
check("floor 1x1 at 0,0 half 14", #fr == 1 and fr[1].x == 0 and fr[1].y == 0 and fr[1].w == 14 and fr[1].h == 14 and fr[1].layer == 0)
local fr2 = F.floor_rects({ { class = "BPO_Station_2x1", gx = 2, gy = 1, h = 8, rot = 1 } })
check("floor 2x1 rot 1 swaps, grid*8, layer 1", fr2[1].x == 16 and fr2[1].y == 8 and fr2[1].w == 30 and fr2[1].h == 14 and fr2[1].layer == 1)
check("nofloor block gives no rect", #F.floor_rects({ { class = "BPO_Dock", gx = 0, gy = 0, h = 0 } }) == 0)

-- ===== validate_place (floor = centre 0,0 half 14 at layer 0)
local FL = fr
local function cont(x, y, hh) return { dat = "DAT_Container", x = x, y = y, h = hh or 0, rot = 0 } end
local ok, why = F.validate_place(FL, {}, cont(0, 0))
check("container inside floor", ok == true)
ok, why = F.validate_place(FL, {}, cont(12, 0))
check("container touching floor edge ok", ok == true)
ok, why = F.validate_place(FL, {}, cont(14, 0))
check("container outside floor refused", ok == false and why:find("outside", 1, true) ~= nil)
ok, why = F.validate_place(FL, {}, cont(1, 0))
check("parity x wrong refused", ok == false and why:find("parity", 1, true) ~= nil)
ok, why = F.validate_place(FL, {}, cont(0, 1))
check("parity y wrong refused", ok == false)
ok, why = F.validate_place(FL, {}, cont(0, 0, 8))
check("no floor on layer 1 refused", ok == false)
ok, why = F.validate_place(FL, { cont(0, 0) }, cont(2, 0))
check("overlap refused", ok == false and why:find("overlap", 1, true) ~= nil)
ok = F.validate_place(FL, { cont(0, 0) }, cont(4, 0))
check("edge-touch is not overlap", ok == true)
ok = F.validate_place(FL, { cont(0, 0, 8) }, cont(0, 0, 0))
check("overlap only same layer", ok == true)
ok, why = F.validate_place(FL, {}, { dat = "DAT_Nope", x = 0, y = 0, h = 0, rot = 0 })
check("unknown dat refused", ok == false)
ok = F.validate_place(FL, {}, { dat = "DAT_SignWall", x = 0, y = 20, h = 0, rot = 0 })
check("SignWall exempt from floor", ok == true)
ok = F.validate_place(FL, {}, cont(0, 20))
check("container off floor refused (control)", ok == false)

-- ===== inventory pure helpers (probe selftest semantics)
local p, rem = I.stage_moves({ { amount = 5 } }, 3)
check("stage: stack 5, shortfall 3 -> one move", #p == 1 and p[1] == 1 and rem == 0)
p, rem = I.stage_moves({ { amount = 5 }, { amount = 5 }, { amount = 5 } }, 8)
check("stage: shortfall 8 over [5,5,5] -> two moves", #p == 2 and p[1] == 1 and p[2] == 2 and rem == 0)
p, rem = I.stage_moves({ { amount = 0 }, { amount = 5 } }, 3)
check("stage: skips empty slot", #p == 1 and p[1] == 2)
p, rem = I.stage_moves({ { amount = 2 } }, 5)
check("stage: short remaining reported", #p == 1 and rem == 3)

local slots = { { item = "a", amount = 3 }, { item = "b", amount = 2 }, { item = "a", amount = 4 } }
local picks, short = I.moveout_slots(slots, { { item = "a", amount = 5 } })
check("moveout: a 5 -> slots 1,3", #picks == 2 and picks[1] == 1 and picks[2] == 3 and #short == 0)
picks, short = I.moveout_slots(slots, { { item = "a", amount = 9 } })
check("moveout: a 9 -> short 2", #picks == 2 and #short == 1 and short[1].item == "a" and short[1].amount == 2)
picks = I.moveout_slots(slots, { { item = "a", amount = 3 }, { item = "a", amount = 3 } })
check("moveout: a slot is picked once", #picks == 2 and picks[1] == 1 and picks[2] == 3)

check("room: stack a 3/5 + empty + b", I.room_from_slots({ { item = "a", amount = 3 }, { amount = 0 }, { item = "b", amount = 2 } }, "a", 5) == 7)

-- ===== parse_station (native read_station text)
local sb, sm = F.parse_station("B\tBPO_Station_01\t0\t0\t0\t0\nM\t10\tBP_Belt\t1\t2\t3\t1\t010203\nM\t11\tBP_X\t4\t5\t0\t0\t\nM\t12\tBP_Y\t0\t0\t0\t0\t-\n")
check("parse_station: one block", #sb == 1 and sb[1].class == "BPO_Station_01")
check("parse_station: three machines", #sm == 3)
check("parse_station: machine 1 fields", sm[1].id == 10 and sm[1].x == 1 and sm[1].y == 2 and sm[1].h == 3 and sm[1].rot == 1)
check("parse_station: machine 1 cfg bytes", sm[1].cfg == "\1\2\3")
check("parse_station: machine 2 empty cfg", sm[2].cfg == "")
check("parse_station: machine 3 cfg nil", sm[3].cfg == nil)
local eb, em = F.parse_station("")
check("parse_station: empty text gives two empty tables", #eb == 0 and #em == 0)
-- 0.8.0: 7th column = class path (6-column lines still parse, path nil)
local sb7 = F.parse_station("B\tBPO_Station_01\t0\t0\t0\t0\t/Game/BPO_Station_01.BPO_Station_01_C\nB\tBPO_Old\t1\t1\t0\t2\n")
check("parse_station: 7th column is the class path", sb7[1].path == "/Game/BPO_Station_01.BPO_Station_01_C")
check("parse_station: 6-column block has no path", #sb7 == 2 and sb7[2].path == nil and sb7[2].rot == 2)

-- ===== parse_block_class (0.8.0 native read_block_class text)
local bi = F.parse_block_class("K\t1\t0\t2\t-524088\t-524088\t-262144\t524188\t524088\t655360\nI\tDAT_Iron\t5\nI\tDAT_Gear\t2\n")
check("parse_block_class: flags and machine count", bi.station == true and bi.ship == false and bi.machines == 2)
check("parse_block_class: bounds min and max", bi.min.x == -524088 and bi.min.z == -262144 and bi.max.z == 655360)
check("parse_block_class: cost list", #bi.cost == 2 and bi.cost[1].item == "DAT_Iron" and bi.cost[2].amount == 2)
local bn, bnerr = F.parse_block_class("")
check("parse_block_class: empty text gives nil, err", bn == nil and bnerr ~= nil)

-- ===== parse_inventory (native read_inventory text)
local inv = F.parse_inventory("S\tHullPlate\t482\t100\nS\t\t0\t1\nS\tMagneticCoils\t50\t20\n")
check("parse_inventory: three slots", #inv == 3)
check("parse_inventory: slot 1 fields", inv[1].item == "HullPlate" and inv[1].amount == 482 and inv[1].max == 100)
check("parse_inventory: empty slot item is empty string", inv[2].item == "" and inv[2].amount == 0 and inv[2].max == 1)
check("parse_inventory: slot 3 fields", inv[3].item == "MagneticCoils" and inv[3].amount == 50 and inv[3].max == 20)
check("parse_inventory: empty text gives no slots", #F.parse_inventory("") == 0)

-- ===== Main_Base (2).json floors (2026-11-08)
do
    local blocks = {
        { class = "BPO_Station_02A", gx = 2, gy = 42, h = 0, rot = 0 },
        { class = "BPO_Station_02A", gx = 2, gy = -42, h = 0, rot = 0 },
        { class = "BPO_Station_02A", gx = 14, gy = 10, h = 4, rot = 0 },
        { class = "BPO_Station_02A", gx = -2, gy = 10, h = 4, rot = 0 },
        { class = "BPO_Dock_2", gx = 2, gy = 50, h = 0, rot = 1 },
        { class = "BPO_Dock_2", gx = 2, gy = -50, h = 0, rot = 3 },
        { class = "BPO_ElevatorBlock_03", gx = 16, gy = 16, h = 0, rot = 0 },
        { class = "BPO_ElevatorBlock_03", gx = -4, gy = 16, h = 0, rot = 0 },
    }
    local fl = F.floor_rects(blocks)
    local function at(dat, x, y, h) return F.validate_place(fl, {}, { dat = dat, x = x, y = y, h = h or 0, rot = 0 }) end
    local cpos = { {8, 386}, {24, 386}, {8, -386}, {24, -386}, {8, 376}, {24, 376} }
    for _, p in ipairs(cpos) do
        check(string.format("MB2 container (%d,%d) ok", p[1], p[2]), at("DAT_Container_2", p[1], p[2]) == true)
    end
    local bpos = { {5, 367}, {5, 369} }
    for y = -367, -383, -2 do bpos[#bpos + 1] = { 37, y } end
    for _, x in ipairs({ 29, 31, 33, 35 }) do bpos[#bpos + 1] = { x, -383 } end
    for _, y in ipairs({ 111, 113, 115 }) do
        bpos[#bpos + 1] = { 123, y }
        for _, x in ipairs({ -27, -29, -31, -33, -35, -37 }) do bpos[#bpos + 1] = { x, y } end
    end
    for _, p in ipairs(bpos) do
        check(string.format("MB2 belt (%d,%d) ok", p[1], p[2]), at("DAT_ConveyorBelt", p[1], p[2]) == true)
    end
    local ok, why = at("DAT_Container_2", 200, 200)
    check("MB2 container (200,200) refused outside every floor", ok == false and why:find("outside every floor", 1, true) ~= nil)
    -- single Station_01 (floor +-14): belt at x=13 (footprint 12..14) inside, x=15 (14..16) outside
    local one = F.floor_rects({ { class = "BPO_Station_01", gx = 0, gy = 0, h = 0, rot = 0 } })
    check("MB2 single block: belt (13,1) inside ok", F.validate_place(one, {}, { dat = "DAT_ConveyorBelt", x = 13, y = 1, h = 0, rot = 0 }) == true)
    check("MB2 single block: belt (15,1) outside refused", F.validate_place(one, {}, { dat = "DAT_ConveyorBelt", x = 15, y = 1, h = 0, rot = 0 }) == false)
    check("MB2 belt at h=8 over layer-0 floors refused", at("DAT_ConveyorBelt", 5, 367, 8) == false)
    -- seam: two Station_02 at gx 0 and 8 (64 pitch, floors +-30 -> gap 30..34); container at x=32 straddles it
    local seam = F.floor_rects({
        { class = "BPO_Station_02", gx = 0, gy = 0, h = 0, rot = 0 },
        { class = "BPO_Station_02", gx = 8, gy = 0, h = 0, rot = 0 },
    })
    check("MB2 seam: container (32,0) straddling touching blocks ok", F.validate_place(seam, {}, { dat = "DAT_Container_2", x = 32, y = 0, h = 0, rot = 0 }) == true)
end

-- ===== install hooks
local MS = {}
F.install(MS)
I.install(MS)
check("install sets MSCore.factory and MSCore.inv", MS.factory == F and MS.inv == I)

-- ===== factory_native.inv_split (0.7.6): mscore.lua loaded with UE4SS globals stubbed (no game)
do
    setmetatable(_G, { __index = function(_, k)
        if type(k) == "string" and not k:match("^MSCore") then return function() end end
        return nil
    end })
    -- _G metatable is restored on every path (pass or error in the section)
    local sec_ok, sec_err = pcall(function()
        _G.MSCoreNative_version = function() return "stub" end
        -- mscore.lua copies the natives when it loads, so each case loads it again
        local MSC = dofile(dir .. "/../Scripts/mscore.lua")
        local ok1, why1 = MSC.factory_native.inv_split(1, 0, 1, 2)
        check("inv_split: missing native gives nil, 'missing' text", ok1 == nil and tostring(why1):find("missing", 1, true) ~= nil)
        _G.MSCoreNative_inv_split = function(id, s, n, d) return true, nil end
        local MSC2 = dofile(dir .. "/../Scripts/mscore.lua")
        local ok2, why2 = MSC2.factory_native.inv_split(1, 0, 1, 2)
        check("inv_split: present native is called", ok2 == true and why2 == nil)
    end)
    _G.MSCoreNative_version = nil
    _G.MSCoreNative_inv_split = nil
    setmetatable(_G, nil)
    if not sec_ok then check("inv_split section ran without error: " .. tostring(sec_err), false) end
end

-- ===== factory_native add_block / pending / read_block_class (0.8.0): fake natives, same stub setup as inv_split
do
    setmetatable(_G, { __index = function(_, k)
        if type(k) == "string" and not k:match("^MSCore") then return function() end end
        return nil
    end })
    local sec_ok, sec_err = pcall(function()
        _G.MSCoreNative_version = function() return "stub" end
        local MSC = dofile(dir .. "/../Scripts/mscore.lua")
        local a1, w1 = MSC.factory_native.add_block(1, "/Game/X.X_C", 0, 0, 0, 0)
        check("add_block: missing native gives nil, 'missing' text", a1 == nil and tostring(w1):find("missing", 1, true) ~= nil)
        _G.MSCoreNative_add_block = function() return false, "refused" end
        _G.MSCoreNative_pending = function() return true end
        _G.MSCoreNative_read_block_class = function() return true, "K\t1\t0\t0\t0\t0\t0\t0\t0\t0\n" end
        local MSC2 = dofile(dir .. "/../Scripts/mscore.lua")
        local a2, w2 = MSC2.factory_native.add_block(1, "/Game/X.X_C", 0, 0, 0, 0)
        check("add_block: native refusal is passed through", a2 == nil and w2 == "refused")
        local seen
        _G.MSCoreNative_pending = function(...) seen = { ... } return true end
        local p1 = MSC2.factory_native.pending(1, true, false, 3, 4, 0)
        check("pending: booleans reach the native as 1/0", p1 == true and seen[2] == 1 and seen[3] == 0 and seen[4] == 3)
        local bi2 = MSC2.factory_native.read_block_class("/Game/X.X_C")
        check("read_block_class: native text is parsed", bi2 ~= nil and bi2.station == true and bi2.machines == 0)
    end)
    _G.MSCoreNative_version = nil
    _G.MSCoreNative_add_block = nil
    _G.MSCoreNative_pending = nil
    _G.MSCoreNative_read_block_class = nil
    setmetatable(_G, nil)
    if not sec_ok then check("add_block section ran without error: " .. tostring(sec_err), false) end
end

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
