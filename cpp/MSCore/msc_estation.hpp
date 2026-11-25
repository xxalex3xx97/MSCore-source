// FILE: cpp/MSCore/msc_estation.hpp
// WHAT: enemy-station module (default ON): enemy_stations = enemy stations (EnemyStationObject) in the system of a ship, metres, nearest first. Read-only.
// WHY: Fleet attackers / defenders / escorts shoot enemy stations by logical name; the game names (EnemyStationObject, Health, ConstructionProgress) live only here.
// READ: FindAllOf EnemyStationObject cached 2 s (est_scan); sys, chunk, Location read like for enemy ships; Health / MaxHealth via cmb_hp; corpses (Health <= 0) are dropped.
//       Radius is NOT guessed: radius_m = -1 (flag bit2 clear). The SphereColliders inner struct property names are logged once (PROBE estation_collider) for the next slice.
// SAFETY: off when Mods\MSCore\estations_off.txt exists (g_est_off). Every raw access under seh_run; a fault turns module 'estations' off. Writes nothing.
// CAPS: module 'estations' (estations_resolve). Kill switch: module_disable("estations").
// USED BY: dllmain.cpp (estations_resolve, check_one "estations", MSCore_enemy_stations). Test: tests/test_estation_logic.cpp.
// UNVERIFIED (vault plan 2026-11-23-enemy-stations-PLAN.md, static RE only, never run live): class EnemyStationObject, Health i32 +0x118, MaxHealth +0x11c, ConstructionProgress / MaxConstructionProgress.
// DEPENDS ON: msc_estation_logic.hpp, msc_combat.hpp (cmb_hp, cmb_delta_m), msc_flight.hpp (ship_flight_read, flight_loc), msc_warp.hpp (get_chunk, sys_id), msc_caps.hpp, msc_ue.hpp.
#pragma once
#include <atomic>
#include <climits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "msc_estation_logic.hpp"

namespace
{
    constexpr ULONGLONG kEstCacheMs = 2000;   // station snapshot max age (stations do not move; FindAllOf is not free)

    std::atomic<bool> g_est_off{false};       // marker Mods\MSCore\estations_off.txt (default ON)

    struct EstRaw { int id; int sys; int hull; int max_hull; int build; int flags; int64_t ch[3]; double loc[3]; double radius; };
    struct EstSnap { ULONGLONG at = 0; bool valid = false; std::vector<EstRaw> rows; };
    EstSnap g_est_snap;
    std::mutex g_est_mx;
    std::mutex g_est_log_mx;                    // guards g_est_seen / g_est_col_logged (est_scan is called while g_est_mx is held, so it needs its own lock)
    std::set<int> g_est_seen;                 // ids already logged (PROBE estation), under g_est_log_mx
    bool g_est_col_logged = false;            // PROBE estation_collider once, under g_est_log_mx

    void estations_resolve() { static bool done = false; if (done) return; done = true; caps_add("estations", true, ""); }
    bool estations_enabled() { return !g_est_off.load() && module_ok(STR("estations")); }

    // WHAT: fresh scan of enemy stations. Corpses are dropped; an unreadable Health keeps the row (hull -1, flag bit1) so the consumer fails closed.
    //       false on a SEH fault only. Zero stations is a normal result.
    bool est_scan(std::vector<EstRaw>& rows)
    {
        rows.clear();
        std::vector<UObject*> all;
        if (!seh_run([&] { UObjectGlobals::FindAllOf(STR("EnemyStationObject"), all); })) return false;
        for (UObject* o : all)
        {
            EstRaw r{};
            bool keep = false;
            const bool ok = seh_run([&] {
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) return;
                r.id = obj_id(o);
                if (r.id <= 0 || !get_chunk(o, r.ch) || !flight_loc(o, r.loc)) return;
                int64_t hp = 0, mx = -1, cp = -1, cm = -1;
                const bool hok = cmb_hp(o, STR("Health"), hp);
                if (hok && hp <= 0) return;   // corpse the game still lists
                cmb_hp(o, STR("MaxHealth"), mx);
                cmb_hp(o, STR("ConstructionProgress"), cp);
                cmb_hp(o, STR("MaxConstructionProgress"), cm);
                r.sys = sys_id(o);
                r.hull = est_hull_value(hok, hp);
                r.max_hull = mx > 0 ? static_cast<int>(mx > INT_MAX ? INT_MAX : mx) : -1;
                r.build = est_build_pct(cp, cm);
                r.radius = -1.0;
                r.flags = est_flags(hok && hp > 0, !hok, false);
                {
                    std::lock_guard<std::mutex> lk(g_est_log_mx);
                    if (g_est_seen.insert(r.id).second)
                        msc_log(STR("PROBE estation id=") + widen(std::to_string(r.id)) + STR(" class=") + o->GetClassPrivate()->GetName() + STR(" hp=") + widen(std::to_string(hok ? hp : -1)) +
                                STR("/") + widen(std::to_string(mx)) + STR(" build=") + widen(std::to_string(r.build)) + STR("% radius=-1 sys=") + widen(std::to_string(r.sys)));
                    if (!g_est_col_logged)
                    {
                        g_est_col_logged = true;
                        std::wstring names;
                        FProperty* sp = o->GetPropertyByNameInChain(STR("SphereColliders"));
                        if (!sp) names = STR("no SphereColliders property");
                        else
                        {
                            auto* ap = CastField<FArrayProperty>(sp);
                            FProperty* in = ap ? ap->GetInner() : nullptr;
                            auto* st = in ? CastField<FStructProperty>(in) : nullptr;
                            if (st && st->GetStruct()) for (FProperty* q : st->GetStruct()->ForEachProperty()) names += q->GetName() + STR(",");
                            else names = STR("inner=") + (in ? in->GetClass().GetName() : std::wstring(STR("?")));
                        }
                        msc_log(STR("PROBE estation_collider props=") + names);
                    }
                }
                keep = true;
            });
            if (!ok) return false;
            if (keep) rows.push_back(r);
        }
        return true;
    }

    // WHAT: enemy stations of ship sid's system within radius_m, nearest first, copied to out (max rows). Returns the total count.
    // RETURNS: -2 module off, -1 scan fault (module turned off), 0 none / ship pose unreadable.
    int enemy_stations(int sid, double radius_m, MsEnemyStation* out, int max)
    {
        if (!estations_enabled()) return -2;
        MsFlight f{};
        if (ship_flight_read(sid, FL_POSE, f) != 1 || !(f.flags & FF_OBJ_POSE)) return 0;
        std::vector<EstRaw> rows;
        {
            std::lock_guard<std::mutex> lk(g_est_mx);
            const ULONGLONG now = GetTickCount64();
            if (!g_est_snap.valid || now - g_est_snap.at >= kEstCacheMs)
            {
                EstSnap n;
                if (!est_scan(n.rows))
                {
                    module_disable(STR("estations"), STR("station scan fault"));
                    g_est_snap = EstSnap();
                    return -1;
                }
                n.valid = true; n.at = now;
                g_est_snap = std::move(n);
            }
            rows = g_est_snap.rows;
        }
        std::vector<MsEnemyStation> v;
        for (const EstRaw& e : rows)
        {
            if (e.sys != f.sys) continue;
            MsEnemyStation m{};
            m.id = e.id; m.sys = e.sys; m.hull = e.hull; m.max_hull = e.max_hull; m.flags = e.flags; m.build_pct = e.build; m.radius_m = e.radius;
            m.d_m = cmb_delta_m(f.ch, f.loc_raw, e.ch, e.loc, m.rel_m);
            v.push_back(m);
        }
        const std::vector<MsEnemyStation> hit = est_filter(v, radius_m, INT_MAX);
        if (out && max > 0) for (size_t i = 0; i < hit.size() && static_cast<int>(i) < max; ++i) out[i] = hit[i];
        return static_cast<int>(hit.size());
    }

    // WHAT: read-only selftest: the scan must not fault; zero stations is fine (none loaded); with stations at least one needs a readable Health.
    bool estations_selftest(std::string& detail)
    {
        std::vector<EstRaw> rows;
        if (!est_scan(rows)) { detail = "station scan faulted"; return false; }
        int hull_ok = 0;
        for (const EstRaw& r : rows) if (!(r.flags & EST_F_HULL_UNREAD)) ++hull_ok;
        detail = "stations=" + std::to_string(rows.size()) + " hull_readable=" + std::to_string(hull_ok);
        if (rows.empty()) { detail += " (none loaded in the world)"; return true; }
        return hull_ok > 0;
    }
}
