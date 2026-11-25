// FILE: cpp/MSCore/msc_ballistics.hpp
// WHAT: ballistics module (default ON, API 25). ship_guns_read = aim gun group of a ship (bullet speed m/frame, lifetime frames, range m, TimeBetweenShots frames,
//       group size, distinct speeds, min/max speed). sim_rate_read = measured sim ticks per wall second. Maths: msc_ballistics_logic.hpp. Shared rate: msc_simrate.hpp.
// WHY: Fleet must not hold game names or constants for bullet speed and sim rate. MSCore learns both; Fleet runs a ladder learned -> last good -> INI.
// READ: DMSPlayerShipObject.Weapons (weapon_array). Per weapon: BulletSpeed, BulletLifetime, TimeBetweenShots (PNum, /65536 = per frame), bAddToPrimaryGroup (default true).
//       Cannon-class weapons: CannonObject.LastShotTime = sim frame stamp (verified live 2026-11-23, DevEval tickrate.lua). Turret-class weapons are not in the aim group.
// SAFETY: off when Mods\MSCore\ballistics_off.txt exists (g_bal_off). Raw reads under seh_run; a fault turns module 'ballistics' off. Writes nothing.
//         Plausibility fails closed: speed (0.1,100) m/frame, lifetime (1,100000) frames, range (10,20000) m, else no values (err set).
// CAPS: module 'ballistics' (ballistics_resolve). Kill switch: module_disable("ballistics").
// USED BY: dllmain.cpp (ballistics_resolve, check_one "ballistics", MSCore_ship_guns, MSCore_sim_rate). Test: tests/test_ballistics_logic.cpp.
// UNVERIFIED: BulletSpeed unit (assumed m/frame via PNum; Fleet measured 6.667 m/frame in DevEval, plausibility guards the rest); Turret exclusion may drop a primary turret (accepted).
// DEPENDS ON: msc_ballistics_logic.hpp, msc_simrate.hpp, msc_combat.hpp (cmb_num, cmb_scaled), msc_weapons.hpp (weapon_array, ship_caps_read), msc_flight.hpp (flight_ship),
//             msc_world.hpp (world_paused_read), msc_caps.hpp, msc_cmd.hpp (alive, seh_run), msc_ue.hpp (obj_id, get_bool_prop, widen, msc_log).
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "msc_ballistics_logic.hpp"
#include "msc_simrate.hpp"

namespace
{
    constexpr double kBalSpeedMin = 0.1, kBalSpeedMax = 100.0;     // plausible bullet speed, m/frame
    constexpr double kBalLifeMin = 1.0, kBalLifeMax = 100000.0;    // plausible lifetime, frames
    constexpr double kBalRangeMin = 10.0, kBalRangeMax = 20000.0;  // plausible range, m
    constexpr ULONGLONG kBalLogMs = 10000;                         // PROBE lines: at most one per key per 10 s

    std::atomic<bool> g_bal_off{false};   // marker Mods\MSCore\ballistics_off.txt (default ON)

    void ballistics_resolve() { static bool done = false; if (done) return; done = true; caps_add("ballistics", true, ""); }
    bool ballistics_enabled() { return !g_bal_off.load() && module_ok(STR("ballistics")) && module_ok(STR("ships")); }

    // WHAT: true when the rate-limited PROBE line for key may print now (kBalLogMs per key).
    bool bal_log_due(const std::string& key)
    {
        static std::mutex mx;
        static std::map<std::string, ULONGLONG> last;
        std::lock_guard<std::mutex> lk(mx);
        const ULONGLONG now = GetTickCount64();
        auto it = last.find(key);
        if (it != last.end() && now - it->second < kBalLogMs) return false;
        last[key] = now;
        return true;
    }

    // WHAT: paused state, re-read through world_paused_read at most every kBalPauseCheckS. Game thread, call under seh_run.
    bool bal_paused(double now_s)
    {
        static double checked_at = -1e9;
        static bool paused = false;
        if (now_s - checked_at >= kBalPauseCheckS) { checked_at = now_s; paused = world_paused_read() == 1; }
        return paused;
    }

    // WHAT: aim gun group of ship sid into out[8]: 0 speed m/frame, 1 lifetime frames, 2 range m, 3 TimeBetweenShots frames, 4 guns in group,
    //       5 distinct speeds, 6 min speed, 7 max speed. Also takes ONE sample of the sim rate (stamp = max LastShotTime of Cannon weapons).
    //       false + err when off, ship missing, no readable gun, implausible values, or a native fault (turns the module off).
    bool ship_guns_read(int sid, double* out, std::wstring& err)
    {
        if (!ballistics_enabled()) { err = STR("ballistics off"); return false; }
        UObject* ship = flight_ship(sid);
        if (!ship) { err = STR("ship not found"); return false; }
        std::vector<BalGun> guns;
        int64_t stamp = -1;
        bool arr_ok = false;
        bool paused = false;
        const double now = bal_now_s();
        if (!seh_run([&] {
                TArray<UObject*>* a = weapon_array(ship);
                if (!a || a->Num() <= 0 || a->Num() > kWeaponsMaxCount) return;
                arr_ok = true;
                for (int32_t i = 0; i < a->Num(); ++i)
                {
                    UObject* w = (*a)[i];
                    if (!alive(w)) continue;
                    const std::wstring cn = w->GetClassPrivate()->GetName();
                    if (cn.find(STR("Cannon")) != std::wstring::npos)
                    {
                        double st = 0;
                        if (cmb_num(w, STR("LastShotTime"), st)) stamp = std::max(stamp, static_cast<int64_t>(st));
                    }
                    if (cn.find(STR("Turret")) != std::wstring::npos) continue;
                    double sp = 0, lt = 0, tbs = 0;
                    if (!cmb_scaled(w, STR("BulletSpeed"), sp) || !cmb_scaled(w, STR("BulletLifetime"), lt)) continue;
                    if (sp <= 0 || lt <= 0) continue;
                    cmb_scaled(w, STR("TimeBetweenShots"), tbs);
                    bool prim = true;
                    get_bool_prop(w, STR("bAddToPrimaryGroup"), prim);
                    guns.push_back({ sp, lt, tbs, prim });
                }
                paused = bal_paused(now);
            }))
        {
            module_disable(STR("ballistics"), STR("ship_guns fault"));
            err = STR("native fault");
            return false;
        }
        {
            std::lock_guard<std::mutex> lk(g_bal_mx);
            bal_sample(g_bal, now, stamp, paused);
        }
        if (!arr_ok) { err = STR("no weapons array"); return false; }
        const BalPick p = bal_pick(guns);
        if (!p.ok) { err = STR("no gun with readable BulletSpeed/BulletLifetime"); return false; }
        if (!(p.speed_mpf > kBalSpeedMin && p.speed_mpf < kBalSpeedMax) || !(p.life_frames > kBalLifeMin && p.life_frames < kBalLifeMax) ||
            !(p.range_m > kBalRangeMin && p.range_m < kBalRangeMax))
        {
            err = STR("implausible values");
            return false;
        }
        out[0] = p.speed_mpf;
        out[1] = p.life_frames;
        out[2] = p.range_m;
        out[3] = p.tbs_frames;
        out[4] = p.n;
        out[5] = p.distinct;
        out[6] = p.smin;
        out[7] = p.smax;
        const std::string id = std::to_string(sid);
        char buf[320];
        if (bal_log_due("gun" + id))
        {
            std::snprintf(buf, sizeof buf, "PROBE bullet_gun ship=%d mpf=%.4f life_frames=%.2f range_m=%.1f tbs=%.3f n=%d distinct=%d", sid, p.speed_mpf, p.life_frames, p.range_m, p.tbs_frames, p.n, p.distinct);
            msc_log(widen(buf));
        }
        if (p.distinct >= 2 && bal_log_due("mix" + id))
        {
            std::snprintf(buf, sizeof buf, "PROBE bullet_speed mixed=1 ship=%d min=%.4f max=%.4f picked=%.4f", sid, p.smin, p.smax, p.speed_mpf);
            msc_log(widen(buf));
        }
        return true;
    }

    // WHAT: measured sim ticks per wall second (median of the stamp rate). false + err when off or not measured yet (needs firing). Pure read of g_bal.
    bool sim_rate_read(double* tps, int* samples, double* age_s, std::wstring& err)
    {
        if (!ballistics_enabled()) { err = STR("ballistics off"); return false; }
        const double now = bal_now_s();
        int n = 0;
        double t = 0, age = 0;
        {
            std::lock_guard<std::mutex> lk(g_bal_mx);
            t = bal_tps(g_bal, &n);
            age = bal_age(g_bal, now);
        }
        if (t <= 0) { err = STR("not measured yet (needs firing)"); return false; }
        *tps = t;
        *samples = n;
        *age_s = age;
        if (bal_log_due("sim_rate"))
        {
            char buf[160];
            std::snprintf(buf, sizeof buf, "PROBE sim_rate tps=%.2f samples=%d age_s=%.1f", t, n, age);
            msc_log(widen(buf));
        }
        return true;
    }

    // WHAT: read-only selftest on the first armed ship (kCapAttack): guns + sim rate. SKIP when no armed ship. Call under seh_run.
    bool ballistics_selftest(std::string& detail)
    {
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        for (UObject* ship : ships)
        {
            if (!alive(ship) || ship->GetName().rfind(STR("Default__"), 0) == 0) continue;
            if (!(ship_caps_read(ship) & kCapAttack)) continue;
            const int sid = obj_id(ship);
            double o[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
            std::wstring err;
            if (sid <= 0 || !ship_guns_read(sid, o, err))
            {
                detail = "ship=" + std::to_string(sid) + " guns unreadable: ";
                for (wchar_t c : err) detail += static_cast<char>(c);   // ASCII reasons
                return false;
            }
            double tps = 0, age = 0;
            int n = 0;
            std::wstring err2;
            const bool measured = sim_rate_read(&tps, &n, &age, err2);
            char buf[320];
            std::snprintf(buf, sizeof buf, "ship=%d mpf=%.4f life_frames=%.2f range_m=%.1f tbs=%.3f n=%d distinct=%d tps=%.2f measured=%d",
                          sid, o[0], o[1], o[2], o[3], static_cast<int>(o[4]), static_cast<int>(o[5]), sim_tps_conv(), measured ? 1 : 0);
            detail = buf;
            return true;
        }
        detail = "SKIP no armed ship";
        return false;
    }
}
