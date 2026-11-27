// FILE: cpp/MSCore/msc_stshield.hpp
// WHAT: station shield module (default ON): MSCore_station_zones rows (id, sys, centre metres, extent, range) of player stations.
// WHY: mods keep mining ships out of a station shield bubble (fixed radius decided by the mod). extent_m and range_m stay -1 (not read).
// READ: FindAllOf StationObject cached 2 s (stz_scan). pos = rockreg frame: chunk (get_chunk) * RR_CHUNK_M + Location raw / 65536 (sec_axes Location). No Location = no row.
// SAFETY: off when Mods\MSCore\stshield_off.txt exists (g_stsh_off). Every raw access under seh_run; a fault turns module 'stshield' off. Writes nothing.
// CAPS: module 'stshield' (stshield_resolve). Kill switch: module_disable("stshield").
// USED BY: dllmain.cpp (stshield_resolve, check_one "stshield", MSCore_station_zones). Test: tests/test_stshield_logic.cpp.
// DEPENDS ON: msc_stshield_logic.hpp, msc_rockreg_logic.hpp (rr_abs_m), msc_warp.hpp (get_chunk, sec_axes, sys_id), msc_cmd.hpp, msc_caps.hpp, msc_ue.hpp, msc_contract.hpp (kClsStation).
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
    std::mutex g_stz_mx;                      // guards the cache (stz_scan runs under it from station_zones)
    std::vector<MsStationZone> g_stz_rows;
    ULONGLONG g_stz_at = 0;
    bool g_stz_valid = false;

    void stshield_resolve() { static bool done = false; if (done) return; done = true; caps_add("stshield", true, ""); }
    bool stshield_enabled() { return !g_stsh_off.load() && module_ok(STR("stshield")); }

    // WHAT: fresh scan of stations (rows) false on a SEH fault only. Zero stations is normal.
    bool stz_scan(std::vector<MsStationZone>& rows)
    {
        rows.clear();
        std::vector<UObject*> sts;
        if (!seh_run([&] { UObjectGlobals::FindAllOf(widen(kClsStation).c_str(), sts); })) return false;
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
                double loc[3] = {0, 0, 0};
                if (!sec_axes(o, STR("Location"), raw)) return;
                for (int i = 0; i < 3; ++i) loc[i] = static_cast<double>(raw[i]);
                rr_abs_m(ch, loc, kStzRawPerM, r.pos);
                r.extent_m = -1; r.range_m = -1;
                keep = sz_is_player_station(std::wstring(o->GetClassPrivate()->GetName()));
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
        detail = "stations=" + std::to_string(rows.size()) + "";
        return true;
    }
}
