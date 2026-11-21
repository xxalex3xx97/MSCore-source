// FILE: cpp/MSCore/msc_wclass.hpp
// WHAT: capability bits of one weapon from its class name (substring rules). Pure, no game headers.
// BITS: 1 MiningLaser, 2 ItemScooper, 4 attack weapon (Cannon/Lance/Missile), 8 auto defence (LaserTurret/Turret, never armed), 16 tractor. Bit 32 reserved for cargo (not from Weapons).
// RULE: a name containing "Turret" is auto defence only (Cannon in CannonTurret does not arm). Never match plain "Laser".
// USED BY: msc_weapons.hpp (MSCore_ship_caps). Test: tests/test_wclass.cpp.
#pragma once
#include <string>
namespace
{
    inline constexpr int kCapLaser = 1, kCapScooper = 2, kCapAttack = 4, kCapAutoDef = 8, kCapTractor = 16;
    inline bool wc_has(const std::wstring& s, const wchar_t* sub) { return s.find(sub) != std::wstring::npos; }
    inline int weapon_class_bits(const std::wstring& cn)
    {
        int bits = 0;
        if (wc_has(cn, L"MiningLaser")) bits |= kCapLaser;
        if (wc_has(cn, L"ItemScooper")) bits |= kCapScooper;
        if (wc_has(cn, L"Tractor")) bits |= kCapTractor;
        if (wc_has(cn, L"Turret")) bits |= kCapAutoDef;
        else if (wc_has(cn, L"Cannon") || wc_has(cn, L"Lance") || wc_has(cn, L"Missile")) bits |= kCapAttack;
        return bits;
    }
}
