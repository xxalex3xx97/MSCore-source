// FILE: cpp/MSCore/msc_stnames.hpp
// WHAT: stations_read(rows): id + DisplayName of every live StationObject (native API 12, C export MSCore_stations).
// WHY: mods need the station names (UI lists, labels) without Lua struct access or ProcessEvent. DisplayName is read straight from memory.
// SAFETY: game thread, reads only. The FindAllOf and every per-station read run under seh_run; alive() guards each object.
//         A fault returns false (caller turns the module off). Default__ objects and ids <= 0 are skipped. No ProcessEvent.
// CAPS: module 'stations' (stnames_resolve registers it at startup). Runtime kill switch: module_disable("stations") in dllmain.cpp.
// USED BY: dllmain.cpp (stnames_resolve, selftest check "stations", MSCore_stations).
// DEPENDS ON: msc_cmd.hpp (alive, seh_run), msc_ue.hpp (obj_id, read_text_prop), msc_contract.hpp (kClsStation), msc_caps.hpp.
#pragma once
#include <cstdint>
#include <vector>

namespace
{
    // WHAT: one station row, 200 bytes, exported as is by MSCore_stations.
    struct MsStationName { int32_t id; int32_t pad; wchar_t name[96]; };
    static_assert(sizeof(MsStationName) == 200, "MsStationName layout is part of the C API");

    void stnames_resolve() { static bool done = false; if (done) return; done = true; caps_add("stations", true, ""); }

    // WHAT: fills rows with every live station (id > 0, name may be empty). false when a read faulted.
    bool stations_read(std::vector<MsStationName>& rows)
    {
        rows.clear();
        std::vector<UObject*> all;
        if (!seh_run([&] { UObjectGlobals::FindAllOf(widen(kClsStation).c_str(), all); })) return false;
        for (UObject* o : all)
        {
            MsStationName r{};
            bool keep = false;
            const bool ok = seh_run([&] {
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) return;
                r.id = obj_id(o);
                if (r.id <= 0) return;
                const std::wstring n = read_text_prop(o, STR("DisplayName"));
                size_t k = 0;
                for (; k < n.size() && k < 95; ++k)
                    r.name[k] = (n[k] == L'\t' || n[k] == L'\r' || n[k] == L'\n') ? L' ' : n[k];
                r.name[k] = 0;
                keep = true;
            });
            if (!ok) return false;
            if (keep) rows.push_back(r);
        }
        return true;
    }
}
