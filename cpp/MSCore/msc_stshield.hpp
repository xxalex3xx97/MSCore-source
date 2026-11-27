// FILE: cpp/MSCore/msc_stshield.hpp
// WHAT: station shield module (default ON), PROBE only: MSCore_station_zones rows (id, sys, centre metres, extent, range) + one-shot property dumps of StationObject and ShieldGeneratorObject.
// WHY: mods keep mining ships out of a station shield zone. The shield range field is not known yet (no vault note has it); the PROBE lines name it. range_m stays -1 until a probe shows the field. No number is guessed.
// READ: FindAllOf StationObject + ShieldGeneratorObject cached 2 s (stz_scan). pos = rockreg frame: chunk (get_chunk) * RR_CHUNK_M + Location raw / 65536 (sec_axes Location, flight_loc fallback).
//       Per station id once: PROBE stshield id= sys= pos= gens=<n> class=..., PROBE stshield_prop (every readable StationObject property), and for each linked generator (max 8) its class and all properties.
//       A generator is linked to a station when one of its object properties points to that station. Property values: scalars, names, strings, object (class#id), struct fields (2 levels), array Num.
// SAFETY: off when Mods\MSCore\stshield_off.txt exists (g_stsh_off). Every raw access under seh_run; a fault turns module 'stshield' off. Writes nothing. PROBE lines only show with Mods\MSCore\debug_probes.txt.
// CAPS: module 'stshield' (stshield_resolve). Kill switch: module_disable("stshield").
// USED BY: dllmain.cpp (stshield_resolve, check_one "stshield", MSCore_station_zones). Test: tests/test_stshield_logic.cpp.
// DEPENDS ON: msc_stshield_logic.hpp, msc_rockreg_logic.hpp (rr_abs_m), msc_warp.hpp (get_chunk, sec_axes, sys_id), msc_flight.hpp (flight_loc), msc_cmd.hpp, msc_caps.hpp, msc_ue.hpp, msc_contract.hpp (kClsStation).
#pragma once
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "msc_rockreg_logic.hpp"
#include "msc_stshield_logic.hpp"

namespace
{
    constexpr ULONGLONG kStzCacheMs = 2000;   // snapshot max age (stations do not move)
    constexpr double kStzRawPerM = 65536.0;   // same as RR_RAW_PER_M

    std::atomic<bool> g_stsh_off{false};      // marker Mods\MSCore\stshield_off.txt (default ON)
    std::mutex g_stz_mx;                      // guards the cache and g_stz_seen (stz_scan runs under it from station_zones)
    std::vector<MsStationZone> g_stz_rows;
    ULONGLONG g_stz_at = 0;
    bool g_stz_valid = false;
    std::set<int> g_stz_seen;                 // station ids already probed

    void stshield_resolve() { static bool done = false; if (done) return; done = true; caps_add("stshield", true, ""); }
    bool stshield_enabled() { return !g_stsh_off.load() && module_ok(STR("stshield")); }

    // WHAT: readable text of one property value of container c (depth limits struct recursion). Reads only; caller holds SEH.
    std::wstring stz_val(FProperty* p, const uint8_t* c, int depth)
    {
        const uint8_t* b = c + p->GetOffset_Internal();
        if (auto* bp = CastField<FBoolProperty>(p)) return bp->GetPropertyValueInContainer(const_cast<uint8_t*>(c)) ? STR("true") : STR("false");
        if (CastField<FFloatProperty>(p)) return std::to_wstring(*reinterpret_cast<const float*>(b));
        if (CastField<FDoubleProperty>(p)) return std::to_wstring(*reinterpret_cast<const double*>(b));
        if (CastField<FIntProperty>(p)) return std::to_wstring(*reinterpret_cast<const int32_t*>(b));
        if (CastField<FInt64Property>(p)) return std::to_wstring(*reinterpret_cast<const int64_t*>(b));
        if (CastField<FByteProperty>(p) || CastField<FEnumProperty>(p)) return std::to_wstring(static_cast<int>(*b));
        if (CastField<FNameProperty>(p)) return reinterpret_cast<const FName*>(b)->ToString();
        if (p->GetClass().GetFName().ToString() == STR("StrProperty")) return std::wstring(**reinterpret_cast<const FString*>(b));
        if (CastField<FObjectProperty>(p))
        {
            UObject* t = *reinterpret_cast<UObject* const*>(b);
            if (!t) return STR("null");
            if (!alive(t)) return STR("dead");
            return std::wstring(t->GetClassPrivate()->GetName()) + STR("#") + std::to_wstring(obj_id(t));
        }
        if (CastField<FArrayProperty>(p)) return STR("array Num=") + std::to_wstring(reinterpret_cast<const TArray<uint8_t>*>(b)->Num());
        if (auto* sp = CastField<FStructProperty>(p))
        {
            if (!sp->GetStruct()) return STR("struct?");
            if (depth >= 2) return STR("{...}");
            std::wstring s = STR("{");
            for (FProperty* q : sp->GetStruct()->ForEachProperty()) s += q->GetName() + STR("=") + stz_val(q, b, depth + 1) + STR(" ");
            return s + STR("}");
        }
        return STR("?type=") + p->GetClass().GetFName().ToString();
    }

    // WHAT: logs every property of o (whole class chain) as PROBE stshield_prop <tag> name= type= val=. Caller holds SEH.
    void stz_dump(UObject* o, const std::wstring& tag)
    {
        const uint8_t* c = reinterpret_cast<const uint8_t*>(o);
        for (FProperty* q : o->GetClassPrivate()->ForEachPropertyInChain())
            msc_log(STR("PROBE stshield_prop ") + tag + STR(" name=") + q->GetName() + STR(" type=") + q->GetClass().GetFName().ToString() + STR(" val=") + stz_val(q, c, 0));
    }

    // WHAT: true when any object property of gen points to st. Caller holds SEH.
    bool stz_linked(UObject* gen, UObject* st)
    {
        const uint8_t* c = reinterpret_cast<const uint8_t*>(gen);
        for (FProperty* q : gen->GetClassPrivate()->ForEachPropertyInChain())
            if (CastField<FObjectProperty>(q) && *reinterpret_cast<UObject* const*>(c + q->GetOffset_Internal()) == st) return true;
        return false;
    }

    // WHAT: fresh scan of stations (rows) with the one-shot probes. false on a SEH fault only. Zero stations is normal.
    bool stz_scan(std::vector<MsStationZone>& rows)
    {
        rows.clear();
        std::vector<UObject*> sts, gens;
        if (!seh_run([&] { UObjectGlobals::FindAllOf(widen(kClsStation).c_str(), sts); })) return false;
        if (!seh_run([&] { UObjectGlobals::FindAllOf(STR("ShieldGeneratorObject"), gens); })) return false;
        for (UObject* o : sts)
        {
            MsStationZone r{};
            bool keep = false;
            const bool ok = seh_run([&] {
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) return;
                r.id = obj_id(o);
                if (r.id <= 0) return;
                int64_t ch[3] = {0, 0, 0}, raw[3] = {0, 0, 0};
                if (!get_chunk(o, ch)) return;
                r.sys = sys_id(o);
                double loc[3] = {0, 0, 0}, fl[3] = {0, 0, 0};
                const bool sec = sec_axes(o, STR("Location"), raw);
                if (sec) for (int i = 0; i < 3; ++i) loc[i] = static_cast<double>(raw[i]);
                const bool fok = flight_loc(o, fl);
                if (!sec && !fok) return;
                if (!sec) for (int i = 0; i < 3; ++i) loc[i] = fl[i];
                rr_abs_m(ch, loc, kStzRawPerM, r.pos);
                r.extent_m = -1; r.range_m = -1;
                if (g_stz_seen.insert(r.id).second)
                {
                    std::vector<UObject*> mine;
                    for (UObject* g : gens) if (alive(g) && stz_linked(g, o)) mine.push_back(g);
                    std::wstring flt;
                    if (fok && sec) flt = STR(" flight_loc=") + std::to_wstring(fl[0]) + STR(",") + std::to_wstring(fl[1]) + STR(",") + std::to_wstring(fl[2]);
                    msc_log(STR("PROBE stshield id=") + widen(std::to_string(r.id)) + STR(" sys=") + widen(std::to_string(r.sys)) + STR(" class=") + o->GetClassPrivate()->GetName() +
                            STR(" pos=") + std::to_wstring(r.pos[0]) + STR(",") + std::to_wstring(r.pos[1]) + STR(",") + std::to_wstring(r.pos[2]) +
                            STR(" loc_src=") + (sec ? STR("Location") : STR("GetLocation")) + flt +
                            STR(" ch=") + widen(std::to_string(ch[0]) + "," + std::to_string(ch[1]) + "," + std::to_string(ch[2])) +
                            STR(" gens=") + widen(std::to_string(mine.size())) + STR(" gens_total=") + widen(std::to_string(gens.size())) + STR(" range=-1"));
                    stz_dump(o, STR("station=") + widen(std::to_string(r.id)));
                    int n = 0;
                    for (UObject* g : mine)
                    {
                        if (++n > 8) break;
                        stz_dump(g, STR("gen=") + widen(std::to_string(obj_id(g))) + STR(" station=") + widen(std::to_string(r.id)) + STR(" class=") + g->GetClassPrivate()->GetName());
                    }
                    // no generator points at any station: dump the first generator once so the link field can be found
                    static bool anyGenDumped = false;
                    if (mine.empty() && !gens.empty() && !anyGenDumped && alive(gens[0]))
                    {
                        anyGenDumped = true;
                        stz_dump(gens[0], STR("gen=unlinked class=") + gens[0]->GetClassPrivate()->GetName());
                    }
                }
                keep = sz_is_player_station(std::wstring(o->GetClassPrivate()->GetName()));   // probe above logs every station, rows are player stations only
            });
            if (!ok) return false;
            if (keep) rows.push_back(r);
        }
        return true;
    }

    // WHAT: station zones (all systems), ordered by id, copied to out (max rows). Returns the total count.
    // RETURNS: -2 module off, -1 scan fault (module turned off).
    int station_zones(MsStationZone* out, int max)
    {
        if (!stshield_enabled()) return -2;
        std::vector<MsStationZone> rows;
        {
            std::lock_guard<std::mutex> lk(g_stz_mx);
            const ULONGLONG now = GetTickCount64();
            if (!g_stz_valid || now - g_stz_at >= kStzCacheMs)
            {
                std::vector<MsStationZone> n;
                if (!stz_scan(n))
                {
                    module_disable(STR("stshield"), STR("station scan fault"));
                    g_stz_valid = false; g_stz_rows.clear();
                    return -1;
                }
                g_stz_rows = sz_filter(n, -1); g_stz_valid = true; g_stz_at = now;
            }
            rows = g_stz_rows;
        }
        if (out && max > 0) for (size_t i = 0; i < rows.size() && static_cast<int>(i) < max; ++i) out[i] = rows[i];
        return static_cast<int>(rows.size());
    }

    // WHAT: read-only selftest: the scan must not fault; zero stations is fine (none loaded).
    bool stshield_selftest(std::string& detail)
    {
        std::lock_guard<std::mutex> lk(g_stz_mx);
        std::vector<MsStationZone> rows;
        if (!stz_scan(rows)) { detail = "station scan faulted"; return false; }
        detail = "stations=" + std::to_string(rows.size()) + " (range unknown until probe)";
        return true;
    }
}
