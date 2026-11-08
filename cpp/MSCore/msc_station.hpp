// FILE: cpp/MSCore/msc_station.hpp
// WHAT: read_station(id): the live Blueprint (Blocks + Machines) of one StationObject as one tab-separated text (native API 4, Lua only).
//       Lines: B\tclass\tgx\tgy\th\trot   and   M\tid\tcls\tx\ty\th\trot\tcfg
//       read_inventory(id) (0.7.3): the ItemSlots of one Inventory, same style. Lines: S\titem\tamount\tmax
//       inv_split(inv, src, n, dst) (0.7.6): local move between two slots of one Inventory (RVA-bound game functions, see below).
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

    // WHAT: narrow copy of an object name (names are ASCII). No _C strip, unlike st_short.
    std::string st_narrow(const std::wstring& s)
    {
        std::string out;
        for (wchar_t c : s) out += static_cast<char>(c);
        return out;
    }

    // WHAT: text of the live Inventory with save ID inv_id: one line per ItemSlots entry, empty slots included.
    //       Lines: S\titem\tamount\tmax   (item = object name of Item, "" for an empty slot; max = Item.MaxStackSize, 1 when unknown).
    // WHY: Lua struct reads of ItemSlots are unreliable in UE4SS (same as Blueprint). Fields found by name, never by offset.
    // SAFETY: game thread only, reads only. Item pointer checked by alive() before use.
    bool read_inventory(int inv_id, std::string& out, std::wstring& err)
    {
        UObject* inv = find_by_id(STR("Inventory"), inv_id);
        if (!inv) { err = STR("no live Inventory with id ") + std::to_wstring(inv_id); return false; }
        UStruct* ic = inv->GetClassPrivate();
        FProperty* p = st_field(ic, STR("ItemSlots"));
        auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
        auto* ip = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
        if (!ip || !ip->GetStruct()) { err = STR("inventory has no readable ItemSlots"); return false; }
        UStruct* et = ip->GetStruct();
        const auto* ta = reinterpret_cast<const TArray<uint8_t>*>(reinterpret_cast<const uint8_t*>(inv) + p->GetOffset_Internal());
        const size_t es = static_cast<size_t>(ap->GetInner()->GetElementSize());
        out.clear();
        for (int i = 0; ta->GetData() && i < ta->Num(); ++i)
        {
            const uint8_t* e = ta->GetData() + static_cast<size_t>(i) * es;
            std::string item;
            int mx = 1, amount = 0;
            if (UObject* it = st_obj(et, e, STR("Item")); it && alive(it))
            {
                item = st_narrow(it->GetName());
                int ms = 0;
                if (st_int(it->GetClassPrivate(), reinterpret_cast<const uint8_t*>(it), STR("MaxStackSize"), ms) && ms > 0) mx = ms;
            }
            st_int(et, e, STR("Amount"), amount);
            out += "S\t" + item + "\t" + std::to_string(amount) + "\t" + std::to_string(mx) + "\n";
            if (out.size() > kMaxStationBytes) { err = STR("inventory too large"); return false; }
        }
        return true;
    }

    // ---- inv_split (0.7.6): n items from one slot to another slot of the SAME Inventory ----
    // WHAT: the game's RemoveAtSlot / AddAtSlot, bound by RVA. The 40 entry bytes of each are compared first (msc_refund.hpp rule: fail closed).
    // WHY: RE 2026-11. int fn(Inventory* rcx, ItemStack* rdx, int32 slot), Win64 ABI. RemoveAtSlot's return value is unreliable: the slot is re-read instead.
    //      AddAtSlot has no bounds check, puts into exactly that slot if empty, merges only same item, never spills elsewhere.
    constexpr uint64_t SPLIT_REMOVE_RVA = 0x421c730;
    constexpr uint64_t SPLIT_ADD_RVA = 0x4216060;
    constexpr size_t SPLIT_HEAD_LEN = 40;
    constexpr uint8_t SPLIT_REMOVE_HEAD[SPLIT_HEAD_LEN] = {
        0x40, 0x53, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30, 0x49,
        0x63, 0xf0, 0x4c, 0x8b, 0xfa, 0x48, 0x8b, 0xd9, 0x45, 0x85,
        0xc0, 0x0f, 0x88, 0xd1, 0x01, 0x00, 0x00, 0x3b, 0xb1, 0x80,
        0x00, 0x00, 0x00, 0x0f, 0x8d, 0xc5, 0x01, 0x00, 0x00, 0x48 };
    constexpr uint8_t SPLIT_ADD_HEAD[SPLIT_HEAD_LEN] = {
        0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c, 0x24, 0x20,
        0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30, 0x4c,
        0x8b, 0x3a, 0x48, 0x8b, 0xda, 0x49, 0x63, 0xe8, 0x48, 0x8b,
        0xf9, 0x4d, 0x85, 0xff, 0x0f, 0x84, 0x58, 0x01, 0x00, 0x00 };
    constexpr size_t SPLIT_SLOTS_OFF = 0x78;        // ItemSlots TArray inside Inventory (checked by property offset)
    constexpr size_t SPLIT_MULT_OFF = 0xB0;         // int32 stack multiplier of Inventory (RE)
    constexpr size_t SPLIT_FLAGS_OFF = 8;           // uint32 object flags of an Item; bit 30 = dead (RE)
    constexpr uint32_t SPLIT_DEAD_BIT = 1u << 30;
    constexpr size_t SPLIT_STRIDE = 16;             // ItemStack {void* item @0, int32 amount @8, int32 pad @12}

    struct SplitStack { void* item; int32_t amount; int32_t pad; };
    static_assert(sizeof(SplitStack) == SPLIT_STRIDE, "ItemStack layout");
    using SplitAtSlotFn = int (*)(void* inv, SplitStack* st, int32_t slot);

    // WHAT: the two game functions as addresses, after the entry bytes match. false + err when the exe differs.
    bool split_fns(void*& rem, void*& add, std::wstring& err)
    {
        const uint8_t* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) { err = STR("split: no module base"); return false; }
        const uint8_t* r = base + SPLIT_REMOVE_RVA;
        const uint8_t* a = base + SPLIT_ADD_RVA;
        if (memcmp(r, SPLIT_REMOVE_HEAD, SPLIT_HEAD_LEN) != 0 || memcmp(a, SPLIT_ADD_HEAD, SPLIT_HEAD_LEN) != 0)
        { err = STR("split: inventory functions differ from this build, feature off"); return false; }
        rem = const_cast<uint8_t*>(r);
        add = const_cast<uint8_t*>(a);
        return true;
    }

    // WHAT: ItemSlots data pointer and count of inv. false + err when the layout is not the expected one.
    bool split_slots(UObject* inv, uint8_t*& data, int32_t& num, std::wstring& err)
    {
        FProperty* p = st_field(inv->GetClassPrivate(), STR("ItemSlots"));
        auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
        if (!ap || p->GetOffset_Internal() != SPLIT_SLOTS_OFF || ap->GetInner()->GetElementSize() != SPLIT_STRIDE)
        { err = STR("split: unexpected Inventory layout"); return false; }
        auto* ta = reinterpret_cast<TArray<uint8_t>*>(reinterpret_cast<uint8_t*>(inv) + SPLIT_SLOTS_OFF);
        data = ta->GetData();
        num = ta->Num();
        return data != nullptr;
    }

    // WHAT: item and amount of one slot, read fresh from the Inventory (the array may move after a game call).
    bool split_now(UObject* inv, int slot, void*& it, int32_t& amt, std::wstring& err)
    {
        uint8_t* data = nullptr;
        int32_t num = 0;
        if (!split_slots(inv, data, num, err) || slot < 0 || slot >= num) { if (err.empty()) err = STR("split: slot out of range"); return false; }
        const uint8_t* e = data + static_cast<size_t>(slot) * SPLIT_STRIDE;
        it = *reinterpret_cast<void* const*>(e);
        amt = *reinterpret_cast<const int32_t*>(e + 8);
        return true;
    }

    // WHAT: moves n items from slot src to empty slot dst of Inventory inv_id. Slots 0-based. true on success.
    // WHY: RE sequence (0.7.6). If the destination refuses, the items go back to src. Local edit, not a game command.
    // SAFETY: game thread, under factory_guard. Item pointers checked by alive() and the dead bit before use.
    bool inv_split(int inv_id, int src, int n, int dst, std::wstring& err)
    {
        UObject* inv = find_by_id(STR("Inventory"), inv_id);
        if (!alive(inv)) { err = STR("no live Inventory with id ") + std::to_wstring(inv_id); return false; }
        void* rem = nullptr;
        void* add = nullptr;
        if (!split_fns(rem, add, err)) return false;
        uint8_t* data = nullptr;
        int32_t num = 0;
        if (!split_slots(inv, data, num, err)) return false;
        if (src < 0 || src >= num || dst < 0 || dst >= num) { err = STR("split: slot out of range"); return false; }
        if (src == dst) { err = STR("split: source and destination are the same slot"); return false; }
        if (n < 1) { err = STR("split: amount must be at least 1"); return false; }

        void* it = nullptr;
        int32_t cnt = 0;
        if (!split_now(inv, src, it, cnt, err)) return false;
        if (!it) { err = STR("split: source slot is empty"); return false; }
        if (!alive(static_cast<UObject*>(it)) || (*reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(it) + SPLIT_FLAGS_OFF) & SPLIT_DEAD_BIT))
        { err = STR("split: source item is dead"); return false; }
        if (cnt < n) { err = STR("split: source has fewer than the amount"); return false; }
        void* dit = nullptr;
        int32_t dcnt = 0;
        if (!split_now(inv, dst, dit, dcnt, err)) return false;
        if (dit) { err = STR("split: destination slot is not empty"); return false; }
        int ms = 0;
        if (!st_int(static_cast<UObject*>(it)->GetClassPrivate(), static_cast<const uint8_t*>(it), STR("MaxStackSize"), ms)) { err = STR("split: item has no MaxStackSize"); return false; }
        const int32_t mult = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(inv) + SPLIT_MULT_OFF);
        if (mult < 1 || mult > 10000000) { err = STR("split: inventory multiplier out of range"); return false; }   // raw RE read: refuse garbage
        const int64_t cap = ms == 1 ? 1 : static_cast<int64_t>(ms) * mult;
        if (cap > INT32_MAX) { err = STR("split: stack cap out of range"); return false; }
        if (n > cap) { err = STR("split: amount above the stack cap"); return false; }

        SplitStack t{ it, n, 0 };
        reinterpret_cast<SplitAtSlotFn>(rem)(inv, &t, src);   // return value ignored: the slot is re-read
        void* ni = nullptr;
        int32_t na = 0;
        if (!split_now(inv, src, ni, na, err)) return false;
        const int32_t want = cnt - n;
        if (want == 0 ? ni != nullptr : (ni != it || na != want)) { err = STR("split: source slot did not change"); return false; }

        t = SplitStack{ it, n, 0 };
        const int left = reinterpret_cast<SplitAtSlotFn>(add)(inv, &t, dst);
        if (left > 0)
        {
            t = SplitStack{ it, left, 0 };
            reinterpret_cast<SplitAtSlotFn>(add)(inv, &t, src);
            void* bi = nullptr;
            int32_t ba = 0;
            if (!split_now(inv, src, bi, ba, err)) return false;
            const int32_t back = cnt - (n - left);   // src must hold the original minus the part dst accepted
            const int32_t have = bi ? ba : 0;
            if (have < back) { err = STR("split: items lost (") + std::to_wstring(back - have) + STR(")"); return false; }
            if (bi != it || have != back) { err = STR("split: put back changed the source slot"); return false; }
        }
        void* di = nullptr;
        int32_t da = 0;
        if (!split_now(inv, dst, di, da, err)) return false;
        if (di != it || da != n - left) { err = STR("split: destination slot did not change"); return false; }
        if (left > 0) { err = STR("split: destination full, put back"); return false; }
        return true;
    }
}
