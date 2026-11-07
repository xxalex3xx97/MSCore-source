// FILE: cpp/MSCore/msc_station.hpp
// WHAT: read_station(id): the live Blueprint (Blocks + Machines) of one StationObject as one tab-separated text (native API 4, Lua only).
//       Lines: B\tclass\tgx\tgy\th\trot   and   M\tid\tcls\tx\ty\th\trot\tcfg
//       cfg = lowercase hex of SerializedMachineConfig, "" for an empty array, "-" when the field is unreadable.
//       Trailing _C is stripped from class names.
// WHY: Lua struct access to Blueprint crashes UE4SS, so the walk is native. Ported from MiningBenchMod mb_bpprobe.hpp
//      (pp_read_blocks, pp_read_bp_machines). Fields are found by property name, never by fixed offset.
// DEPENDS ON: msc_cmd.hpp (alive), msc_ue.hpp (obj_id). The caller wraps read_station in factory_guard (msc_factory.hpp).
// USED BY: dllmain.cpp lua_read_station (MSCoreNative_read_station); parsed by mscore_factory.lua parse_station.
// SAFETY: game thread only, reads only (no writes). Every game pointer is checked by alive() before use.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    constexpr size_t kMaxStationBytes = 64u * 1024u * 1024u;   // WHY: cap on the text returned to Lua

    // WHAT: property by name up the class chain (stops at Object). nullptr when missing.
    FProperty* st_field(UStruct* st, const TCHAR* name)
    {
        for (UStruct* u = st; u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty()) if (p->GetName() == name) return p;
        return nullptr;
    }

    // WHAT: reads an IntProperty into v; false when missing or not an int.
    bool st_int(UStruct* st, const uint8_t* base, const TCHAR* name, int& v)
    {
        FProperty* p = st_field(st, name);
        if (!p || !CastField<FIntProperty>(p)) return false;
        v = *reinterpret_cast<const int32_t*>(base + p->GetOffset_Internal());
        return true;
    }

    // WHAT: address of a struct-typed property (out = its struct), or nullptr.
    const uint8_t* st_struct(UStruct* st, const uint8_t* base, const TCHAR* name, UStruct*& out)
    {
        FProperty* p = st_field(st, name);
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return nullptr;
        out = sp->GetStruct();
        return base + p->GetOffset_Internal();
    }

    // WHAT: UObject* of an ObjectProperty, or nullptr. The caller still checks alive().
    UObject* st_obj(UStruct* st, const uint8_t* base, const TCHAR* name)
    {
        FProperty* p = st_field(st, name);
        if (!p || !CastField<FObjectProperty>(p)) return nullptr;
        return *reinterpret_cast<UObject* const*>(base + p->GetOffset_Internal());
    }

    // WHAT: GridTransform-like struct: GridLocation{X,Y}, Height, GridRotation. Missing fields keep the values given.
    void st_transform(UStruct* st, const uint8_t* base, int& x, int& y, int& h, int& rot)
    {
        UStruct* gl = nullptr;
        if (const uint8_t* g = st_struct(st, base, STR("GridLocation"), gl)) { st_int(gl, g, STR("X"), x); st_int(gl, g, STR("Y"), y); }
        st_int(st, base, STR("Height"), h);
        st_int(st, base, STR("GridRotation"), rot);
    }

    // WHAT: class name without trailing _C, as narrow text (names are ASCII).
    std::string st_short(std::wstring s)
    {
        if (s.size() > 2 && s.compare(s.size() - 2, 2, STR("_C")) == 0) s.resize(s.size() - 2);
        std::string out;
        for (wchar_t c : s) out += static_cast<char>(c);
        return out;
    }

    // WHAT: the array `arr` (Blocks or Machines) of the owner's BuildBlueprint struct named Blueprint (inherited from BuildingFrame).
    //       false when there is no such struct or the field is not an array of structs.
    // WHY: one walk for both lists. elem_st = element struct, ta = the TArray inside owner memory, es = element size.
    bool st_blueprint(UObject* owner, const TCHAR* arr, UStruct*& elem_st, const TArray<uint8_t>*& ta, size_t& es)
    {
        const uint8_t* ob = reinterpret_cast<const uint8_t*>(owner);
        for (UStruct* u = owner->GetClassPrivate(); u && u->GetName() != STR("Object"); u = u->GetSuperStruct())
            for (FProperty* p : u->ForEachProperty())
            {
                auto* sp = CastField<FStructProperty>(p);
                if (!sp || !sp->GetStruct() || p->GetName() != STR("Blueprint")) continue;
                if (sp->GetStruct()->GetName().find(STR("BuildBlueprint")) == std::wstring::npos) continue;
                UStruct* bst = sp->GetStruct();
                const uint8_t* bb = ob + p->GetOffset_Internal();
                FProperty* af = st_field(bst, arr);
                auto* ap = af ? CastField<FArrayProperty>(af) : nullptr;
                auto* ip = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
                if (!ip || !ip->GetStruct()) return false;
                elem_st = ip->GetStruct();
                ta = reinterpret_cast<const TArray<uint8_t>*>(bb + af->GetOffset_Internal());
                es = static_cast<size_t>(ap->GetInner()->GetElementSize());
                return true;
            }
        return false;
    }

    // WHAT: appends the B lines (Blueprint.Blocks). false + err on a missing Blueprint or a too large text.
    bool st_read_blocks(UObject* s, std::string& out, std::wstring& err)
    {
        UStruct* et = nullptr;
        const TArray<uint8_t>* ta = nullptr;
        size_t es = 0;
        if (!st_blueprint(s, STR("Blocks"), et, ta, es)) { err = STR("station has no readable Blueprint.Blocks"); return false; }
        for (int i = 0; ta->GetData() && i < ta->Num(); ++i)
        {
            const uint8_t* e = ta->GetData() + static_cast<size_t>(i) * es;
            std::string cls;
            if (UObject* k = st_obj(et, e, STR("Class"))) cls = st_short(k->GetName());
            int gx = 0, gy = 0, h = 0, rot = 0;
            UStruct* tst = nullptr;
            if (const uint8_t* t = st_struct(et, e, STR("Transform"), tst)) st_transform(tst, t, gx, gy, h, rot);
            out += "B\t" + cls + "\t" + std::to_string(gx) + "\t" + std::to_string(gy) + "\t" + std::to_string(h) + "\t" + std::to_string(rot) + "\n";
            if (out.size() > kMaxStationBytes) { err = STR("station too large"); return false; }
        }
        return true;
    }

    // WHAT: appends the M lines (Blueprint.Machines). false + err on a missing Blueprint or a too large text.
    bool st_read_machines(UObject* s, std::string& out, std::wstring& err)
    {
        UStruct* et = nullptr;
        const TArray<uint8_t>* ta = nullptr;
        size_t es = 0;
        if (!st_blueprint(s, STR("Machines"), et, ta, es)) { err = STR("station has no readable Blueprint.Machines"); return false; }
        static const char kHex[] = "0123456789abcdef";
        for (int i = 0; ta->GetData() && i < ta->Num(); ++i)
        {
            const uint8_t* e = ta->GetData() + static_cast<size_t>(i) * es;
            int id = 0;
            if (!st_int(et, e, STR("MachineID"), id)) st_int(et, e, STR("ID"), id);
            std::string cls;
            if (UObject* k = st_obj(et, e, STR("Class"))) cls = st_short(k->GetName());
            int x = 0, y = 0, h = 0, rot = 0;
            UStruct* tst = nullptr;
            const uint8_t* t = st_struct(et, e, STR("Transform"), tst);
            if (!t) t = st_struct(et, e, STR("GridTransform"), tst);
            if (t && tst) st_transform(tst, t, x, y, h, rot);
            std::string cfg = "-";
            if (FProperty* cf = st_field(et, STR("SerializedMachineConfig")))
            {
                auto* ca = CastField<FArrayProperty>(cf);
                if (ca && CastField<FByteProperty>(ca->GetInner()))
                {
                    const auto* cta = reinterpret_cast<const TArray<uint8_t>*>(e + cf->GetOffset_Internal());
                    cfg.clear();
                    for (int j = 0; cta->GetData() && j < cta->Num(); ++j)
                    {
                        const uint8_t b = cta->GetData()[j];
                        cfg += kHex[b >> 4];
                        cfg += kHex[b & 15];
                    }
                }
            }
            out += "M\t" + std::to_string(id) + "\t" + cls + "\t" + std::to_string(x) + "\t" + std::to_string(y) + "\t" + std::to_string(h)
                 + "\t" + std::to_string(rot) + "\t" + cfg + "\n";
            if (out.size() > kMaxStationBytes) { err = STR("station too large"); return false; }
        }
        return true;
    }

    // WHAT: text of the live StationObject with save id station_id (Blocks lines, then Machines lines). false + err when not found or unreadable.
    // WHY: first live match wins; Default__ objects are skipped. Caller runs this under factory_guard (SEH).
    bool read_station(int station_id, std::string& out, std::wstring& err)
    {
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("StationObject"), all);
        UObject* st = nullptr;
        for (UObject* s : all)
            if (!st && alive(s) && s->GetName().rfind(STR("Default__"), 0) != 0 && obj_id(s) == station_id) st = s;
        if (!st) { err = STR("no live StationObject with id ") + std::to_wstring(station_id); return false; }
        out.clear();
        return st_read_blocks(st, out, err) && st_read_machines(st, out, err);
    }
}
