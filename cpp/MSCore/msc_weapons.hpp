// FILE: cpp/MSCore/msc_weapons.hpp
// WHAT: mining capability bits of a ship from its Weapons array: 1 = MiningLaser, 2 = ItemScooper. 0 = readable array with no mining weapon.
// WHY: mining mods read this here so they name no game property or weapon class. Ported from mods/Fleet/mb_weapons.hpp (weapon_array, weapon_match).
// READ: DMSPlayerShipObject.Weapons (TArray of weapon objects). Each weapon's class name is matched by substring (MiningLaser, ItemScooper).
// UNREADABLE: -1 = ship dead or null, property missing or not an array, count outside 0..kWeaponsMaxCount, or a native fault.
//             An empty readable array is 0 (no weapons), not -1.
// SAFETY: alive() guards the ship and each weapon; the array read runs under seh_run. A fault is logged once and returns -1 (no module kill: the caller decides).
// USED BY: dllmain.cpp (MSCore_ship_mining_caps, check_one "mining_caps").
// DEPENDS ON: msc_cmd.hpp (alive, seh_run), msc_ue.hpp (msc_log).
#pragma once
#include <cstdint>
#include <string>

namespace
{
    constexpr int32_t kWeaponsMaxCount = 100000;   // TArray count clamp for the Weapons array (ponytail: one constant, no shared helper existed)
    constexpr int kCapLaser = 1;                   // MiningLaser weapon present
    constexpr int kCapScooper = 2;                 // ItemScooper weapon present

    // WHAT: pointer to the ship's Weapons TArray, or nullptr when the property is missing or not an array.
    TArray<UObject*>* weapon_array(UObject* ship)
    {
        FProperty* p = ship->GetPropertyByNameInChain(STR("Weapons"));
        return (p && CastField<FArrayProperty>(p)) ? ship->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Weapons")) : nullptr;
    }

    // WHAT: true when the weapon's class name contains sub. A dead or null weapon is false.
    bool weapon_is(UObject* w, const TCHAR* sub)
    {
        if (!alive(w)) return false;
        const std::wstring cn = w->GetClassPrivate()->GetName();
        return cn.find(sub) != std::wstring::npos;
    }

    // WHAT: mining bits of ship (1 laser, 2 scooper), 0 none, -1 unknown. Never logs per call; a fault is logged once.
    int ship_mining_caps_read(UObject* ship)
    {
        if (!alive(ship)) return -1;
        static bool fault_logged = false;
        int bits = 0;
        bool ok = false;
        const bool ran = seh_run([&] {
            TArray<UObject*>* a = weapon_array(ship);
            if (!a) return;
            const int32_t n = a->Num();
            if (n < 0 || n > kWeaponsMaxCount) return;
            for (int32_t i = 0; i < n; ++i)
            {
                UObject* w = (*a)[i];
                if (weapon_is(w, STR("MiningLaser"))) bits |= kCapLaser;
                if (weapon_is(w, STR("ItemScooper"))) bits |= kCapScooper;
            }
            ok = true;
        });
        if (!ran)
        {
            if (!fault_logged) { fault_logged = true; msc_log(STR("ship_mining_caps: native fault, returning -1")); }
            return -1;
        }
        return ok ? bits : -1;
    }
}
