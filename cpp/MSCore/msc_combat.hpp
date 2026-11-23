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
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "msc_combat_logic.hpp"
#include "msc_simrate.hpp"

namespace
{
    constexpr ULONGLONG kCmbCacheMs = 500;   // enemy snapshot max age

    std::atomic<bool> g_combat_on{false};       // marker Mods\MSCore\combat_on.txt (default OFF)
    std::atomic<bool> g_fire_console{false};    // marker Mods\MSCore\combat_fire_console.txt
    std::map<int, int> g_fire_last;             // ship id -> last fire command sent (0 off, 1 on)
    std::mutex g_fire_mx;

    struct EnemyRaw { int id; int sys; int hull; int flags; int64_t ch[3]; double loc[3]; };
    struct EnemyPose { int64_t ch[3]; double loc[3]; bool big_known = false; bool big = false; };
    std::map<int, EnemyPose> g_enemy_pose;   // last scan pose per enemy id, pruned to ids seen
    std::mutex g_pose_mx;
    CmbLogRate g_probe_rate;                 // global cap on PROBE enemy_class lines (20 per 10 s)
    std::mutex g_probe_mx;
    CmbLogRate g_dead_rate;                  // cap on PROBE enemy_dead / enemy_task lines (20 per 10 s)
    std::set<std::pair<std::wstring, int>> g_task_seen, g_dead_seen, g_unk_seen;   // (class,task) pairs / dead ids / unknown-health classes already logged (under g_probe_mx)
    struct EnemySnap { ULONGLONG at = 0; bool valid = false; std::vector<EnemyRaw> rows; };
    EnemySnap g_enemy_snap;
    std::mutex g_enemy_mx;

    void combat_resolve() { static bool done = false; if (done) return; done = true; caps_add("combat", true, ""); }
    bool combat_enabled() { return g_combat_on.load() && module_ok(STR("combat")); }

    std::string cmb_ascii(const std::wstring& s) { return std::string(s.begin(), s.end()); }

    // WHAT: CURRENT Health of an enemy object as read by the DevEval dump (get(o,"Health")): Int / Int64 / Float / Double, or a struct's Value (PNum, raw). false when unreadable. Call under seh_run.
    //       A raw struct Value is not rescaled: the sign (alive test) is what matters.
    bool cmb_hp(UObject* o, const TCHAR* name, int64_t& out)
    {
        int32_t i = 0;
        if (get_int_prop(o, name, i)) { out = i; return true; }
        FProperty* p = o ? o->GetPropertyByNameInChain(name) : nullptr;
        if (!p) return false;
        double d = 0;
        if (get_num_prop(o, name, d)) { out = static_cast<int64_t>(d); return true; }
        auto* sp = CastField<FStructProperty>(p);
        int64_t v = 0;
        if (!sp || !sp->GetStruct() || !read_axis(reinterpret_cast<const uint8_t*>(o) + p->GetOffset_Internal(), sp->GetStruct(), STR("Value"), v)) return false;
        out = v;
        return true;
    }

    // WHAT: fresh scan of enemy ships. Enemies with CURRENT Health <= 0 (corpses the game still lists) are dropped here, so every consumer
    //       (enemies_near, hostile counts) never sees them; unreadable Health keeps a row with hull -1, alive clear, flag bit3 (fail closed).
    //       false on SEH fault, or when objects exist but none had a readable pose + Health.
    bool enemies_scan(std::vector<EnemyRaw>& rows)
    {
        rows.clear();
        std::vector<UObject*> all;
        if (!seh_run([&] { UObjectGlobals::FindAllOf(widen(kClsEnemyShip).c_str(), all); })) return false;
        int seen = 0, skipped = 0, known = 0;
        for (UObject* o : all)
        {
            EnemyRaw r{};
            bool keep = false;
            const bool ok = seh_run([&] {
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) return;
                ++seen;
                r.id = obj_id(o);
                int64_t hp = 0;
                if (r.id <= 0 || !get_chunk(o, r.ch) || !flight_loc(o, r.loc)) { ++skipped; return; }
                const bool hok = cmb_hp(o, STR("Health"), hp);
                const CmbHp hs = cmb_hp_state(hok, hp);
                int64_t mx = -1;
                cmb_hp(o, STR("MaxHealth"), mx);
                const std::wstring cn0 = o->GetClassPrivate()->GetName();
                if (hs == CMB_HP_DEAD)
                {   // corpse: log once per id, drop the row (the pose entry is pruned below), counts as readable
                    ++known;
                    std::lock_guard<std::mutex> lk(g_probe_mx);
                    if (g_dead_seen.insert({std::wstring(), r.id}).second && cmb_log_allow(g_dead_rate, GetTickCount64())) msc_log(cmb_dead_line(r.id, cn0, hp, mx));
                    return;
                }
                if (hs == CMB_HP_UNKNOWN)
                {
                    std::lock_guard<std::mutex> lk(g_probe_mx);
                    if (cmb_first_seen(g_unk_seen, cn0, 0)) msc_log(STR("combat enemy Health unreadable class=") + cn0 + STR(" id=") + widen(std::to_string(r.id)));
                }
                else ++known;
                r.sys = sys_id(o);
                r.hull = cmb_hull_value(hok, hp);
                // UNVERIFIED: CurrentTask read as one raw byte at the property offset (enum/byte prop, +0x178)
                int task = 0xFF;
                if (FProperty* tp = o->GetPropertyByNameInChain(STR("CurrentTask"))) task = *(reinterpret_cast<const uint8_t*>(o) + tp->GetOffset_Internal());
                bool moved = false, big = false, big_known = false;
                {
                    std::lock_guard<std::mutex> lk(g_pose_mx);
                    auto it = g_enemy_pose.find(r.id);
                    if (it != g_enemy_pose.end()) { big_known = it->second.big_known; big = it->second.big; }
                }
                if (!big_known)   // class + MaxHealth read once per enemy id, cached in the pose map
                {
                    const std::wstring& cn = cn0;
                    big = cmb_class_big(cn);
                    if (!big && mx >= kCmbBigMaxHealth) big = true;   // MaxHealth: strength / BIG only, never the alive test
                    bool log_it = false;
                    {
                        std::lock_guard<std::mutex> lk(g_probe_mx);
                        log_it = cmb_log_allow(g_probe_rate, GetTickCount64());
                    }
                    if (log_it) msc_log(cmb_probe_class_line(r.id, cn, big, hok ? hp : -1, mx));
                }
                {
                    std::lock_guard<std::mutex> lk(g_pose_mx);
                    auto it = g_enemy_pose.find(r.id);
                    if (it != g_enemy_pose.end())
                    {
                        double rel[3];
                        moved = cmb_delta_m(it->second.ch, it->second.loc, r.ch, r.loc, rel) >= 1.0;
                    }
                    EnemyPose& ps = g_enemy_pose[r.id];
                    for (int i = 0; i < 3; ++i) { ps.ch[i] = r.ch[i]; ps.loc[i] = r.loc[i]; }
                    ps.big_known = true; ps.big = big;
                }
                {   // CurrentTask table: once per (class, task)
                    std::lock_guard<std::mutex> lk(g_probe_mx);
                    if (cmb_first_seen(g_task_seen, cn0, task) && cmb_log_allow(g_dead_rate, GetTickCount64())) msc_log(cmb_task_line(r.id, cn0, task, hok ? hp : -1));
                }
                r.flags = cmb_enemy_flags(hs == CMB_HP_ALIVE, moved, task, big, hs == CMB_HP_UNKNOWN);
                keep = true;
            });
            if (!ok) return false;
            if (keep) rows.push_back(r);
        }
        {
            std::lock_guard<std::mutex> lk(g_pose_mx);
            for (auto it = g_enemy_pose.begin(); it != g_enemy_pose.end();)
            {
                bool found = false;
                for (const EnemyRaw& r : rows) if (r.id == it->first) { found = true; break; }
                it = found ? std::next(it) : g_enemy_pose.erase(it);
            }
        }
        return !(seen > 0 && known == 0);   // nothing readable (pose + Health) = layout problem; ponytail: skipped is only counted
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
            m.id = e.id; m.sys = e.sys; m.hull = e.hull; m.pad = e.flags;
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
                        if (n == STR("bSetPrimary")) *d = 1;
                        else if (n == STR("bFiringPrimary")) *d = on ? 1 : 0;
                        else if (n == STR("bSetSecondary") || n == STR("bFiringSecondary")) *d = 0;
                        else if (n.find(STR("Primary")) != std::wstring::npos) *d = on ? 1 : 0;
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

    // WHAT: practice enemy near ship sid: EnemyStationObject::GetClosestEnemyBase(ship) -> CreativeEnemyInput(ship id) -> AddInputCommand(baseId).
    // WHY: same chain as the game's own WBP_StationInventory. Guesses: static call on the class default object; first int param named *ID*/*Target* = ship id.
    bool combat_spawn_enemy(int sid, std::wstring& err)
    {
        if (!combat_enabled()) { err = STR("combat off"); return false; }
        UObject* ship = flight_ship(sid);
        if (!ship) { err = STR("ship not found"); return false; }
        bool ok = false;
        if (!seh_run([&] {
                UClass* ecls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.EnemyStationObject"));
                UObject* cdo = ecls ? ecls->GetClassDefaultObject() : nullptr;
                UFunction* fn = alive(cdo) ? cdo->GetFunctionByNameInChain(STR("GetClosestEnemyBase")) : nullptr;
                if (!fn) { err = STR("GetClosestEnemyBase not found"); return; }
                std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
                FProperty* rp = fn->GetReturnProperty();
                FProperty* ap = nullptr;
                for (FProperty* q : fn->ForEachProperty()) if (CastField<FObjectProperty>(q) && q != rp) { ap = q; break; }
                if (!rp || !ap) { err = STR("GetClosestEnemyBase params unexpected"); return; }
                *reinterpret_cast<UObject**>(buf.data() + ap->GetOffset_Internal()) = ship;
                cdo->ProcessEvent(fn, buf.data());
                UObject* base = *reinterpret_cast<UObject**>(buf.data() + rp->GetOffset_Internal());
                if (!alive(base)) { err = STR("no enemy base"); return; }
                const int bid = obj_id(base);
                if (bid <= 0) { err = STR("enemy base id unreadable"); return; }
                static bool logged = false;
                UObject* cmd = make_cmd(STR("CreativeEnemyInput"), [sid](UFunction* f, std::vector<uint8_t>& b) {
                    bool set = false;
                    std::wstring pl = STR("combat spawn factory:");
                    for (FProperty* q : f->ForEachProperty())
                    {
                        const std::wstring n = q->GetName();
                        pl += STR(" ") + n + (CastField<FIntProperty>(q) ? STR(":int") : STR(":other"));
                        if (!set && CastField<FIntProperty>(q) && (n.find(STR("ID")) != std::wstring::npos || n.find(STR("Target")) != std::wstring::npos))
                        { *reinterpret_cast<int32_t*>(b.data() + q->GetOffset_Internal()) = sid; set = true; }
                    }
                    if (!logged) { logged = true; msc_log(pl); }
                    return set;
                }, err);
                ok = cmd && add_input(bid, cmd, err);
            }))
        {
            err = STR("spawn faulted");
            module_disable(STR("combat"), STR("spawn_enemy fault"));
            return false;
        }
        return ok;
    }

    // WHAT: numeric weapon prop as double: plain Float/Double/Int, or a PNum struct's Value (BulletSpeed etc. are PNum). false when missing/unreadable.
    bool cmb_num(UObject* w, const TCHAR* name, double& out)
    {
        FProperty* p = w->GetPropertyByNameInChain(name);
        if (!p) return false;
        if (get_num_prop(w, name, out)) return true;
        int64_t v = 0;
        auto* sp = CastField<FStructProperty>(p);
        if (!sp || !sp->GetStruct() || !read_axis(reinterpret_cast<const uint8_t*>(w) + p->GetOffset_Internal(), sp->GetStruct(), STR("Value"), v)) return false;
        out = static_cast<double>(v);
        return true;
    }

    // WHAT: raw value of weapon property name as text: int / float / double, or a struct's Value (PNum). "n/a" when missing.
    std::string cmb_prop_text(UObject* w, const TCHAR* name)
    {
        double d = 0;
        return cmb_num(w, name, d) ? std::to_string(static_cast<long long>(d)) : "n/a";
    }

    // WHAT: engage range (whole metres) of ship sid = shortest cannon range * kRangeSafety. -2 combat off, -1 none/unreadable/fault.
    // Every -1 logs its reason once per ship (again only when the reason changes).
    // ponytail: BulletLifetime assumed ticks (UNCONFIRMED), kRangeSafety tune after in-game pass 2.
    int ship_weapon_range(int sid)
    {
        if (!combat_enabled()) return -2;
        static std::mutex mx;
        static std::map<int, std::wstring> logged;
        auto note = [&](const std::wstring& msg) {
            std::lock_guard<std::mutex> lk(mx);
            auto it = logged.find(sid);
            if (it != logged.end() && it->second == msg) return;
            logged[sid] = msg;
            msc_log(STR("combat range ship=") + widen(std::to_string(sid)) + STR(" ") + msg);
        };
        UObject* ship = flight_ship(sid);
        if (!ship) { note(STR("none why=ship not found")); return -1; }
        std::vector<double> rs;
        std::wstring why, raws;
        int ncannon = 0;
        if (!seh_run([&] {
                TArray<UObject*>* a = weapon_array(ship);
                if (!a || a->Num() <= 0 || a->Num() > kWeaponsMaxCount) { why = STR("no weapons array"); return; }
                for (int32_t i = 0; i < a->Num(); ++i)
                {
                    UObject* w = (*a)[i];
                    if (!alive(w)) continue;
                    const std::wstring cn = w->GetClassPrivate()->GetName();
                    if (cn.find(STR("Cannon")) == std::wstring::npos || cn.find(STR("Turret")) != std::wstring::npos) continue;
                    ++ncannon;
                    double sp = 0, lt = 0;
                    bool bad = false;
                    static const TCHAR* const kBp[2] = { STR("BulletSpeed"), STR("BulletLifetime") };
                    for (const TCHAR* pn : kBp)
                    {
                        if (cmb_num(w, pn, pn[6] == 'S' ? sp : lt)) continue;
                        FProperty* p = w->GetPropertyByNameInChain(pn);
                        why = std::wstring(STR("read failed ")) + pn + STR(" type=") + (p ? p->GetClass().GetName() : std::wstring(STR("missing")));
                        bad = true;
                        break;
                    }
                    if (bad) continue;
                    const double r = cmb_range_m(sp, lt, 1.0, kCmbRawPerM);
                    if (r <= 0) { why = STR("range<=0"); continue; }
                    rs.push_back(r);
                    raws += STR(" speed=") + widen(std::to_string(static_cast<long long>(sp))) + STR(" lifetime=") + widen(std::to_string(static_cast<long long>(lt)));
                }
            }))
        {
            module_disable(STR("combat"), STR("ship_weapon_range fault"));
            return -1;
        }
        const int out = cmb_cannon_reach(ncannon);
        if (out <= 0) { note(STR("none why=") + (why.empty() ? std::wstring(STR("no cannon")) : why)); return -1; }
        const double raw = cmb_min_range(rs);
        note(STR("n=") + widen(std::to_string(ncannon)) + STR(" raw_m=") + widen(std::to_string(static_cast<int>(raw))) + STR(" reach_m=") +
             widen(std::to_string(out)) + STR(" (user-observed)") + raws);
        return out;
    }

    // WHAT: object name of an object property, "n/a" when missing/null.
    std::string cmb_obj_name(UObject* w, const TCHAR* name)
    {
        FProperty* p = w->GetPropertyByNameInChain(name);
        auto* op = p ? CastField<FObjectProperty>(p) : nullptr;
        if (!op) return "n/a";
        UObject* o = op->GetPropertyValue(reinterpret_cast<uint8_t*>(w) + p->GetOffset_Internal());
        return alive(o) ? cmb_ascii(o->GetName()) : "null";
    }

    // WHAT: reads 3 axes of vec property name of o: shape 1 = PNum axes (raw fixed point), 2 = plain Double/Float axes (value as is). false when neither reads.
    // Call under seh_run. UNVERIFIED which shape CachedVelocity has; the shape is reported in the PROBE enemy_vel line.
    bool cmb_vec_read(UObject* o, const TCHAR* name, double out[3], int& shape)
    {
        FProperty* p = o->GetPropertyByNameInChain(name);
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return false;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(o) + p->GetOffset_Internal();
        double pv[3] = {0, 0, 0}, dv[3] = {0, 0, 0};
        const bool pnum = flight_vec3(base, sp->GetStruct(), pv);
        static const TCHAR* const names[3] = {STR("X"), STR("Y"), STR("Z")};
        bool dbl = true;
        for (int a = 0; a < 3 && dbl; ++a)
        {
            FProperty* f = sp->GetStruct()->FindProperty(FName(names[a], FNAME_Add));
            if (f && CastField<FDoubleProperty>(f)) dv[a] = *reinterpret_cast<const double*>(base + f->GetOffset_Internal());
            else if (f && CastField<FFloatProperty>(f)) dv[a] = *reinterpret_cast<const float*>(base + f->GetOffset_Internal());
            else dbl = false;
        }
        shape = cmb_vel_shape(pnum, dbl);
        if (!shape) return false;
        for (int i = 0; i < 3; ++i) out[i] = shape == 1 ? pv[i] : dv[i];
        return true;
    }

    // WHAT: PNum struct prop as value/65536, plain numeric prop as is. false when missing.
    bool cmb_scaled(UObject* o, const TCHAR* name, double& out)
    {
        FProperty* p = o->GetPropertyByNameInChain(name);
        double d = 0;
        if (!p || !cmb_num(o, name, d)) return false;
        out = CastField<FStructProperty>(p) ? d / kCmbRawPerM : d;
        return true;
    }

    // WHAT: enemy ship object of id, nullptr when missing/dead. Call under seh_run.
    //       dead (Health <= 0, corpse still listed by the game) -> nullptr with *dead = true.
    UObject* cmb_enemy(int id, bool* dead = nullptr)
    {
        const std::wstring cls = widen(kClsEnemyShip);
        UObject* o = id > 0 ? find_by_id(cls.c_str(), id) : nullptr;
        if (!alive(o)) return nullptr;
        int64_t hp = 0;
        if (cmb_hp(o, STR("Health"), hp) && hp <= 0) { if (dead) *dead = true; return nullptr; }
        return o;
    }

    // WHAT: enemy velocity (API 23). v_mps = per-frame metres * sim_tps_conv() (measured sim ticks/s, fallback 22.4); v_frame (optional) = per-frame metres. Read-only.
    // Probe line once per enemy id per 5 s.
    bool enemy_velocity(int id, double* v_mps, double* v_frame, std::wstring& err)
    {
        if (!combat_enabled()) { err = STR("combat off"); return false; }
        if (!v_mps) { err = STR("null out"); return false; }
        double raw[3], vf[3], vm[3];
        double sp = -1;
        int shape = 0;
        bool ok = false;
        if (!seh_run([&] {
                bool dead = false;
                UObject* o = cmb_enemy(id, &dead);
                if (!o) { err = dead ? STR("enemy dead") : STR("enemy not found"); return; }
                if (!cmb_vec_read(o, STR("CachedVelocity"), raw, shape)) { err = STR("CachedVelocity unreadable"); return; }
                sp = cmb_vel_mps(raw, kCmbRawPerM, sim_tps_conv(), vf, vm);   // UNVERIFIED: double shape also treated as raw units
                ok = sp >= 0;
            }))
        {
            err = STR("enemy_velocity faulted");
            module_disable(STR("combat"), STR("enemy_velocity fault"));
            return false;
        }
        if (!ok) return false;
        for (int i = 0; i < 3; ++i) { v_mps[i] = vm[i]; if (v_frame) v_frame[i] = vf[i]; }
        static std::map<int, ULONGLONG> last;
        static bool shape_logged = false;
        static std::mutex mx;
        std::lock_guard<std::mutex> lk(mx);
        const ULONGLONG now = GetTickCount64();
        auto it = last.find(id);
        if (it == last.end() || now - it->second >= 5000)
        {
            last[id] = now;
            auto f3 = [](const double* v) { char b[96]; std::snprintf(b, sizeof b, "%.4f,%.4f,%.4f", v[0], v[1], v[2]); return widen(std::string(b)); };
            char sb[32];
            std::snprintf(sb, sizeof sb, "%.3f", sp);
            msc_log(STR("PROBE enemy_vel id=") + widen(std::to_string(id)) + STR(" v_frame=") + f3(vf) + STR(" v_mps=") + f3(vm) + STR(" |v|=") + widen(std::string(sb)) +
                    STR(" shape=") + (shape == 1 ? STR("pnum") : STR("double")));
        }
        if (!shape_logged) { shape_logged = true; msc_log(std::wstring(STR("combat enemy velocity shape=")) + (shape == 1 ? STR("pnum (raw/65536 per frame)") : STR("double (treated as raw per frame)"))); }
        return true;
    }

    // WHAT: enemy aim calibration (API 23): out = {AlignmentToTarget, RequiredAlignmentToFire, AttackDistance m, ReleaseTargetDistance m}; missing prop -> -1. Read-only.
    bool enemy_aim(int id, double* out, std::wstring& err)
    {
        if (!combat_enabled()) { err = STR("combat off"); return false; }
        if (!out) { err = STR("null out"); return false; }
        bool found = false, dead = false;
        if (!seh_run([&] {
                UObject* o = cmb_enemy(id, &dead);
                if (!o) return;
                found = true;
                static const TCHAR* const names[4] = {STR("AlignmentToTarget"), STR("RequiredAlignmentToFire"), STR("AttackDistance"), STR("ReleaseTargetDistance")};
                for (int i = 0; i < 4; ++i) if (!cmb_scaled(o, names[i], out[i])) out[i] = -1;
            }))
        {
            err = STR("enemy_aim faulted");
            module_disable(STR("combat"), STR("enemy_aim fault"));
            return false;
        }
        if (!found) { err = dead ? STR("enemy dead") : STR("enemy not found"); return false; }
        return true;
    }

    // WHAT: enemy motion (API 24), read-only. out[14] = CurrentTask, attack_speed_mps, fatigue, fatigue_frames, flyaway_frames, avoid_frames,
    //       target_id, target rel metres x3 (same axes as MsEnemy.rel_m, kCmbNoLoc x3 if unreadable), pathfinding_rot_speed (per s, UNVERIFIED unit), attack_distance_m, avoidance_distance_m, avoidance_speed_mps (callers MUST pass 14).
    //       Missing prop -> -1. UNVERIFIED: TargetLocation (+0x1d4) is chunk-relative to TargetChunkLocation (+0x1e0), raw 65536/m like Location.
    bool enemy_motion(int sid, int id, double* out, std::wstring& err)
    {
        if (!combat_enabled()) { err = STR("combat off"); return false; }
        if (!out) { err = STR("null out"); return false; }
        double r[kCmbMotionSlots];
        cmb_motion_init(r);
        MsFlight f{};
        const bool pose = ship_flight_read(sid, FL_POSE, f) == 1 && (f.flags & FF_OBJ_POSE);
        bool found = false, dead = false, tloc = false;
        std::wstring shapes, cls;
        int tshape = 0;
        if (!seh_run([&] {
                UObject* o = cmb_enemy(id, &dead);
                if (!o) return;
                found = true;
                cls = o->GetClassPrivate()->GetName();
                auto shp =[&](const TCHAR* n) {
                    FProperty* p = o->GetPropertyByNameInChain(n);
                    shapes += STR(" ") + std::wstring(n) + STR(":") + (p ? p->GetClass().GetName() : std::wstring(STR("missing")));
                };
                static const TCHAR* const kShapeProps[] = {STR("CurrentTask"), STR("AttackSpeed"), STR("Fatigue"), STR("FatigueFrames"), STR("FlyAwayFrames"), STR("AvoidFrames"),
                                                           STR("TargetID"), STR("TargetLocation"), STR("TargetChunkLocation"), STR("PathfindingRotSpeed"), STR("AttackDistance"), STR("AvoidanceDistance"), STR("AvoidanceSpeed")};
                for (const TCHAR* n : kShapeProps) shp(n);
                double d = 0;
                FProperty* tp = o->GetPropertyByNameInChain(STR("CurrentTask"));
                if (tp && (CastField<FByteProperty>(tp) || CastField<FEnumProperty>(tp)) && tp->GetSize() == 1)
                    r[0] = *(reinterpret_cast<const uint8_t*>(o) + tp->GetOffset_Internal());
                else if (cmb_num(o, STR("CurrentTask"), d)) r[0] = d;
                if (cmb_scaled(o, STR("AttackSpeed"), d)) r[1] = cmb_rate_per_s(true, d, sim_tps_conv());
                static const TCHAR* const ints[5] = {STR("Fatigue"), STR("FatigueFrames"), STR("FlyAwayFrames"), STR("AvoidFrames"), STR("TargetID")};
                for (int i = 0; i < 5; ++i) if (cmb_num(o, ints[i], d)) r[2 + i] = d;   // int, or PNum raw (frames/ids are plain ints)
                if (cmb_scaled(o, STR("PathfindingRotSpeed"), d)) r[10] = cmb_rate_per_s(true, d, sim_tps_conv());
                if (cmb_scaled(o, STR("AttackDistance"), d)) r[11] = d;
                if (cmb_scaled(o, STR("AvoidanceDistance"), d)) r[12] = d;
                if (cmb_scaled(o, STR("AvoidanceSpeed"), d)) r[13] = cmb_rate_per_s(true, d, sim_tps_conv());
                double tl[3];
                int64_t tc[3] = {0, 0, 0};
                FProperty* cp = o->GetPropertyByNameInChain(STR("TargetChunkLocation"));
                auto* csp = cp ? CastField<FStructProperty>(cp) : nullptr;
                if (pose && cmb_vec_read(o, STR("TargetLocation"), tl, tshape) && csp && csp->GetStruct())
                {
                    const uint8_t* cb = reinterpret_cast<const uint8_t*>(o) + cp->GetOffset_Internal();
                    if (read_axis(cb, csp->GetStruct(), STR("X"), tc[0]) && read_axis(cb, csp->GetStruct(), STR("Y"), tc[1]) && read_axis(cb, csp->GetStruct(), STR("Z"), tc[2]))
                    {
                        double rel[3];
                        cmb_delta_m(f.ch, f.loc_raw, tc, tl, rel);
                        cmb_motion_set_target(r, true, rel);
                        tloc = true;
                    }
                }
            }))
        {
            err = STR("enemy_motion faulted");
            module_disable(STR("combat"), STR("enemy_motion fault"));
            return false;
        }
        if (!found) { err = dead ? STR("enemy dead") : STR("enemy not found"); return false; }
        for (int i = 0; i < kCmbMotionSlots; ++i) out[i] = r[i];
        static std::mutex mx;
        static std::set<int> seen;
        static ULONGLONG win = 0;
        static int cnt = 0;
        static bool shapes_logged = false, noloc_logged = false;
        std::lock_guard<std::mutex> lk(mx);
        if (!shapes_logged) { shapes_logged = true; msc_log(STR("combat enemy motion shapes") + shapes + STR(" tloc_shape=") + widen(std::to_string(tshape))); }
        if (!tloc && !noloc_logged) { noloc_logged = true; msc_log(std::wstring(STR("combat enemy motion target location unreadable (pose=")) + (pose ? STR("ok") : STR("no")) + STR(") -> 1e30")); }
        if (seen.insert(id).second)
        {
            const ULONGLONG now = GetTickCount64();
            if (now - win >= 10000) { win = now; cnt = 0; }
            if (cnt < 20)
            {
                ++cnt;
                char b[400];
                std::snprintf(b, sizeof b, " task=%.0f atk_mps=%.3f fat=%.0f/%.0f fly=%.0f avoid=%.0f tgt=%.0f tloc_rel=%.2f,%.2f,%.2f rot=%.4f atk_dist=%.2f avoid_dist=%.2f avoid_speed=%.3f",
                              r[0], r[1], r[2], r[3], r[4], r[5], r[6], cmb_motion_noloc(r) ? -1.0 : r[7], cmb_motion_noloc(r) ? -1.0 : r[8], cmb_motion_noloc(r) ? -1.0 : r[9], r[10], r[11], r[12], r[13]);
                msc_log(STR("PROBE enemy_motion id=") + widen(std::to_string(id)) + STR(" class=") + cls + widen(std::string(b)));
            }
        }
        return true;
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
        static const TCHAR* props[6] = { STR("BulletSpeed"), STR("BulletLifetime"), STR("TimeBetweenShots"), STR("MissileRange"), STR("Range"), STR("LaserReachMeters") };
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        for (UObject* ship : ships)
        {
            if (!alive(ship) || ship->GetName().rfind(STR("Default__"), 0) == 0) continue;
            if (!(ship_caps_read(ship) & kCapAttack)) continue;
            TArray<UObject*>* a = weapon_array(ship);
            if (!a || a->Num() <= 0 || a->Num() > kWeaponsMaxCount) continue;
            for (int32_t i = 0; i < a->Num(); ++i)
            {
                UObject* w = (*a)[i];
                if (!alive(w)) continue;
                const std::wstring cn = w->GetClassPrivate()->GetName();
                std::wstring line = STR("combat probe weapon ship=") + widen(std::to_string(obj_id(ship))) + STR(" ") + cn;
                for (const TCHAR* pn : props) line += std::wstring(STR(" ")) + pn + STR("=") + widen(cmb_prop_text(w, pn));
                if (cn.find(STR("Cannon")) != std::wstring::npos) line += STR(" BulletData=") + widen(cmb_obj_name(w, STR("BulletData")));
                msc_log(line);
            }
        }
        std::vector<UObject*> es;
        UObjectGlobals::FindAllOf(STR("EnemyShipObject"), es);
        int logged = 0;
        std::vector<std::wstring> seenCls;
        for (UObject* e : es)
        {
            if (!alive(e) || e->GetName().rfind(STR("Default__"), 0) == 0) continue;
            const std::wstring ecn = e->GetClassPrivate()->GetName();
            const bool newCls = std::find(seenCls.begin(), seenCls.end(), ecn) == seenCls.end();
            if (newCls) seenCls.push_back(ecn);
            // first 12 always, then the first of each new class, cap 40
            if (logged >= 40 || (logged >= 12 && !newCls)) continue;
            ++logged;
            auto num = [&](const TCHAR* pn) { std::string t = cmb_prop_text(e, pn); return widen(t == "n/a" ? std::string("?") : t); };
            int64_t hp = 0;
            const bool hok = cmb_hp(e, STR("Health"), hp);
            std::wstring task = STR("?");
            if (FProperty* tp = e->GetPropertyByNameInChain(STR("CurrentTask"))) task = widen(std::to_string(*(reinterpret_cast<const uint8_t*>(e) + tp->GetOffset_Internal())));
            // UNVERIFIED spawner prop name: try int SpawnerID/SpawnerId, then object Spawner
            std::wstring sp = STR("?");
            int32_t sv = 0;
            if (get_int_prop(e, STR("SpawnerID"), sv) || get_int_prop(e, STR("SpawnerId"), sv)) sp = widen(std::to_string(sv));
            else if (FProperty* op = e->GetPropertyByNameInChain(STR("Spawner")))
                if (auto* ofp = CastField<FObjectProperty>(op)) { UObject* so = ofp->GetPropertyValue(reinterpret_cast<uint8_t*>(e) + op->GetOffset_Internal()); if (alive(so)) sp = widen(std::to_string(obj_id(so))); }
            msc_log(STR("combat probe enemy id=") + widen(std::to_string(obj_id(e))) + STR(" class=") + e->GetClassPrivate()->GetName() + STR(" task=") + task +
                    STR(" hull=") + (hok ? widen(std::to_string(hp)) : std::wstring(STR("?"))) + STR("/") + num(STR("MaxHealth")) + STR(" spawner=") + sp +
                    STR(" alive=") + (hok && hp > 0 ? STR("1") : STR("0")));
        }
        std::vector<UObject*> sts;
        UObjectGlobals::FindAllOf(STR("EnemyStationObject"), sts);
        int sn = 0;
        std::wstring sids;
        for (UObject* s : sts)
        {
            if (!alive(s) || s->GetName().rfind(STR("Default__"), 0) == 0) continue;
            if (sn++ < 8) sids += (sids.empty() ? STR("") : STR(",")) + widen(std::to_string(obj_id(s)));
        }
        msc_log(STR("combat probe stations n=") + widen(std::to_string(sn)) + STR(" ids=") + sids);
        return true;
    }
}
