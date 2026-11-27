// FILE: cpp/MSCore/msc_weapons.hpp
// WHAT: capability bits of a ship from its Weapons array (msc_wclass.hpp bits: 1 MiningLaser, 2 ItemScooper, 4 attack, 8 auto defence, 16 tractor). 0 = readable array with no weapon.
// EXPORT: MSCore_ship_caps (API 17) = ship_caps_read; MSCore_ship_mining_caps keeps 0..3 via ship_mining_caps_read (bits 1|2 only).
// WHY: mining mods read this here so they name no game property or weapon class. Ported from mods/Fleet/mb_weapons.hpp (weapon_array, weapon_match).
// READ: DMSPlayerShipObject.Weapons (TArray of weapon objects). Each weapon's class name is matched by substring (MiningLaser, ItemScooper).
// UNREADABLE: -1 = ship dead or null, property missing or not an array, count outside 0..kWeaponsMaxCount, or a native fault.
//             An empty readable array is 0 (no weapons), not -1.
// SAFETY: alive() guards the ship and each weapon; the array read runs under seh_run. A fault is logged once and returns -1 (no module kill: the caller decides).
// USED BY: dllmain.cpp (MSCore_ship_caps, MSCore_ship_mining_caps, check_one "caps" / "mining_caps").
// DEPENDS ON: msc_cmd.hpp (alive, seh_run), msc_ue.hpp (msc_log), msc_wclass.hpp (weapon_class_bits).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include "msc_wclass.hpp"

namespace
{
    constexpr int32_t kWeaponsMaxCount = 100000;   // TArray count clamp for the Weapons array (ponytail: one constant, no shared helper existed)

    std::atomic<bool> g_weapons_probe{false};   // set by weapons_probe.txt marker (dllmain refund_file_switch)
    void weapons_probe_name(const std::wstring& cn)
    {
        static std::set<std::wstring> seen;
        if (seen.insert(cn).second) msc_log(STR("weapons probe class=") + cn + STR(" bits=") + std::to_wstring(weapon_class_bits(cn)));
    }

    // WHAT: pointer to the ship's Weapons TArray, or nullptr when the property is missing or not an array.
    TArray<UObject*>* weapon_array(UObject* ship)
    {
        FProperty* p = ship->GetPropertyByNameInChain(STR("Weapons"));
        return (p && CastField<FArrayProperty>(p)) ? ship->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Weapons")) : nullptr;
    }

    // WHAT: capability bits of one weapon from its class name (weapon_class_bits). A dead or null weapon is 0.
    int weapon_bits(UObject* w)
    {
        if (!alive(w)) return 0;
        const std::wstring cn = w->GetClassPrivate()->GetName();
        if (g_weapons_probe.load()) weapons_probe_name(cn);
        return weapon_class_bits(cn);
    }

    // WHAT: PROBE caps line per ship, logged only when (weaponsNum, nonAlive, bits) changes for that ship id (not per call). Diagnostic for the ship list gate.
    void caps_probe_change(int sid, int num, int non_alive, int bits)
    {
        static std::map<int, std::array<int, 3>> last;
        static std::mutex mx;
        const std::array<int, 3> cur{num, non_alive, bits};
        {
            std::lock_guard<std::mutex> lk(mx);
            auto it = last.find(sid);
            if (it != last.end() && it->second == cur) return;
            last[sid] = cur;
        }
        msc_log(STR("PROBE caps ship=") + std::to_wstring(sid) + STR(" weaponsNum=") + std::to_wstring(num) + STR(" nonAlive=") + std::to_wstring(non_alive) + STR(" bits=") + std::to_wstring(bits));
    }

    // WHAT: caps bits 1..31, 0 none, -1 unknown. Never logs per call (PROBE caps only on change); a fault is logged once.
    int ship_caps_read(UObject* ship)
    {
        if (!alive(ship)) return -1;
        static std::atomic<bool> fault_logged{false};
        int bits = 0;
        bool ok = false;
        int num = -1, non_alive = 0;
        const bool ran = seh_run([&] {
            TArray<UObject*>* a = weapon_array(ship);
            if (!a) return;
            const int32_t n = a->Num();
            if (n < 0 || n > kWeaponsMaxCount) return;
            num = n;
            for (int32_t i = 0; i < n; ++i)
            {
                if (!alive((*a)[i])) ++non_alive;
                bits |= weapon_bits((*a)[i]);
            }
            ok = true;
        });
        if (ran) { int sid = 0; seh_run([&] { sid = obj_id(ship); }); if (sid > 0) caps_probe_change(sid, num, non_alive, ok ? bits : -1); }
        if (!ran)
        {
            if (!fault_logged.exchange(true)) { msc_log(STR("ship_caps: native fault, returning -1")); }
            return -1;
        }
        return ok ? bits : -1;
    }

    // WHAT: mining bits only (1 laser, 2 scooper) for MSCore_ship_mining_caps (API 15 contract 0..3). -1 passes through.
    int ship_mining_caps_read(UObject* ship)
    {
        const int r = ship_caps_read(ship);
        return r < 0 ? r : (r & (kCapLaser | kCapScooper));
    }
}
