// FILE: cpp/MSCore/msc_stnames.hpp
// WHAT: stations_read(rows): id + DisplayName of every live StationObject (native API 12, C export MSCore_stations).
// WHY: mods need the station names (UI lists, labels) without Lua struct access or ProcessEvent. The name is read straight from memory (candidates in kStnCands).
// SAFETY: game thread, reads only. The FindAllOf and every per-station read run under seh_run; alive() guards each object.
//         A fault returns false (caller turns the module off). Default__ objects and ids <= 0 are skipped. No ProcessEvent.
// CAPS: module 'stations' (stnames_resolve registers it at startup). Runtime kill switch: module_disable("stations") in dllmain.cpp.
// USED BY: dllmain.cpp (stnames_resolve, selftest check "stations", MSCore_stations).
// DEPENDS ON: msc_cmd.hpp (alive, seh_run), msc_ue.hpp (obj_id), msc_contract.hpp (kClsStation), msc_caps.hpp.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    // WHAT: one station row, 200 bytes, exported as is by MSCore_stations.
    struct MsStationName { int32_t id; int32_t pad; wchar_t name[96]; };
    static_assert(sizeof(MsStationName) == 200, "MsStationName layout is part of the C API");

    void stnames_resolve() { static bool done = false; if (done) return; done = true; caps_add("stations", true, ""); }

    std::mutex g_stn_mx;      // guards g_stn_src (written by stn_name on the game thread, read by the selfcheck)
    std::wstring g_stn_src;   // which candidate hit first ("DisplayName", "Blueprint.DisplayName", ...)
    std::wstring stn_src_get() { std::lock_guard<std::mutex> lk(g_stn_mx); return g_stn_src; }

    // WHAT: where a station name may live. inner==nullptr: plain property on the station; else field `inner` inside the struct property `outer`.
    struct StnCand { const TCHAR* outer; const TCHAR* inner; };
    const StnCand kStnCands[] = { {STR("DisplayName"), nullptr}, {STR("Blueprint"), STR("DisplayName")}, {STR("StationName"), nullptr}, {STR("CustomName"), nullptr} };

    // WHAT: text of a Text/Str/Name property at address a ("" for other types or unset). Reads only; caller holds SEH.
    std::wstring stn_text_at(FProperty* p, const uint8_t* a)
    {
        const std::wstring c = p->GetClass().GetFName().ToString();
        if (c == STR("TextProperty")) { auto* t = reinterpret_cast<const FText*>(a); return t->GetTextData() ? t->ToString() : STR(""); }
        if (c == STR("StrProperty")) return std::wstring(**reinterpret_cast<const FString*>(a));
        if (c == STR("NameProperty")) { const std::wstring n = reinterpret_cast<const FName*>(a)->ToString(); return n == STR("None") ? STR("") : n; }
        return STR("");
    }

    std::wstring stn_cand(UObject* o, const StnCand& c)
    {
        FProperty* p = o->GetPropertyByNameInChain(c.outer);
        if (!p) return STR("");
        const uint8_t* base = reinterpret_cast<const uint8_t*>(o) + p->GetOffset_Internal();
        if (!c.inner) return stn_text_at(p, base);
        auto* sp = CastField<FStructProperty>(p);
        if (!sp || !sp->GetStruct()) return STR("");
        FProperty* f = sp->GetStruct()->FindProperty(FName(c.inner, FNAME_Add));
        return f ? stn_text_at(f, base + f->GetOffset_Internal()) : STR("");
    }

    // WHAT: first non-empty candidate name; remembers which candidate hit first in g_stn_src.
    std::wstring stn_name(UObject* o)
    {
        for (const StnCand& c : kStnCands)
        {
            std::wstring n = stn_cand(o, c);
            if (n.empty()) continue;
            {
                std::lock_guard<std::mutex> lk(g_stn_mx);
                if (g_stn_src.empty()) g_stn_src = c.inner ? std::wstring(c.outer) + STR(".") + c.inner : std::wstring(c.outer);
            }
            return n;
        }
        return STR("");
    }

    // WHAT: one-shot selfcheck diagnostic: logs station objects, name-like properties, and every candidate value. Never called from MSCore_stations.
    void stn_probe_log()
    {
        static bool done = false; if (done) return; done = true;
        seh_run([&] {
            std::vector<UObject*> all;
            UObjectGlobals::FindAllOf(widen(kClsStation).c_str(), all);
            int n = 0;
            for (UObject* o : all)
            {
                if (n >= 3) break;
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) continue;
                msc_log(STR("stations probe object ") + o->GetName() + STR(" id=") + std::to_wstring(obj_id(o)));
                auto has = [](const std::wstring& s) { return s.find(STR("Name")) != std::wstring::npos || s.find(STR("Label")) != std::wstring::npos || s.find(STR("Title")) != std::wstring::npos; };
                if (n == 0)
                {
                    for (UStruct* u = o->GetClassPrivate(); u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
                        for (FProperty* p : u->ForEachProperty())
                            if (has(p->GetName()))
                                msc_log(STR("stations probe prop ") + o->GetClassPrivate()->GetName() + STR(".") + p->GetName() + STR(" type=") + p->GetClass().GetFName().ToString() +
                                        STR(" off=") + std::to_wstring(p->GetOffset_Internal()) + STR(" declared in ") + u->GetName());
                    FProperty* bp = o->GetPropertyByNameInChain(STR("Blueprint"));
                    auto* sp = bp ? CastField<FStructProperty>(bp) : nullptr;
                    if (!sp || !sp->GetStruct()) msc_log(STR("stations probe: no struct property Blueprint on the station"));
                    else
                        for (FProperty* p : sp->GetStruct()->ForEachProperty())
                            if (has(p->GetName()))
                                msc_log(STR("stations probe prop Blueprint.") + p->GetName() + STR(" type=") + p->GetClass().GetFName().ToString() +
                                        STR(" off=") + std::to_wstring(p->GetOffset_Internal()) + STR(" declared in ") + sp->GetStruct()->GetName());
                }
                for (const StnCand& c : kStnCands)
                {
                    FProperty* p = o->GetPropertyByNameInChain(c.outer);
                    const std::wstring nm = c.inner ? std::wstring(c.outer) + STR(".") + c.inner : std::wstring(c.outer);
                    msc_log(STR("stations probe cand ") + nm + STR(" outerType=") + (p ? p->GetClass().GetFName().ToString() : std::wstring(STR("MISSING"))) +
                            STR(" value='") + stn_cand(o, c) + STR("'"));
                }
                // ProcessEvent diagnostic, default OFF: msc_log(STR("stations probe GetDisplayName=") + call_text(o, STR("GetDisplayName")));
                ++n;
            }
        });
    }

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
                // pad flags for the Fleet mod: bit1 = flag valid, bit0 = class is the player station (kClsPlayerStation)
                const std::wstring cls(o->GetClassPrivate()->GetName());
                r.pad = 2 | (cls == widen(kClsPlayerStation) ? 1 : 0);
                name_field_copy(r.name, 96, stn_name(o));   // name from kStnCands
                keep = true;
            });
            if (!ok) return false;
            if (keep) rows.push_back(r);
        }
        return true;
    }
}
