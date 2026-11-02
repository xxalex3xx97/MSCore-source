// FILE: cpp/MSCore/msc_ue.hpp
// WHAT: reflection helpers for the exports: typed property read/write by name, object id, display text, ProcessEvent calls with params,
//       find by save id, pilot console lookup, command factories, log. alive/put/first_of/set_int_prop live in msc_cmd.hpp.
// WHY: ported from MiningBenchMod mb_ue, mb_orders, mb_warp, mb_steer so the mining mod calls MSCore instead of owning the code.
// USED BY: msc_orders.hpp, msc_warp.hpp, msc_pin.hpp, dllmain.cpp.
#pragma once
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // WHAT: writes a line to the UE4SS log with an [MSCore] prefix. WHY: every export logs through here.
    void msc_log(const std::wstring& s) { Output::send<LogLevel::Normal>(STR("[MSCore] {}\n"), s); }

    // WHAT: narrow to wide by casting each byte. Names and error texts are ASCII.
    std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

    // WHAT: lowercase copy (towlower per character). WHY: case-insensitive name matches.
    std::wstring lower(std::wstring s)
    {
        for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
        return s;
    }

    // WHAT: save object ID (int32 property ID), or -1 if missing. WHY: docks, ships and orders are addressed by this id.
    int obj_id(UObject* o)
    {
        if (!o) return -1;
        auto* p = o->GetValuePtrByPropertyNameInChain<int32_t>(STR("ID"));
        return p ? *p : -1;
    }

    // WHAT: reads an IntProperty; false when missing or not an int. WHY: type-checked, so a wrong type is never misread.
    bool get_int_prop(UObject* o, const TCHAR* prop, int32_t& out)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        if (!p || !CastField<FIntProperty>(p)) return false;
        auto* pv = o->GetValuePtrByPropertyNameInChain<int32_t>(prop);
        if (!pv) return false;
        out = *pv;
        return true;
    }

    // WHAT: reads a bool through FBoolProperty; false when missing or not a bool. WHY: UE packs bools into bit fields.
    bool get_bool_prop(UObject* o, const TCHAR* prop, bool& out)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        auto* bp = p ? CastField<FBoolProperty>(p) : nullptr;
        if (!bp) return false;
        out = bp->GetPropertyValueInContainer(o);
        return true;
    }

    // WHAT: writes a bool through FBoolProperty; false when not a bool. WHY: a raw byte write would corrupt the bit field.
    bool set_bool_prop(UObject* o, const TCHAR* prop, bool val)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        auto* bp = p ? CastField<FBoolProperty>(p) : nullptr;
        if (!bp) return false;
        bp->SetPropertyValueInContainer(o, val);
        return true;
    }

    // WHAT: UObject* of an ObjectProperty, or nullptr. WHY: type-checked read; the caller still runs alive().
    UObject* get_obj_prop(UObject* o, const TCHAR* prop)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        if (!p || !CastField<FObjectProperty>(p)) return nullptr;
        auto* pv = o->GetValuePtrByPropertyNameInChain<UObject*>(prop);
        return pv ? *pv : nullptr;
    }

    // WHAT: copies n bytes (clamped to the parameter size) into a UFunction parameter buffer. false when missing or overflowing.
    // WHY: enum and bool params are 1 byte, so a wider source must not clobber the next param.
    bool put_prop(UFunction* fn, std::vector<uint8_t>& buf, const TCHAR* name, const void* src, size_t n)
    {
        FProperty* p = fn->FindProperty(FName(name, FNAME_Add));
        if (!p) return false;
        const size_t sz = std::min(static_cast<size_t>(p->GetSize()), n);
        const size_t off = static_cast<size_t>(p->GetOffset_Internal());
        if (off + sz > buf.size()) return false;
        std::memcpy(buf.data() + off, src, sz);
        return true;
    }

    // WHAT: calls a no-argument UFunction via ProcessEvent and returns its String or Text result; "" otherwise.
    // WHY: Blueprint functions have no C++ entry point here. The buffer is GetParmsSize plus 16 bytes of padding.
    std::wstring call_text(UObject* o, const TCHAR* fname)
    {
        UFunction* fn = o->GetFunctionByNameInChain(fname);
        if (!fn) return STR("");
        FProperty* rp = fn->GetReturnProperty();
        if (!rp) return STR("");
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        auto cls = rp->GetClass().GetFName().ToString();
        uint8_t* d = buf.data() + rp->GetOffset_Internal();
        if (cls == STR("StrProperty")) return std::wstring(**reinterpret_cast<FString*>(d));
        if (cls == STR("TextProperty")) return reinterpret_cast<FText*>(d)->ToString();
        return STR("");
    }

    // WHAT: text value of a TextProperty (e.g. DisplayName) read straight from memory; empty when missing or not text.
    // WHY: null text data = unset FText; ToString would dereference null (crash seen on a map-selected SolarSystem).
    std::wstring read_text_prop(UObject* o, const TCHAR* prop)
    {
        if (!o) return STR("");
        FProperty* p = o->GetPropertyByNameInChain(prop);
        if (!p || p->GetClass().GetFName().ToString() != STR("TextProperty")) return STR("");
        auto* t = o->GetValuePtrByPropertyNameInChain<FText>(prop);
        return (t && t->GetTextData()) ? t->ToString() : STR("");
    }

    // WHAT: in-game display name of a save object. DisplayName first (memory read), GetDisplayName as fallback.
    std::wstring obj_display(UObject* o)
    {
        if (!o) return STR("");
        std::wstring r = read_text_prop(o, STR("DisplayName"));
        return r.empty() ? call_text(o, STR("GetDisplayName")) : r;
    }

    // WHAT: alive(o) && obj_id(o) == id, but a freed pointer returns false instead of crashing.
    // WHY: a cached UObject can be freed by a save reload or dismantle; alive() then reads freed memory.
    bool cached_ok(UObject* o, int id)
    {
        __try { return alive(o) && obj_id(o) == id; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: finds the object of class cls whose save ID is id. Result cached per (class, id).
    // WHY: FindAllOf walks every UObject and is slow; the last hit is kept and rescanned only when it is gone or its id changed.
    UObject* find_by_id(const TCHAR* cls, int id)
    {
        static std::map<std::pair<std::wstring, int>, UObject*> cache;
        const auto key = std::make_pair(std::wstring(cls), id);
        if (auto it = cache.find(key); it != cache.end())
        {
            if (cached_ok(it->second, id)) return it->second;
            cache.erase(it);
        }
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(cls, v);
        for (UObject* o : v) if (o && obj_id(o) == id) { cache[key] = o; return o; }
        return nullptr;
    }

    // WHAT: the PilotConsoleObject of ship sid: ShipID match first, then ParentID. nullptr when none.
    // WHY: the console id is the aim/piloting target for the warp wrapper.
    UObject* pilot_console_of(int sid)
    {
        std::vector<UObject*> pcs;
        UObjectGlobals::FindAllOf(STR("PilotConsoleObject"), pcs);
        const TCHAR* keys[] = {STR("ShipID"), STR("ParentID")};
        for (const TCHAR* key : keys)
        {
            for (UObject* o : pcs)
            {
                int32_t v = -1;
                if (alive(o) && get_int_prop(o, key, v) && v == sid) return o;
            }
        }
        return nullptr;
    }

    // WHAT: finds the UFunction parameter called nm (case-insensitive) or In<nm>. false when none matches.
    // WHY: factory parameter names differ between builds (InX vs X), so both forms are tried.
    bool prm(UFunction* f, const TCHAR* nm, FProperty*& out)
    {
        const std::wstring n = lower(nm);
        for (FProperty* p : f->ForEachProperty())
        {
            const std::wstring pn = lower(p->GetName());
            if (pn == n || pn == STR("in") + n) { out = p; return true; }
        }
        std::wstring all;
        for (FProperty* p : f->ForEachProperty()) all += p->GetName() + STR(" ");
        msc_log(STR("warp: param ") + std::wstring(nm) + STR(" not found in ") + f->GetName() + STR(": ") + all);
        return false;
    }

    // WHAT: copies n bytes into the parameter found by prm, clamped to the parameter size. false when missing or overflowing.
    bool set_prm(UFunction* f, std::vector<uint8_t>& buf, const TCHAR* nm, const void* src, size_t n)
    {
        FProperty* p = nullptr;
        if (!prm(f, nm, p)) return false;
        const size_t sz = std::min(static_cast<size_t>(p->GetSize()), n);
        const size_t off = static_cast<size_t>(p->GetOffset_Internal());
        if (off + sz > buf.size()) return false;
        std::memcpy(buf.data() + off, src, sz);
        return true;
    }

    // WHAT: writes named fields of struct st at base. A PNum-style field gets its inner Value written; a plain int field is written directly.
    // WHY: positions and quats are fixed-point PNum structs, chunk locations are plain ints; one writer covers both.
    bool wr_fields(UStruct* st, uint8_t* base, std::initializer_list<std::pair<const TCHAR*, int64_t>> kv)
    {
        for (const auto& [name, val] : kv)
        {
            FProperty* f = st ? st->FindProperty(FName(name, FNAME_Add)) : nullptr;
            if (!f) return false;
            FProperty* t = f;
            uint8_t* d = base + f->GetOffset_Internal();
            if (auto* sp = CastField<FStructProperty>(f))
            {
                t = sp->GetStruct() ? sp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add)) : nullptr;
                if (!t) return false;
                d += t->GetOffset_Internal();
            }
            if (CastField<FInt64Property>(t)) *reinterpret_cast<int64_t*>(d) = val;
            else if (CastField<FIntProperty>(t)) *reinterpret_cast<int32_t*>(d) = static_cast<int32_t>(val);
            else return false;
        }
        return true;
    }

    // WHAT: reads one axis (plain int, or PNum-style struct with Value) of a struct at base into out. false when missing.
    bool read_axis(const uint8_t* base, UStruct* st, const TCHAR* name, int64_t& out)
    {
        FProperty* f = st ? st->FindProperty(FName(name, FNAME_Add)) : nullptr;
        if (!f) return false;
        FProperty* t = f;
        const uint8_t* d = base + f->GetOffset_Internal();
        if (auto* sp = CastField<FStructProperty>(f))
        {
            t = sp->GetStruct() ? sp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add)) : nullptr;
            if (!t) return false;
            d += t->GetOffset_Internal();
        }
        if (CastField<FInt64Property>(t)) out = *reinterpret_cast<const int64_t*>(d);
        else if (CastField<FIntProperty>(t)) out = *reinterpret_cast<const int32_t*>(d);
        else return false;
        return true;
    }

    // WHAT: makes a command object of class /Script/MainSequence.<cls> with its first CreateInput* factory, called on the class default object.
    //       fill sets the factory params. err is set on failure (nullptr returned).
    // WHY: same factory route as the order and filter commands; the factory name is not fixed, so the first CreateInput* is used.
    UObject* make_cmd(const TCHAR* cls, std::function<bool(UFunction*, std::vector<uint8_t>&)> fill, std::wstring& err)
    {
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (std::wstring(STR("/Script/MainSequence.")) + cls).c_str());
        if (!c) { err = STR("class not found: ") + std::wstring(cls); return nullptr; }
        UObject* cdo = c->GetClassDefaultObject();
        if (!alive(cdo)) { err = STR("no class default object: ") + std::wstring(cls); return nullptr; }
        UFunction* fn = nullptr;
        for (UFunction* f : c->ForEachFunction()) if (f->GetName().rfind(STR("CreateInput"), 0) == 0) { fn = f; break; }
        if (!fn) { err = STR("CreateInput not found on ") + std::wstring(cls); return nullptr; }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        if (!fill(fn, buf)) { err = STR("param fill failed: ") + std::wstring(cls); return nullptr; }
        cdo->ProcessEvent(fn, buf.data());
        FProperty* rp = fn->GetReturnProperty();
        UObject* cmd = rp ? *reinterpret_cast<UObject**>(buf.data() + rp->GetOffset_Internal()) : nullptr;
        if (!alive(cmd)) { err = STR("command null: ") + std::wstring(cls); return nullptr; }
        return cmd;
    }
}
