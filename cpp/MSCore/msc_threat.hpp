// FILE: cpp/MSCore/msc_threat.hpp
// WHAT: threat module (default ON): ship_threat = per ship: IsShipInCombat, hostile / far TargettedBy entries, nearest hostile distance (m). Read-only.
// WHY: moved out of Fleet (tgt_read_fn) so the game names (IsShipInCombat, TargettedBy, Owner, Faction) live only here.
// READ: IsShipInCombat via ProcessEvent (bool return); TargettedBy array of object pointers (vault game-map-combat.md 3.2: +0x880, Num +0x888);
//       each entry refetched/checked alive, distance = chunk + Location delta (cmb_delta_m) from the ship to the entry.
// SAFETY: off when Mods\MSCore\threat_off.txt exists (g_threat_off). Whole read under seh_run; a fault turns module 'threat' off. Writes nothing.
// CAPS: module 'threat' (threat_resolve). Kill switch: module_disable("threat").
// USED BY: dllmain.cpp (threat_resolve, check_one "threat", MSCore_ship_threat). Test: tests/test_threat_logic.cpp.
// UNVERIFIED: TargettedBy element type (assumed object pointer, logged once: type + element size); range = distance from ship to the targeting object, metres.
// DEPENDS ON: msc_threat_logic.hpp, msc_combat_logic.hpp (cmb_delta_m), msc_flight.hpp (flight_ship, flight_loc), msc_warp.hpp (get_chunk), msc_caps.hpp, msc_ue.hpp.
#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "msc_combat_logic.hpp"
#include "msc_threat_logic.hpp"

namespace
{
    std::atomic<bool> g_threat_off{false};   // marker Mods\MSCore\threat_off.txt (default ON)

    void threat_resolve() { static bool done = false; if (done) return; done = true; caps_add("threat", true, ""); }
    bool threat_enabled() { return !g_threat_off.load() && module_ok(STR("threat")); }

    struct ThrOut { bool in_combat = false; ThrCount c; };

    // WHAT: log once per ship when (hostile, far, in_combat, nearest) changes.
    void threat_note(int sid, const ThrOut& o)
    {
        static std::mutex mx;
        static std::map<int, std::wstring> last;
        const std::wstring line = STR("threat ship=") + widen(std::to_string(sid)) + STR(" hostile=") + widen(std::to_string(o.c.hostile)) + STR(" far=") + widen(std::to_string(o.c.far_n)) +
                                  STR(" in_combat=") + (o.in_combat ? STR("1") : STR("0")) + STR(" nearest=") + widen(std::to_string(static_cast<long long>(o.c.nearest_m)));
        std::lock_guard<std::mutex> lk(mx);
        auto it = last.find(sid);
        if (it != last.end() && it->second == line) return;
        last[sid] = line;
        msc_log(line);
    }

    // WHAT: raw read for ship sid (SEH inside). false on unknown ship / unreadable TargettedBy; faults turn the module off.
    bool threat_read(int sid, double range_m, ThrOut& out, bool quiet = false)
    {
        UObject* ship = flight_ship(sid);
        if (!ship) return false;
        bool arr_ok = false;
        if (!seh_run([&] {
                if (UFunction* fn = ship->GetFunctionByNameInChain(STR("IsShipInCombat")))
                {
                    FProperty* rp = fn->GetReturnProperty();
                    if (rp && CastField<FBoolProperty>(rp))
                    {
                        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
                        ship->ProcessEvent(fn, buf.data());
                        out.in_combat = buf[rp->GetOffset_Internal()] != 0;
                    }
                }
                FProperty* tp = ship->GetPropertyByNameInChain(STR("TargettedBy"));
                auto* ap = (tp && CastField<FArrayProperty>(tp)) ? CastField<FArrayProperty>(tp) : nullptr;
                auto* arr = ap ? ship->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("TargettedBy")) : nullptr;
                if (!arr) return;
                arr_ok = true;
                FProperty* inner = ap->GetInner();
                const bool objs = inner && CastField<FObjectProperty>(inner);
                const int32_t stride = inner ? inner->GetElementSize() : 0;
                static bool logged = false;
                if (!logged) { logged = true; msc_log(STR("threat TargettedBy element type=") + (inner ? inner->GetClass().GetName() : std::wstring(STR("?"))) + STR(" size=") + widen(std::to_string(stride))); }
                double sp[3] = {0, 0, 0};
                int64_t sc[3] = {0, 0, 0};
                const bool pose = flight_loc(ship, sp) && get_chunk(ship, sc);
                const int32_t n = arr->Num() < 0 ? 0 : (arr->Num() > 100000 ? 100000 : arr->Num());
                UObject* so = get_obj_prop(ship, STR("Owner"));
                UObject* sf = get_obj_prop(ship, STR("Faction"));
                for (int32_t i = 0; i < n; ++i)
                {
                    UObject* e = (objs && stride > 0) ? *reinterpret_cast<UObject**>(&(*arr)[0] + static_cast<size_t>(i) * static_cast<size_t>(stride)) : nullptr;
                    const bool live = e && alive(e);
                    UObject* eo = live ? get_obj_prop(e, STR("Owner")) : nullptr;
                    UObject* ef = live ? get_obj_prop(e, STR("Faction")) : nullptr;
                    const bool same_side = (alive(eo) && eo == so) || (alive(ef) && ef == sf);
                    double ep[3] = {0, 0, 0}, rel[3];
                    int64_t ec[3] = {0, 0, 0};
                    const double d = (live && pose && flight_loc(e, ep) && get_chunk(e, ec)) ? cmb_delta_m(sc, sp, ec, ep, rel) : -1.0;
                    thr_add(out.c, thr_kind(live, same_side, objs, d, range_m), d);
                }
            }))
        {
            module_disable(STR("threat"), STR("ship_threat fault"));
            return false;
        }
        if (!arr_ok && !out.in_combat) return false;
        if (!quiet) threat_note(sid, out);
        return true;
    }

    // WHAT: API ship_threat. 1 ok, 0 fail or threat off.
    int ship_threat(int sid, double range_m, int* hostile_n, int* far_n, int* in_combat, double* nearest_d_m)
    {
        if (!threat_enabled()) return 0;
        ThrOut o;
        if (!threat_read(sid, range_m, o)) return 0;
        if (hostile_n) *hostile_n = o.c.hostile;
        if (far_n) *far_n = o.c.far_n;
        if (in_combat) *in_combat = o.in_combat ? 1 : 0;
        if (nearest_d_m) *nearest_d_m = o.c.nearest_m;
        return 1;
    }

    // WHAT: read-only selftest: logs counts for every ship that has a hostile entry or is in combat.
    bool threat_selftest(std::string& detail)
    {
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        int n = 0, flagged = 0;
        for (UObject* s : ships)
        {
            if (!alive(s) || s->GetName().rfind(STR("Default__"), 0) == 0) continue;
            const int sid = obj_id(s);
            ThrOut o;
            if (sid <= 0 || !threat_read(sid, 3000.0, o, true)) continue;
            ++n;
            if (o.c.hostile > 0 || o.in_combat)
            {
                ++flagged;
                msc_log(STR("threat check ship=") + widen(std::to_string(sid)) + STR(" hostile=") + widen(std::to_string(o.c.hostile)) + STR(" far=") + widen(std::to_string(o.c.far_n)) +
                        STR(" in_combat=") + (o.in_combat ? STR("1") : STR("0")) + STR(" nearest=") + widen(std::to_string(static_cast<long long>(o.c.nearest_m))));
            }
        }
        detail = "ships=" + std::to_string(n) + " threatened=" + std::to_string(flagged);
        if (n == 0) { detail = "SKIP no readable ships"; return false; }
        return true;
    }
}
