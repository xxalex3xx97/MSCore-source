// FILE: cpp/MSCore/msc_combat.hpp
// WHAT: combat module (OFF by default): enemies_near (enemy ships around a ship, metres) and ship_fire (ShipFiringInput on change only).
//       ship_weapon_range is a stub. Pure maths lives in msc_combat_logic.hpp.
// WHY: mods ask "who is near / fire on or off" by logical name; the game names (EnemyShipObject, Health, ShipFiringInput) live only here.
// READ: enemy snapshot (FindAllOf EnemyShipObject) refreshed at most every kCmbCacheMs; sys, chunk and Location read like for ships.
// SAFETY: off until Mods\MSCore\combat_on.txt exists (g_combat_on). Every raw access under seh_run; a fault turns module 'combat' off.
//         Fail closed: no factory / no class -> ship_fire returns false. Fire is sent on change only (g_fire_last). No NewObject fallback.
//         Marker combat_fire_console.txt sends the fire command to the pilot console instead of the ship.
// CAPS: module 'combat' (combat_resolve in the caps_once block of dllmain.cpp). Kill switch: module_disable("combat").
// USED BY: dllmain.cpp (combat_resolve, check_one "combat", MSCore_enemies_near, MSCore_ship_fire, MSCore_ship_weapon_range).
// DEPENDS ON: msc_combat_logic.hpp, msc_cmd.hpp, msc_caps.hpp, msc_ue.hpp, msc_flight.hpp, msc_warp.hpp (get_chunk, sys_id), msc_weapons.hpp, msc_contract.hpp.
// UNVERIFIED (all from vault main-sequence\game-map-combat.md, static RE, never run live):
//   (1) class EnemyShipObject and its Health property (EnemyBase +0x118 i32);
//   (2) enemy position = Location plus chunk (same helpers as ships), units raw 65536 per metre;
//   (3) ShipFiringInput (cmd 60; bPrimary +0x40, bSecondary +0x41), whether it has a CreateInput* factory, receiver ship vs pilot console;
//   (4) chunk edge 2048 m (kCmbChunkM in msc_combat_logic.hpp).
#pragma once
#include <atomic>
#include <climits>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "msc_combat_logic.hpp"

namespace
{
    constexpr ULONGLONG kCmbCacheMs = 500;   // enemy snapshot max age

    std::atomic<bool> g_combat_on{false};       // marker Mods\MSCore\combat_on.txt (default OFF)
    std::atomic<bool> g_fire_console{false};    // marker Mods\MSCore\combat_fire_console.txt
    std::map<int, int> g_fire_last;             // ship id -> last fire command sent (0 off, 1 on)
    std::mutex g_fire_mx;

    struct EnemyRaw { int id; int sys; int hull; int64_t ch[3]; double loc[3]; };
    struct EnemySnap { ULONGLONG at = 0; bool valid = false; std::vector<EnemyRaw> rows; };
    EnemySnap g_enemy_snap;
    std::mutex g_enemy_mx;

    void combat_resolve() { static bool done = false; if (done) return; done = true; caps_add("combat", true, ""); }
    bool combat_enabled() { return g_combat_on.load() && module_ok(STR("combat")); }

    std::string cmb_ascii(const std::wstring& s) { return std::string(s.begin(), s.end()); }

    // WHAT: fresh scan of enemy ships. false on SEH fault, or when objects exist but none was readable.
    bool enemies_scan(std::vector<EnemyRaw>& rows)
    {
        rows.clear();
        std::vector<UObject*> all;
        if (!seh_run([&] { UObjectGlobals::FindAllOf(widen(kClsEnemyShip).c_str(), all); })) return false;
        int seen = 0, skipped = 0;
        for (UObject* o : all)
        {
            EnemyRaw r{};
            bool keep = false;
            const bool ok = seh_run([&] {
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) return;
                ++seen;
                r.id = obj_id(o);
                int32_t hp = 0;
                if (r.id <= 0 || !get_chunk(o, r.ch) || !flight_loc(o, r.loc) || !get_int_prop(o, STR("Health"), hp)) { ++skipped; return; }
                r.sys = sys_id(o);
                r.hull = hp;
                keep = true;
            });
            if (!ok) return false;
            if (keep) rows.push_back(r);
        }
        return !(seen > 0 && rows.empty());   // ponytail: skipped is only counted; add a log line when live data shows skips
    }

    // WHAT: enemies of ship sid's system within radius_m, nearest first, copied to out (max rows). Returns the total count.
    // RETURNS: -2 combat off, -1 scan fault (module turned off), 0 none / ship pose unreadable.
    int enemies_near(int sid, double radius_m, MsEnemy* out, int max)
    {
        if (!combat_enabled()) return -2;
        MsFlight f{};
        if (ship_flight_read(sid, FL_POSE, f) != 1 || !(f.flags & FF_OBJ_POSE)) return 0;
        std::vector<EnemyRaw> rows;
        {
            std::lock_guard<std::mutex> lk(g_enemy_mx);
            const ULONGLONG now = GetTickCount64();
            if (!g_enemy_snap.valid || now - g_enemy_snap.at >= kCmbCacheMs)
            {
                EnemySnap n;
                if (!enemies_scan(n.rows))
                {
                    module_disable(STR("combat"), STR("enemy scan fault or layout"));
                    g_enemy_snap = EnemySnap();
                    return -1;
                }
                n.valid = true; n.at = now;
                g_enemy_snap = std::move(n);
            }
            rows = g_enemy_snap.rows;
        }
        std::vector<MsEnemy> v;
        for (const EnemyRaw& e : rows)
        {
            if (e.sys != f.sys) continue;
            MsEnemy m{};
            m.id = e.id; m.sys = e.sys; m.hull = e.hull;
            m.d_m = cmb_delta_m(f.ch, f.loc_raw, e.ch, e.loc, m.rel_m);
            v.push_back(m);
        }
        const std::vector<MsEnemy> hit = cmb_filter(v, radius_m, INT_MAX);
        if (out && max > 0) for (size_t i = 0; i < hit.size() && static_cast<int>(i) < max; ++i) out[i] = hit[i];
        return static_cast<int>(hit.size());
    }

    // WHAT: ShipFiringInput(primary = on, secondary = false) to ship sid, only when the state changes. false with err on failure.
    bool ship_fire(int sid, bool on, std::wstring& err)
    {
        if (!combat_enabled()) { err = STR("combat off"); return false; }
        if (!flight_ship(sid)) { err = STR("ship not found"); return false; }
        std::lock_guard<std::mutex> lk(g_fire_mx);
        auto it = g_fire_last.find(sid);
        if (!cmb_fire_send(it == g_fire_last.end() ? -1 : it->second, on)) return true;
        bool ok = false;
        if (!seh_run([&] {
                UObject* cmd = make_cmd(STR("ShipFiringInput"), [on](UFunction* fn, std::vector<uint8_t>& b) {
                    for (FProperty* q : fn->ForEachProperty())
                    {
                        if (!CastField<FBoolProperty>(q)) continue;
                        const std::wstring n = q->GetName();
                        uint8_t* d = b.data() + q->GetOffset_Internal();
                        if (n.find(STR("Primary")) != std::wstring::npos) *d = on ? 1 : 0;
                        else if (n.find(STR("Secondary")) != std::wstring::npos) *d = 0;
                    }
                    return true;
                }, err);
                int recv = sid;
                if (cmd && g_fire_console.load()) { const int c = flight_console_id(sid); if (c > 0) recv = c; }
                ok = cmd && add_input(recv, cmd, err);
            }))
        {
            err = STR("ship_fire faulted");
            module_disable(STR("combat"), STR("ship_fire fault"));
            return false;
        }
        if (ok) g_fire_last[sid] = on ? 1 : 0;
        return ok;
    }

    // STUB: range formula needs probe P-D (BulletSpeed * BulletLifetime units unknown)
    int ship_weapon_range(int sid) { (void)sid; return combat_enabled() ? -1 : -2; }

    // WHAT: raw value of weapon property name as text: int / float / double, or a struct's Value (PNum). "n/a" when missing.
    std::string cmb_prop_text(UObject* w, const TCHAR* name)
    {
        FProperty* p = w->GetPropertyByNameInChain(name);
        if (!p) return "n/a";
        double d = 0;
        if (get_num_prop(w, name, d)) return std::to_string(static_cast<long long>(d));
        int64_t v = 0;
        auto* sp = CastField<FStructProperty>(p);
        if (sp && sp->GetStruct() && read_axis(reinterpret_cast<const uint8_t*>(w) + p->GetOffset_Internal(), sp->GetStruct(), STR("Value"), v)) return std::to_string(v);
        return "n/a";
    }

    // WHAT: read-only selftest, sends nothing: both classes resolve, factory presence (probe P-B), enemy count, weapon props of the first ship (probe P-D).
    bool combat_selftest(std::string& detail)
    {
        UClass* enemy = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.EnemyShipObject"));
        if (!enemy) { detail = "EnemyShipObject class not found"; return false; }
        UClass* fire = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ShipFiringInput"));
        if (!fire) { detail = "ShipFiringInput class not found"; return false; }
        std::string fac = "no CreateInput*";
        for (UFunction* fn : fire->ForEachFunction())
            if (fn->GetName().rfind(STR("CreateInput"), 0) == 0)
            {
                fac = "factory " + cmb_ascii(fn->GetName()) + "(";
                for (FProperty* q : fn->ForEachProperty())
                    fac += cmb_ascii(q->GetName()) + ":" + (CastField<FBoolProperty>(q) ? "bool" : CastField<FIntProperty>(q) ? "int" : CastField<FObjectProperty>(q) ? "obj" : "other") + " ";
                fac += ")";
                break;
            }
        std::vector<EnemyRaw> rows;
        const bool scan = enemies_scan(rows);
        detail = fac + " enemies=" + (scan ? std::to_string(rows.size()) : std::string("scan failed"));
        UObject* ship = first_of(STR("DMSPlayerShipObject"));
        TArray<UObject*>* a = alive(ship) ? weapon_array(ship) : nullptr;
        if (a && a->Num() > 0 && a->Num() <= kWeaponsMaxCount)
        {
            static const TCHAR* props[6] = { STR("BulletSpeed"), STR("BulletLifetime"), STR("TimeBetweenShots"), STR("MissileRange"), STR("Range"), STR("LaserReachMeters") };
            for (int32_t i = 0; i < a->Num(); ++i)
            {
                UObject* w = (*a)[i];
                if (!alive(w)) continue;
                std::wstring line = STR("combat probe weapon ") + w->GetClassPrivate()->GetName();
                for (const TCHAR* pn : props) line += std::wstring(STR(" ")) + pn + STR("=") + widen(cmb_prop_text(w, pn));
                msc_log(line);
            }
        }
        return true;
    }
}
