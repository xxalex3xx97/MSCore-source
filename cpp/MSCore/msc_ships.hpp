// FILE: cpp/MSCore/msc_ships.hpp
// WHAT: ship shields (current and max) in whole units by one native read. Class name from msc_contract.hpp (kClsShip, logical 'ship').
// WHY: mining mods read shields here so they name no game property. Ported from MiningBenchMod mb_logic.hpp (shield_whole, shield_from_raw) and mb_ue.hpp (pnum_prop, call_double).
// READ: PNum Shields / MaxShields first (Value, Q16 raw, 65536 = 1 unit). The double GetShields / GetMaxShields is read only when the PNum property is missing.
// UNREADABLE: negative or NaN = unreadable (bit clear, value -1). Above INT32_MAX clamps to INT32_MAX.
// SAFETY: alive() guards the object; the read runs under seh_run. A fault returns 0 (nothing readable), never a crash.
// CAPS: module 'ships' (ships_resolve registers it at startup). Runtime kill switch: module_disable("ships") in dllmain.cpp.
// USED BY: dllmain.cpp (ships_resolve, selftest check "ships", MSCore_ship_shields).
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    // WHAT: registers the ships cap (ok) once. The shield read is checked by the selftest (check "ships"): a world with no ship is a SKIP.
    void ships_resolve() { static bool done = false; if (done) return; done = true; caps_add("ships", true, ""); }

    // WHAT: whole shield units from a double. Negative / NaN = -1 (unreadable). Floors, clamps to INT32_MAX.
    int32_t shield_whole(double v) { if (!(v >= 0.0)) return -1; if (v > 2147483647.0) return 2147483647; return static_cast<int32_t>(v); }

    // WHAT: whole shield units from a raw PNum Value (Q16, 65536 = 1 unit). Negative raw = -1.
    int32_t shield_from_raw(int64_t raw) { return shield_whole(static_cast<double>(raw) / 65536.0); }

    // WHAT: raw Value of PNum property prop of o (int64 or int32). false when the property is missing or is not a PNum.
    bool pnum_raw(UObject* o, const TCHAR* prop, int64_t& out)
    {
        FProperty* p = o->GetPropertyByNameInChain(prop);
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return false;
        uint8_t* base = o->GetValuePtrByPropertyNameInChain<uint8_t>(prop);
        FProperty* v = base ? sp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add)) : nullptr;
        if (!v) return false;
        const uint8_t* d = base + v->GetOffset_Internal();
        if (CastField<FInt64Property>(v)) out = *reinterpret_cast<const int64_t*>(d);
        else if (CastField<FIntProperty>(v)) out = *reinterpret_cast<const int32_t*>(d);
        else return false;
        return true;
    }

    // WHAT: double return value of function fname on o. false when missing or not a double.
    bool call_double(UObject* o, const TCHAR* fname, double& out)
    {
        UFunction* fn = o->GetFunctionByNameInChain(fname);
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        if (!rp || !CastField<FDoubleProperty>(rp)) return false;
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        out = *reinterpret_cast<double*>(buf.data() + rp->GetOffset_Internal());
        return true;
    }

    // WHAT: one shield value: the PNum prop when it exists (its read decides, even when unreadable), else the getter double.
    // WHY: same order as MiningBenchMod hull_watch (pnum first, GetShields fallback).
    bool read_shield(UObject* ship, const TCHAR* pnum, const TCHAR* getter, int32_t& out)
    {
        int64_t raw = 0;
        if (pnum_raw(ship, pnum, raw)) { out = shield_from_raw(raw); return out >= 0; }
        double d = 0.0;
        if (call_double(ship, getter, d)) { out = shield_whole(d); return out >= 0; }
        out = -1;
        return false;
    }

    // WHAT: current and max shield of ship in whole units. Returns bitmask 1 = cur readable, 2 = max readable, 0 = none (also a dead ship or a fault).
    // WHY: the caller checks the bits, a -1 is never a real value. The SEH call keeps this function free of C++ objects.
    int ship_shields_read(UObject* ship, int32_t& cur, int32_t& max)
    {
        cur = -1;
        max = -1;
        if (!alive(ship)) return 0;
        int32_t c = -1, m = -1;
        int ok = 0;
        if (!seh_run([&] {
                if (read_shield(ship, STR("Shields"), STR("GetShields"), c)) ok |= 1;
                if (read_shield(ship, STR("MaxShields"), STR("GetMaxShields"), m)) ok |= 2;
            })) return 0;
        cur = c;
        max = m;
        return ok;
    }

    // WHAT: current and total hull of ship (GetCurrentHull / GetTotalHull, int). Unreadable value = -1. true when at least one value was read.
    // WHY: the export derives its bitmask from cur >= 0 / tot >= 0. Same call style as ship_repair_need (msc_repair.hpp).
    bool ship_hull_read(UObject* ship, int32_t& cur, int32_t& tot)
    {
        cur = -1;
        tot = -1;
        if (!alive(ship)) return false;
        int32_t c = -1, t = -1;
        bool a = false, b = false;
        if (!seh_run([&] { a = call_int(ship, STR("GetCurrentHull"), c); b = call_int(ship, STR("GetTotalHull"), t); })) return false;
        cur = a ? c : -1;
        tot = b ? t : -1;
        return a || b;
    }
}
