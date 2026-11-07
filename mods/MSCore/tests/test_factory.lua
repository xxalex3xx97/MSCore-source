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

-- ===== parse_inventory (native read_inventory text)
local inv = F.parse_inventory("S\tHullPlate\t482\t100\nS\t\t0\t1\nS\tMagneticCoils\t50\t20\n")
check("parse_inventory: three slots", #inv == 3)
check("parse_inventory: slot 1 fields", inv[1].item == "HullPlate" and inv[1].amount == 482 and inv[1].max == 100)
check("parse_inventory: empty slot item is empty string", inv[2].item == "" and inv[2].amount == 0 and inv[2].max == 1)
check("parse_inventory: slot 3 fields", inv[3].item == "MagneticCoils" and inv[3].amount == 50 and inv[3].max == 20)
check("parse_inventory: empty text gives no slots", #F.parse_inventory("") == 0)

-- ===== install hooks
local MS = {}
F.install(MS)
I.install(MS)
check("install sets MSCore.factory and MSCore.inv", MS.factory == F and MS.inv == I)

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
