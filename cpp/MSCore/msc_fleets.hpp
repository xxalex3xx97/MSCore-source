// FILE: cpp/MSCore/msc_fleets.hpp
// WHAT: fleets_get(): FleetTag objects (id, parent, DisplayName) and per-ship FleetTagIDs, cached 3 s (native API 13, C exports MSCore_fleet_tags / MSCore_fleet_ships).
//       Also fleets_probe_text() (layout dump of FleetTagInput) and fleet_write() (stub until the probe is captured).
// WHY: the fleet tree lives in game memory only; mods need it without Lua struct access or ProcessEvent. Reads go straight to memory.
// SAFETY: game thread, reads only. FindAllOf and every per-object read run under seh_run; alive() guards each object.
//         A fault or a FleetTagIDs layout surprise returns false (caller turns the module off). Default__ objects and ids <= 0 are skipped. No ProcessEvent.
// CAPS: module 'fleets' (read, on) and 'fleets_write' (off, unverified). fleets_resolve registers them at startup. Kill switch: module_disable("fleets") in dllmain.cpp.
// USED BY: dllmain.cpp (fleets_resolve, selftest checks "fleets" and "fleets_write", MSCore_fleet_tags, MSCore_fleet_ships, Lua natives fleets_read, fleet_cmd, fleets_probe).
// DEPENDS ON: msc_cmd.hpp (alive, seh_run), msc_ue.hpp (obj_id, get_int_prop, read_text_prop), msc_contract.hpp (kClsFleetTag, kClsShip), msc_caps.hpp.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    constexpr int kFleetMaxTags = 8;
    constexpr ULONGLONG kFleetTtlMs = 3000;
    constexpr long long kFleetMaxAgeMs = 10000;

    // WHAT: one fleet tag row, 200 bytes, exported as is by MSCore_fleet_tags.
    struct MsFleetTag { int32_t id; int32_t parent; wchar_t name[96]; };
    static_assert(sizeof(MsFleetTag) == 200, "MsFleetTag layout is part of the C API");
    // WHAT: one ship row, 48 bytes. count = ids stored in tags (<= 8), total = real number of ids.
    struct MsFleetShip { int32_t ship; int32_t count; int32_t total; int32_t pad; int32_t tags[kFleetMaxTags]; };
    static_assert(sizeof(MsFleetShip) == 48, "MsFleetShip layout is part of the C API");

    struct FleetSnap { ULONGLONG at = 0; bool valid = false; std::vector<MsFleetTag> tags; std::vector<MsFleetShip> ships; };
    FleetSnap g_fleet_snap;
    std::mutex g_fleet_mx;

    void fleets_resolve()
    {
        static bool done = false; if (done) return; done = true;
        caps_add("fleets", true, "");
        caps_add("fleets_write", false, "unverified");
    }

    // WHAT: fresh scan of tags and ships. false on SEH fault or FleetTagIDs layout surprise.
    bool fleets_scan(std::vector<MsFleetTag>& tags, std::vector<MsFleetShip>& ships)
    {
        tags.clear(); ships.clear();
        std::vector<UObject*> all;
        if (!seh_run([&] { UObjectGlobals::FindAllOf(widen(kClsFleetTag).c_str(), all); })) return false;
        for (UObject* o : all)
        {
            MsFleetTag r{};
            bool keep = false;
            const bool ok = seh_run([&] {
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) return;
                r.id = obj_id(o);
                if (r.id <= 0) return;
                int32_t par = 0;
                get_int_prop(o, STR("ParentID"), par);
                r.parent = par;
                const std::wstring n = read_text_prop(o, STR("DisplayName"));
                size_t k = 0;
                for (; k < n.size() && k < 95; ++k)
                    r.name[k] = (n[k] == L'\t' || n[k] == L'\r' || n[k] == L'\n' || n[k] == L'|') ? L' ' : n[k];
                r.name[k] = 0;
                keep = true;
            });
            if (!ok) return false;
            if (keep) tags.push_back(r);
        }
        all.clear();
        if (!seh_run([&] { UObjectGlobals::FindAllOf(widen(kClsShip).c_str(), all); })) return false;
        bool bad = false;
        for (UObject* o : all)
        {
            MsFleetShip s{};
            bool keep = false;
            const bool ok = seh_run([&] {
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) return;
                s.ship = obj_id(o);
                if (s.ship <= 0) return;
                FProperty* p = o->GetPropertyByNameInChain(STR("FleetTagIDs"));
                auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
                if (!ap || !ap->GetInner() || !CastField<FIntProperty>(ap->GetInner())) { bad = true; return; }
                auto* arr = reinterpret_cast<TArray<int32_t>*>(reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal());
                const int n = arr->Num();
                if (n <= 0 || n > 1000) return;   // ships without a fleet are not listed
                s.total = n;
                for (int i = 0; i < n && i < kFleetMaxTags; ++i) s.tags[s.count++] = (*arr)[i];
                keep = true;
            });
            if (!ok || bad) return false;
            if (keep) ships.push_back(s);
        }
        return true;
    }

    // WHAT: snapshot copy. max_age_ms 0 = rescan now, else reuse a snapshot younger than min(max_age_ms, 10000). 1 ok, -2 module off, -1 fault (module turned off).
    int fleets_get(long long max_age_ms, std::vector<MsFleetTag>& t, std::vector<MsFleetShip>& s)
    {
        if (!module_ok(STR("fleets"))) return -2;
        std::lock_guard<std::mutex> lk(g_fleet_mx);
        const long long age = max_age_ms < 0 ? 0 : (max_age_ms > kFleetMaxAgeMs ? kFleetMaxAgeMs : max_age_ms);
        const ULONGLONG now = GetTickCount64();
        if (!g_fleet_snap.valid || age == 0 || now - g_fleet_snap.at >= static_cast<ULONGLONG>(age))
        {
            FleetSnap n;
            if (!fleets_scan(n.tags, n.ships))
            {
                module_disable(STR("fleets"), STR("fleets read fault or layout"));
                g_fleet_snap = FleetSnap();
                return -1;
            }
            n.valid = true; n.at = now;
            g_fleet_snap = std::move(n);
        }
        t = g_fleet_snap.tags; s = g_fleet_snap.ships;
        return 1;
    }

    inline std::string fleets_utf8(const wchar_t* w)
    {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 1) return std::string();
        std::string o(static_cast<size_t>(len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &o[0], len, nullptr, nullptr);
        o.resize(static_cast<size_t>(len - 1));
        return o;
    }

    // WHAT: "T\tid\tparent\tname\n" per tag first, then "S\tship\tcount\ttotal\tid,id,id\n" per ship.
    std::string fleets_text(const std::vector<MsFleetTag>& t, const std::vector<MsFleetShip>& s)
    {
        std::string o;
        for (const auto& r : t) o += "T\t" + std::to_string(r.id) + "\t" + std::to_string(r.parent) + "\t" + fleets_utf8(r.name) + "\n";
        for (const auto& r : s)
        {
            o += "S\t" + std::to_string(r.ship) + "\t" + std::to_string(r.count) + "\t" + std::to_string(r.total) + "\t";
            for (int i = 0; i < r.count; ++i) { if (i) o += ","; o += std::to_string(r.tags[i]); }
            o += "\n";
        }
        return o;
    }

    // WHAT: layout dump of /Script/MainSequence.FleetTagInput (own properties, functions with parameters). Caller wraps in seh_run.
    std::string fleets_probe_text()
    {
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.FleetTagInput"));
        if (!c) return "FleetTagInput class not found";
        auto line = [](FProperty* p) {
            return fleets_utf8(p->GetName().c_str()) + " " + fleets_utf8(p->GetClass().GetFName().ToString().c_str()) + " off=" + std::to_string(p->GetOffset_Internal()) + " size=" + std::to_string(p->GetSize());
        };
        std::string o = "CLS " + fleets_utf8(c->GetName().c_str()) + "\n";
        for (FProperty* p : c->ForEachProperty()) o += "PROP " + line(p) + "\n";
        for (UFunction* f : c->ForEachFunction())
        {
            o += "FN " + fleets_utf8(f->GetName().c_str()) + "\n";
            for (FProperty* p : f->ForEachProperty())
                o += "  P " + line(p) + (p->HasAnyPropertyFlags(CPF_ReturnParm) ? " ret" : "") + "\n";
        }
        return o;
    }

    // WHAT: probe the class; sends nothing. detail = FN count + first factory name. true when the class exists.
    bool fleets_write_selftest(std::string& detail)
    {
        const std::string p = fleets_probe_text();
        if (p.rfind("FleetTagInput class not found", 0) == 0) { detail = p; return false; }
        int fn = 0; std::string first;
        size_t pos = 0;
        while ((pos = p.find("FN ", pos)) != std::string::npos)
        {
            if (pos == 0 || p[pos - 1] == '\n')
            {
                if (!fn) first = p.substr(pos + 3, p.find('\n', pos) - pos - 3);
                ++fn;
            }
            pos += 3;
        }
        detail = std::to_string(fn) + " FleetTagInput functions, first " + (first.empty() ? std::string("-") : first);
        return true;
    }

    // WHAT: fleet write op. Phase 1: gated off until the FleetTagInput layout is verified.
    bool fleet_write(const std::string& op, int a, int b, const std::string& name, std::wstring& err)
    {
        (void)a; (void)b; (void)name;
        static const char* const ops[] = { "create", "rename", "delete", "assign", "unassign", "reparent" };
        bool known = false;
        for (const char* k : ops) if (op == k) known = true;
        if (!known) { err = STR("unknown fleet op"); return false; }
        if (!module_ok(STR("fleets_write"))) { err = STR("fleets_write off: FleetTagInput layout unverified (run fleets_probe + capture, see msc_fleets.hpp)"); return false; }
        err = STR("fleet op not implemented");   // ponytail: phase 2 fills the executor after the probe
        return false;
    }
}
