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

    struct EnemyRaw { int id; int sys; int hull; int flags; int64_t ch[3]; double loc[3]; };
    struct EnemyPose { int64_t ch[3]; double loc[3]; bool big_known = false; bool big = false; };
    std::map<int, EnemyPose> g_enemy_pose;   // last scan pose per enemy id, pruned to ids seen
    std::mutex g_pose_mx;
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
                    big = cmb_class_big(o->GetClassPrivate()->GetName());
                    int32_t mh = 0;   // UNVERIFIED: property name MaxHealth (EnemyBase +0x11c i32)
                    if (!big && get_int_prop(o, STR("MaxHealth"), mh) && mh >= kCmbBigMaxHealth) big = true;
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
                r.flags = cmb_enemy_flags(hp > 0, moved, task, big);
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
            int32_t hp = 0;
            const bool hok = get_int_prop(e, STR("Health"), hp);
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
