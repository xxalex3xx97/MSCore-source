// FILE: cpp/MSCore/msc_scan.hpp
// WHAT: world scan of ships, resource asteroids and stations as MsScanRow (msc_api.hpp). scan_rows() serves a cached list with filters.
// WHY: ported from MiningBenchMod mb_rocks (alive_seh, actor_scan_item) so every mod reads the world the same way.
// CACHE: 4 slots: ship, asteroid-all, asteroid-loaded, station. A slot is rescanned when it was never read, max_age_ms is 0, or it is older than max_age_ms.
// FILTERS (on serve): type = case-insensitive equal to the row type, oname or any ore name (empty = no filter); sys > 0 = that solar system only.
//       Only rows whose obj is still alive are served (a freed object is never read).
// ASTEROID ROWS: asteroid-all = every live SmallResourceAsteroidObject (ores from ResourceItems, loaded/actor when its BP_GenericResourceAsteroid_C
//       actor is in the world, matched by ObjectId). asteroid-loaded = only the BP_GenericResourceAsteroid_C actors (obj = actor, ore = GetScanItem).
// ORE LAYOUT: ResourceItems is read as an array of structs with an Item object property (like GenParams.ResourceRichness), or as an array of
//       Item objects. The second form is not verified in game.
// DEPENDS ON: msc_api.hpp (MsScanRow), msc_cmd.hpp (alive, first_of), msc_ue.hpp (obj_id, get_int_prop, get_bool_prop, get_obj_prop, call_int,
//             actor_vec_call, obj_display, lower, widen, msc_log), msc_warp.hpp (get_chunk, sec_axes, sys_id).
// USED BY: dllmain.cpp (MSCore_scan, MSCoreNative_scan, check "scan").
#pragma once
#include <map>
#include <string>
#include <vector>

namespace
{
    constexpr int SCAN_ORES = 8;   // ore items kept per row (MsScanRow.ores size)
    enum ScanSlot : int { SLOT_SHIP = 0, SLOT_ROCK_ALL = 1, SLOT_ROCK_LOADED = 2, SLOT_STATION = 3, SLOT_COUNT = 4 };

    // one cached scan: time of the last read (GetTickCount64, 0 = never) and its rows
    struct ScanCache { ULONGLONG t = 0; std::vector<MsScanRow> rows; };
    ScanCache g_scan[SLOT_COUNT];

    // WHAT: alive() that survives a freed object (access violation -> false).
    bool alive_seh(UObject* o)
    {
        __try { return alive(o); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: writes s into a wide char array of n chars, always NUL-terminated.
    void set_wstr(wchar_t* dst, size_t n, const std::wstring& s) { wcsncpy_s(dst, n, s.c_str(), _TRUNCATE); }

    // WHAT: a row with the defaults: zeros, hull / max_hull / order_idx = -1. obj is set, sys/chunk/loc are read by fill_place.
    MsScanRow blank_row(int kind, UObject* o)
    {
        MsScanRow r{};
        r.kind = kind;
        r.obj = o;
        r.hull = -1;
        r.max_hull = -1;
        r.order_idx = -1;
        return r;
    }

    // WHAT: system id, chunk and chunk-relative Location of o into r. Each part stays 0 when unreadable.
    void fill_place(MsScanRow& r, UObject* o)
    {
        r.sys = sys_id(o);
        get_chunk(o, r.ch);
        sec_axes(o, STR("Location"), r.loc);
    }

    // WHAT: world position of actor a (K2_GetActorLocation, UE units) into r.world; world_ok = 1 when read.
    void fill_world(MsScanRow& r, UObject* a)
    {
        std::wstring why;
        if (alive_seh(a) && actor_vec_call(a, STR("K2_GetActorLocation"), r.world, why)) r.world_ok = 1;
    }

    // WHAT: ore items of a save object's ResourceItems into r.ores (max SCAN_ORES). An element is a struct with an Item object property, or the Item object itself.
    void fill_ores(MsScanRow& r, UObject* o)
    {
        FProperty* p = o->GetPropertyByNameInChain(STR("ResourceItems"));
        auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
        if (!ap || !ap->GetInner()) return;
        FProperty* inner = ap->GetInner();
        FProperty* ip = inner;
        size_t item_off = 0;
        if (auto* esp = CastField<FStructProperty>(inner))
        {
            if (!esp->GetStruct()) return;
            ip = esp->GetStruct()->FindProperty(FName(STR("Item"), FNAME_Add));
            if (!ip) return;
            item_off = static_cast<size_t>(ip->GetOffset_Internal());
        }
        if (!CastField<FObjectProperty>(ip)) return;
        auto* arr = reinterpret_cast<TArray<uint8_t>*>(reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal());
        const size_t es = static_cast<size_t>(inner->GetElementSize());
        for (int32_t i = 0; i < arr->Num() && r.nores < SCAN_ORES; ++i)
        {
            UObject* it = *reinterpret_cast<UObject**>(arr->GetData() + static_cast<size_t>(i) * es + item_off);
            if (alive_seh(it)) r.ores[r.nores++] = it;
        }
    }

    // WHAT: ore item of a rock actor (GetScanItem(Item)). nullptr when unreadable.
    // WHY: the Item parameter is read from a raw parameter buffer after ProcessEvent; the property type check makes sure the bytes are an object pointer.
    UObject* actor_scan_item(UObject* a)
    {
        UFunction* fn = a->GetFunctionByNameInChain(STR("GetScanItem"));
        FProperty* ip = fn ? fn->FindProperty(FName(STR("Item"), FNAME_Add)) : nullptr;
        if (!ip || !CastField<FObjectProperty>(ip))
        {
            static bool warned = false;
            if (!warned) { warned = true; msc_log(STR("scan: GetScanItem Item param missing or not an object property")); }
            return nullptr;
        }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        a->ProcessEvent(fn, buf.data());
        return *reinterpret_cast<UObject**>(buf.data() + ip->GetOffset_Internal());
    }

    // WHAT: display name of the first ore of r ("" when none).
    std::wstring first_ore_name(const MsScanRow& r)
    {
        return r.nores > 0 && alive_seh(static_cast<UObject*>(r.ores[0])) ? obj_display(static_cast<UObject*>(r.ores[0])) : std::wstring();
    }

    // WHAT: the current order of ship s: CurrentOrderIndex into Orders. Out of range: order_idx -1 and order empty.
    void fill_order(MsScanRow& r, UObject* s, int32_t idx)
    {
        FProperty* p = s->GetPropertyByNameInChain(STR("Orders"));
        auto* arr = (p && CastField<FArrayProperty>(p)) ? s->GetValuePtrByPropertyNameInChain<TArray<UObject*>>(STR("Orders")) : nullptr;
        if (!arr || idx < 0 || idx >= arr->Num()) return;
        UObject* ord = (*arr)[idx];
        r.order_idx = idx;
        if (!alive_seh(ord)) return;
        int32_t dk = 0;
        if (get_int_prop(ord, STR("DockID"), dk)) r.order_dock = dk;
        set_wstr(r.order, 48, ord->GetClassPrivate()->GetName());
    }

    // WHAT: every live ship: id, names, dock state, piloted, hull, order, actor world position.
    void scan_ships(std::vector<MsScanRow>& out)
    {
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        const int cid = alive(ctrl) ? obj_id(ctrl) : -1;
        std::vector<UObject*> ships;
        UObjectGlobals::FindAllOf(STR("DMSPlayerShipObject"), ships);
        for (UObject* s : ships)
        {
            try
            {
                if (!alive_seh(s)) continue;
                MsScanRow r = blank_row(0, s);
                r.id = obj_id(s);
                fill_place(r, s);
                std::wstring nm = obj_display(s);
                set_wstr(r.name, 64, nm.empty() ? std::wstring(s->GetName()) : nm);
                set_wstr(r.type, 64, s->GetClassPrivate()->GetName());
                set_wstr(r.oname, 64, s->GetName());
                int32_t v = 0;
                if (get_int_prop(s, STR("DockID"), v)) { r.dock_id = v; r.docked = v != 0; }
                int32_t pilot = -1;
                r.piloted = cid > 0 && get_int_prop(s, STR("PilotID"), pilot) && pilot == cid;
                if (call_int(s, STR("GetCurrentHull"), v)) r.hull = v;
                if (get_int_prop(s, STR("MaxHealth"), v)) r.max_hull = v;
                if (get_int_prop(s, STR("CurrentOrderIndex"), v)) fill_order(r, s, v);
                UObject* act = get_obj_prop(s, STR("Actor"));
                r.actor = act;
                fill_world(r, act);
                out.push_back(r);
            }
            catch (...) {}
        }
    }

    // WHAT: every live resource asteroid save object. loaded = its BP_GenericResourceAsteroid_C actor is in the world (matched by ObjectId).
    void scan_rocks_all(std::vector<MsScanRow>& out)
    {
        std::map<int, UObject*> actor_by_id;
        std::vector<UObject*> acts;
        UObjectGlobals::FindAllOf(STR("BP_GenericResourceAsteroid_C"), acts);
        for (UObject* a : acts)
        {
            int32_t id = 0;
            if (alive_seh(a) && get_int_prop(a, STR("ObjectId"), id) && id > 0) actor_by_id[id] = a;
        }
        std::vector<UObject*> saves;
        UObjectGlobals::FindAllOf(STR("SmallResourceAsteroidObject"), saves);
        for (UObject* o : saves)
        {
            try
            {
                if (!alive_seh(o)) continue;
                bool live = true;
                if (get_bool_prop(o, STR("bAlive"), live) && !live) continue;
                MsScanRow r = blank_row(1, o);
                r.id = obj_id(o);
                fill_place(r, o);
                fill_ores(r, o);
                set_wstr(r.type, 64, first_ore_name(r));
                set_wstr(r.oname, 64, o->GetName());
                const auto it = actor_by_id.find(r.id);
                if (it != actor_by_id.end())
                {
                    r.actor = it->second;
                    r.loaded = 1;
                    fill_world(r, it->second);
                }
                out.push_back(r);
            }
            catch (...) {}
        }
    }

    // WHAT: every live BP_GenericResourceAsteroid_C actor (loaded rocks). obj = actor, ore = GetScanItem.
    void scan_rocks_loaded(std::vector<MsScanRow>& out)
    {
        std::vector<UObject*> acts;
        UObjectGlobals::FindAllOf(STR("BP_GenericResourceAsteroid_C"), acts);
        for (UObject* a : acts)
        {
            try
            {
                if (!alive_seh(a)) continue;
                MsScanRow r = blank_row(1, a);
                r.actor = a;
                r.loaded = 1;
                int32_t id = 0;
                if (get_int_prop(a, STR("ObjectId"), id)) r.id = id;
                fill_place(r, a);
                UObject* item = actor_scan_item(a);
                if (alive_seh(item))
                {
                    r.ores[0] = item;
                    r.nores = 1;
                    set_wstr(r.type, 64, obj_display(item));
                }
                set_wstr(r.oname, 64, a->GetName());
                fill_world(r, a);
                out.push_back(r);
            }
            catch (...) {}
        }
    }

    // WHAT: every live SolarSystemSector-like station: FindAllOf Dock. name = display name, type = class name, oname = object name.
    void scan_stations(std::vector<MsScanRow>& out)
    {
        std::vector<UObject*> docks;
        UObjectGlobals::FindAllOf(STR("Dock"), docks);
        for (UObject* d : docks)
        {
            try
            {
                if (!alive_seh(d)) continue;
                MsScanRow r = blank_row(2, d);
                r.id = obj_id(d);
                fill_place(r, d);
                set_wstr(r.name, 64, obj_display(d));
                set_wstr(r.type, 64, d->GetClassPrivate()->GetName());
                set_wstr(r.oname, 64, d->GetName());
                UObject* act = get_obj_prop(d, STR("Actor"));
                r.actor = act;
                fill_world(r, act);
                out.push_back(r);
            }
            catch (...) {}
        }
    }

    // WHAT: true when row r matches the lowercase filter want (row type, oname or any ore name).
    bool row_matches(const MsScanRow& r, const std::wstring& want)
    {
        if (lower(r.type) == want || lower(r.oname) == want) return true;
        for (int i = 0; i < r.nores; ++i)
        {
            UObject* it = static_cast<UObject*>(r.ores[i]);
            if (alive_seh(it) && lower(obj_display(it)) == want) return true;
        }
        return false;
    }

    // WHAT: the rows of kind (0 ship, 1 asteroid, 2 station) that pass the filters. The slot is rescanned when it is empty,
    //       max_age_ms is 0, or it is older than max_age_ms (GetTickCount64 ms). Unknown kind: no rows.
    // WHY: one cache per kind, so repeated calls within max_age_ms do not walk FindAllOf again.
    std::vector<MsScanRow> scan_rows(int kind, ULONGLONG max_age_ms, const std::string& type, int sys, bool loaded_only)
    {
        int slot = -1;
        if (kind == 0) slot = SLOT_SHIP;
        else if (kind == 1) slot = loaded_only ? SLOT_ROCK_LOADED : SLOT_ROCK_ALL;
        else if (kind == 2) slot = SLOT_STATION;
        if (slot < 0) return {};
        ScanCache& c = g_scan[slot];
        const ULONGLONG now = GetTickCount64();
        if (c.t == 0 || max_age_ms == 0 || now - c.t >= max_age_ms)
        {
            std::vector<MsScanRow> rows;
            if (slot == SLOT_SHIP) scan_ships(rows);
            else if (slot == SLOT_ROCK_ALL) scan_rocks_all(rows);
            else if (slot == SLOT_ROCK_LOADED) scan_rocks_loaded(rows);
            else scan_stations(rows);
            c.rows.swap(rows);
            c.t = now;
        }
        const std::wstring want = lower(widen(type));
        std::vector<MsScanRow> out;
        for (const MsScanRow& r : c.rows)
        {
            if (sys > 0 && r.sys != sys) continue;
            if (!want.empty() && !row_matches(r, want)) continue;
            if (!alive_seh(static_cast<UObject*>(r.obj))) continue;
            out.push_back(r);
        }
        return out;
    }
}
