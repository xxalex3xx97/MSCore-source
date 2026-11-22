// MSCore native part. Exposes the Lua globals MSCoreNative_* (Lua mod MSCore) and the C exports used by MiningBenchMod (api 2).
#define NOMINMAX  // Windows.h min/max macros break UE4SS headers
#include <Windows.h>
#include <cmath>
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <wchar.h>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
#include <LuaType/LuaUObject.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Property/FTextProperty.hpp>

using namespace RC;
using namespace RC::Unreal;

#include "msc_caps.hpp"
#include "msc_api.hpp"
#include "msc_cmd.hpp"
#include "msc_ue.hpp"
#include "msc_orders.hpp"
#include "msc_warp.hpp"
#include "msc_pin.hpp"
#include "msc_chunks.hpp"
#include "msc_scan.hpp"
#include "msc_ships.hpp"
#include "msc_weapons.hpp"
#include "msc_presence.hpp"
#include "msc_flight.hpp"
#include "msc_repair.hpp"
#include "msc_combat.hpp"
#include "msc_threat.hpp"
#include "msc_strafe.hpp"
#include "msc_capture.hpp"
#include "msc_stnames.hpp"
#include "msc_fleets.hpp"
#include "msc_rockreg.hpp"
#include "msc_world.hpp"
#include "msc_factory.hpp"
#include "msc_station.hpp"
#include "msc_rename.hpp"
#include "msc_dockfilter.hpp"
#include "msc_refund.hpp"
#include "msc_designer.hpp"
#include "msc_loader.hpp"
#include "msc_dockkind.hpp"

namespace
{
    constexpr const char* kVersion = mscl::kVersion;   // single source in msc_loader.hpp; keep equal to MSCore.version in mscore.lua and mscore.mod

    std::string narrow(const std::wstring& s) { std::string out; for (wchar_t c : s) out += static_cast<char>(c); return out; }   // error texts are ASCII

    // copies err text into the caller buffer, always NUL-terminated
    void copy_err(const std::wstring& s, wchar_t* out, int outlen)
    {
        if (out && outlen > 0) wcsncpy_s(out, static_cast<size_t>(outlen), s.c_str(), _TRUNCATE);
    }

    // runs fn(err) for an export: exceptions become a failure, err goes to the caller buffer, returns 1 on success
    template <typename F>
    int export_call(F fn, wchar_t* err, int errlen, const std::wstring& mod = STR("misc"))
    {
        std::wstring e;
        bool ok = false;
        try { ok = fn(e); }
        catch (...) { ok = false; e = STR("exception"); module_disable(mod, STR("exception")); }
        copy_err(e, err, errlen);
        return ok ? 1 : 0;
    }

    // WHAT: first SolarSystemSector with a chunk and warp distance: system id, chunk, arrival distance. false when there is none.
    bool first_sector(int& sys, int64_t ch[3], int64_t& dist)
    {
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(STR("SolarSystemSector"), v);
        for (UObject* o : v)
        {
            if (alive(o) && sec_axes(o, STR("ChunkLocation"), ch) && sec_warp_dist(o, dist)) { sys = sys_id(o); return true; }
        }
        return false;
    }

    // WHAT: one read-only world check by name (Lua: MSCoreNative_check). detail says what was seen. ok = false with "SKIP ..." when the world it needs is not loaded.
    // WHY: runs from selfcheck only; it never sends a command.
    // dock kind (API 19): 1 industrial dock, 0 normal dock, -1 unknown (null, dead, class unreadable, not a dock, native fault). Rule: msc_dockkind.hpp. Game thread.
    // The read stays here because msc_dockkind.hpp is pure (no UObject).
    int dock_kind_read(UObject* o)
    {
        if (!alive(o)) return -1;
        int r = -1;
        if (!seh_run([&] { UClass* c = o->GetClassPrivate(); if (c) r = dock_kind_from_class(std::wstring(c->GetName())); })) return -1;
        return r;
    }

    bool check_one(const std::string& name, std::string& detail)
    {
        std::wstring why;
        UObject* ship = first_of(STR("DMSPlayerShipObject"));
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (name == "classes")
        {
            const TCHAR* cls[] = {STR("NewShipOrderInput"), STR("UpdateShipOrderInput"), STR("LogisticFilterInput"), STR("ShipTeleportInput"),
                                  STR("SetPilotingInput"), STR("MapPinInputCommand"), STR("ConfigureShipOrdersInput")};
            std::string miss;
            for (const TCHAR* c : cls)
                if (!UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (STR("/Script/MainSequence.") + std::wstring(c)).c_str())) miss += narrow(c) + " ";
            detail = miss.empty() ? std::string("7 input classes") : "missing: " + miss;
            return miss.empty();
        }
        if (name == "controller")
        {
            if (!alive(ctrl)) { detail = "SKIP no DMSPlayerController"; return false; }
            if (!ctrl->GetFunctionByNameInChain(STR("AddInputCommand")) || !ctrl->GetFunctionByNameInChain(STR("SetShipAutopilot")) ||
                !ctrl->GetFunctionByNameInChain(STR("SkipToOrder")))
            { detail = "AddInputCommand/SetShipAutopilot/SkipToOrder missing"; return false; }
            detail = ctrl->GetFunctionByNameInChain(STR("DeleteOrder")) ? "ok" : "warn: DeleteOrder missing (ConfigureShipOrders fallback)";
            return true;
        }
        if (name == "scan")
        {
            const auto rows = scan_rows(0, 0, std::string(), 0, false);
            if (rows.empty()) { detail = "SKIP no ships"; return false; }
            detail = std::to_string(rows.size()) + " ships";
            return true;
        }
        if (name == "rocks")
        {
            if (!module_ok(STR("rocks"))) { detail = "rocks off: " + caps_text(); return false; }
            const auto rows = scan_rows(1, 0, std::string(), 0, false);
            if (rows.empty()) { detail = "SKIP no asteroids (world not loaded)"; return false; }
            if (!module_ok(STR("rocks"))) { detail = "rocks turned off by the walk: " + caps_text(); return false; }
            detail = std::to_string(rows.size()) + " asteroids";
            return true;
        }
        if (name == "rockreg")
        {
            if (!module_ok(STR("rockreg"))) { detail = "rockreg off: " + caps_text(); return false; }
            bool ok = false;
            if (!seh_run([&] { ok = rr_selftest(detail); })) { detail = "rockreg selftest faulted (module rockreg off until reload)"; module_disable(STR("rockreg"), STR("selfcheck fault")); return false; }
            return ok;
        }
        if (name == "ships")
        {
            if (!module_ok(STR("ships"))) { detail = "ships off: " + caps_text(); return false; }
            if (!alive(ship)) { detail = "SKIP no ships"; return false; }
            int32_t cur = -1, max = -1;
            const int r = ship_shields_read(ship, cur, max);
            detail = "shields cur=" + std::to_string(cur) + " max=" + std::to_string(max);
            return (r & 1) != 0;
        }
        if (name == "mining_caps")
        {
            if (!module_ok(STR("ships"))) { detail = "ships off: " + caps_text(); return false; }
            if (!alive(ship)) { detail = "SKIP no ships"; return false; }
            const int r = ship_mining_caps_read(ship);
            detail = "mining_caps=" + std::to_string(r);
            return r >= 0;
        }
        if (name == "ship_caps")
        {
            if (!module_ok(STR("ships"))) { detail = "ships off: " + caps_text(); return false; }
            if (!alive(ship)) { detail = "SKIP no ships"; return false; }
            const int r = ship_caps_read(ship);
            detail = "ship_caps=" + std::to_string(r);
            return r >= 0;
        }
        if (name == "dock_kind")
        {
            if (!module_ok(STR("stations"))) { detail = "stations off"; return false; }
            UObject* dock = nullptr;
            std::vector<UObject*> v;
            if (!seh_run([&] { UObjectGlobals::FindAllOf(STR("Dock"), v); })) { detail = "SKIP no docks (FindAllOf faulted)"; return false; }
            for (UObject* o : v) if (alive(o)) { dock = o; break; }
            if (!dock) { detail = "SKIP no docks"; return false; }
            const int r = dock_kind_read(dock);
            detail = "dock_kind=" + std::to_string(r);
            return r >= 0;
        }
        if (name == "stations")
        {
            if (!module_ok(STR("stations"))) { detail = "stations off: " + caps_text(); return false; }
            stn_probe_log();
            std::vector<MsStationName> rows;
            if (!stations_read(rows)) { detail = "stations read faulted (module stations off until reload)"; module_disable(STR("stations"), STR("selfcheck fault")); return false; }
            if (rows.empty()) { detail = "SKIP no stations"; return false; }
            int named = 0;
            for (const auto& r : rows) if (r.name[0]) ++named;
            const std::wstring stn_src = stn_src_get();
            detail = std::to_string(rows.size()) + " stations, " + std::to_string(named) + " with name, via " + (stn_src.empty() ? std::string("none") : narrow(stn_src));
            return named > 0;
        }
        if (name == "fleets")
        {
            if (!module_ok(STR("fleets"))) { detail = "fleets off: " + caps_text(); return false; }
            if (!alive(ship)) { detail = "SKIP no ships"; return false; }
            std::vector<MsFleetTag> t; std::vector<MsFleetShip> s;
            const int r = fleets_get(0, t, s);
            if (r != 1) { detail = "fleets read faulted or off (module fleets off until reload)"; return false; }
            detail = std::to_string(t.size()) + " tags, " + std::to_string(s.size()) + " ships with tags";
            return true;
        }
        if (name == "fleets_write")
        {
            bool ok = false;
            if (!seh_run([&] { ok = fleets_write_selftest(detail); })) { detail = "fleets_write selftest faulted"; return false; }
            detail += module_ok(STR("fleets_write")) ? " [write ON]" : " [write OFF: unverified]";
            return ok;
        }
        if (name == "paused")
        {
            if (!module_ok(STR("world"))) { detail = "world off: " + caps_text(); return false; }
            if (!alive(ctrl)) { detail = "SKIP no DMSPlayerController"; return false; }
            const int r = world_paused_read();
            detail = r < 0 ? std::string("IsGamePaused unreadable") : std::string(r ? "paused=1" : "paused=0");
            return r >= 0;
        }
        if (name == "flight")
        {
            if (!module_ok(STR("flight"))) { detail = "flight off: " + caps_text(); return false; }
            const int sid = alive(ship) ? obj_id(ship) : 0;
            if (sid <= 0) { detail = "SKIP no ships"; return false; }
            MsFlight f{};
            int r = 0, console = 0;
            if (!seh_run([&] { r = ship_flight_read(sid, FL_POSE | FL_STATE | FL_LIMITS | FL_POWER, f); })) r = -1;
            if (r == 0) { detail = "SKIP no ships"; return false; }
            if (r < 0) { detail = "flight read faulted (module flight off until reload)"; module_disable(STR("flight"), STR("selfcheck fault")); return false; }
            seh_run([&] { console = flight_console_id(sid); });
            detail = "thr=" + std::to_string(f.throttle) + " steps=" + std::to_string(f.steps) +
                     " actor=" + ((f.flags & FF_HAS_ACTOR) ? "1" : "0") + " objpose=" + ((f.flags & FF_OBJ_POSE) ? "1" : "0") +
                     " grid=" + ((f.flags & FF_GRID) ? "1" : "0") + " lpflag=" + ((f.flags & FF_LP_KNOWN) ? ((f.flags & FF_LP_ON) ? "1" : "0") : "n/a") +
                     " console=" + std::to_string(console);
            return (f.flags & FF_THROTTLE) && (f.flags & FF_STEPS) && (f.flags & FF_OBJ_POSE) && f.steps > 0;
        }
        if (name == "repair")
        {
            if (!module_ok(STR("repair"))) { detail = "repair off: " + caps_text(); return false; }
            bool ok = false;
            if (!seh_run([&] { ok = repair_selftest(detail); })) { detail = "repair selftest faulted"; module_disable(STR("repair"), STR("selfcheck fault")); return false; }
            return ok;
        }
        if (name == "combat")
        {
            if (!g_combat_on.load()) { detail = "SKIP combat_on.txt missing (feature off)"; return false; }
            if (!module_ok(STR("combat"))) { detail = "combat off: " + caps_text(); return false; }
            bool ok = false;
            if (!seh_run([&] { ok = combat_selftest(detail); })) { detail = "combat selftest faulted"; module_disable(STR("combat"), STR("selfcheck fault")); return false; }
            return ok;
        }
        if (name == "threat")
        {
            if (!module_ok(STR("threat"))) { detail = "threat off: " + caps_text(); return false; }
            if (g_threat_off.load()) { detail = "SKIP threat_off.txt exists (feature off)"; return false; }
            bool ok = false;
            if (!seh_run([&] { ok = threat_selftest(detail); })) { detail = "threat selftest faulted"; module_disable(STR("threat"), STR("selfcheck fault")); return false; }
            return ok;
        }
        if (name == "strafe")
        {
            if (!module_ok(STR("strafe"))) { detail = "strafe off: " + caps_text(); return false; }
            if (g_strafe_off.load()) { detail = "SKIP strafe_off.txt exists (feature off)"; return false; }
            bool ok = false;
            if (!seh_run([&] { ok = strafe_selftest(detail); })) { detail = "strafe selftest faulted"; module_disable(STR("strafe"), STR("selfcheck fault")); return false; }
            return ok;
        }
        if (name == "capture")
        {
            if (!g_cap_on.load()) { detail = "SKIP strafe_probe_on.txt missing (probe off)"; return false; }
            detail = std::string(g_cap_dead ? "probe dead (fault)" : (g_cap_hooked ? "hook armed" : "polling (hook not armed)"));
            return !g_cap_dead;
        }
        if (name == "chunks")
        {
            const bool ok = chunk_ready(why);
            detail = ok ? std::string("natives found") : narrow(why);
            return ok;
        }
        if (name == "sectors")
        {
            const int n = sectors_fill(nullptr, 0);
            detail = std::to_string(n) + " sectors";
            return n > 0;
        }
        if (name == "pin_hook")
        {
            if (!alive(ctrl)) { detail = "SKIP no DMSPlayerController"; return false; }
            const bool ok = pin_hook_ensure();
            detail = ok ? "hooked" : "not hooked";
            return ok;
        }
        if (name == "filters_read")
        {
            if (!alive(ship)) { detail = "SKIP no ship"; return false; }
            std::vector<MsFilterRow> rows;
            const bool ok = filters_read(ship, rows);
            detail = ok ? std::to_string(rows.size()) + " rows" : std::string("LogisticFilters unreadable");
            return ok;
        }
        if (name == "find_item")
        {
            std::vector<UObject*> v;
            UObjectGlobals::FindAllOf(STR("Item"), v);
            UObject* it = nullptr;
            for (UObject* o : v) if (alive(o)) { it = o; break; }
            if (!it) { detail = "SKIP no Item"; return false; }
            const std::wstring nm = it->GetName();
            const bool ok = find_item(narrow(nm), why) == it;
            detail = ok ? "resolved " + narrow(nm) : "no match: " + narrow(why.empty() ? nm : why);
            return ok;
        }
        if (name == "find_dock")
        {
            std::vector<UObject*> v;
            UObjectGlobals::FindAllOf(STR("Dock"), v);
            std::wstring nm;
            for (UObject* o : v) { if (!alive(o)) continue; nm = dock_name(o); if (!nm.empty()) break; }
            if (nm.empty()) { detail = "SKIP no named dock"; return false; }
            const int id = find_dock_by_name(narrow(nm), why);
            const bool ok = id > 0;
            detail = ok ? "id=" + std::to_string(id) : narrow(why);
            return ok;
        }
        // the checks below need a ship and a sector
        int sys = 0;
        int64_t ch[3] = {0, 0, 0};
        int64_t dist = 0;
        if (name == "arrival" || name == "jump_cost")
        {
            if (!alive(ship)) { detail = "SKIP no ship"; return false; }
            if (!first_sector(sys, ch, dist)) { detail = "SKIP no sector"; return false; }
            if (name == "jump_cost")
            {
                int32_t cost = -1;
                const bool ok = jump_cost(ship, sys, ch, cost, why);
                detail = ok ? "cost=" + std::to_string(cost) : narrow(why);
                return ok;
            }
            int64_t arr[3] = {0, 0, 0};
            if (!arrival_of(ship, sys, ch, dist, arr, why)) { detail = narrow(why); return false; }
            const double r = std::sqrt(double(arr[0]) * double(arr[0]) + double(arr[1]) * double(arr[1]));
            detail = "|xy|=" + std::to_string(static_cast<long long>(r)) + " warp_dist=" + std::to_string(dist);
            return std::fabs(r - double(dist)) <= 0.02 * double(dist);
        }
        detail = "unknown check " + name;
        return false;
    }

    // ---------------- Lua natives (MSCoreNative_*) ----------------

    int lua_version(const LuaMadeSimple::Lua& lua) { lua.set_string(kVersion); return 1; }

    int lua_alive(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        try
        {
            if (lua.is_userdata(1))
            {
                auto& obj = lua.get_userdata<LuaType::UObject>(1);
                ok = alive(obj.get_remote_cpp_object());
            }
        }
        catch (...) { ok = false; }
        lua.set_bool(ok);
        return 1;
    }

    // pushes ok and err as the two Lua results
    int lua_result(const LuaMadeSimple::Lua& lua, bool ok, const std::wstring& err)
    {
        lua.set_bool(ok);
        lua.set_string(narrow(err));
        return 2;
    }

    // true when Lua args 1..n are all numbers
    bool nums(const LuaMadeSimple::Lua& lua, int n)
    {
        for (int i = 1; i <= n; ++i) if (!lua.is_number(i)) return false;
        return true;
    }

    // Lua args are CONSUMED IN ORDER: every LuaMadeSimple getter (get_integer, get_float, get_string, get_userdata) removes its
    // stack slot, so the next arg is always index 1. Read each arg as its own statement, in order. Never use a fixed index
    // after a getter: the index then points past the top, lua_remove decrements top and the Lua stack is corrupted (crash later).
    // Type checks (is_number, is_string, is_userdata) do not remove anything, so they may use fixed indexes before any read.
    int arg_int(const LuaMadeSimple::Lua& lua) { return static_cast<int>(lua.get_integer(1)); }

    // next arg as a UObject: nullptr for nil or a non-userdata. Always consumes the arg.
    UObject* arg_obj(const LuaMadeSimple::Lua& lua)
    {
        if (!lua.is_userdata(1)) { lua.discard_value(1); return nullptr; }
        return lua.get_userdata<LuaType::UObject>(1).get_remote_cpp_object();
    }

    // Lua: send_cmd(receiver int, cmd UObject) -> ok, err
    int lua_send_cmd(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!lua.is_number(1)) err = STR("receiver is not a number");
            else if (!lua.is_userdata(2)) err = STR("cmd is not a UObject");
            else
            {
                const int receiver = arg_int(lua);
                UObject* cmd = arg_obj(lua);
                if (!alive(cmd)) err = STR("cmd is not alive");
                else
                {
                    if (fix_type_id(cmd, err)) ok = add_input(receiver, cmd, err);
                }
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: view_send(cid, yawDeg, pitchDeg) -> ok, err
    int lua_view_send(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 3)) err = STR("view_send needs cid, yaw, pitch numbers");
            else
            {
                const int cid = arg_int(lua);
                const float yaw = lua.get_float(1);
                const float pitch = lua.get_float(1);
                ok = view_send(cid, static_cast<int64_t>(yaw * kViewRawPerDeg), static_cast<int64_t>(pitch * kViewRawPerDeg), err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: throttle(sid, delta) -> ok, err
    int lua_throttle(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 2)) err = STR("throttle needs sid, delta numbers");
            else
            {
                const int sid = arg_int(lua);
                const int delta = arg_int(lua);
                ok = send_ship_throttle(sid, delta, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // WHY: text form of the msc_cmd.hpp offsets; selfcheck native.offsets looks for 0x28 in it.
    int lua_offsets(const LuaMadeSimple::Lua& lua)
    {
        lua.set_string("typeid_off=0x28 throttle_id=0x3a view_raw_per_deg=65536 " + caps_text());
        return 1;
    }

    // Lua: check_offsets() -> ok, detail. Builds one ShipMovementInputCommand (not sent) and checks the type id is 0 at +0x28.
    // "SKIP" detail when no controller is loaded.
    int lua_check_offsets(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::string detail;
        try
        {
            UObject* ctrl = first_of(STR("DMSPlayerController"));
            UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ShipMovementInputCommand"));
            if (!alive(ctrl)) detail = "SKIP no DMSPlayerController";
            else if (!c) detail = "ShipMovementInputCommand class not found";
            else
            {
                UObject* cmd = UObjectGlobals::NewObject<UObject>(ctrl, c);
                if (!alive(cmd)) detail = "NewObject ShipMovementInputCommand failed";
                else
                {
                    const int32_t v = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(cmd) + kTypeIdOffset);
                    ok = v == 0;
                    detail = "int@0x28=" + std::to_string(v) + " (expect 0 from constructor)";
                }
            }
        }
        catch (...) { ok = false; detail = "exception"; }
        lua.set_bool(ok);
        lua.set_string(detail);
        return 2;
    }

    // Lua: new_order(receiver, cls) -> ok, err
    int lua_new_order(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 1) || !lua.is_string(2)) err = STR("new_order needs receiver number, cls string");
            else
            {
                const int receiver = arg_int(lua);
                const std::string cls(lua.get_string(1));
                ok = new_order(receiver, widen(cls), err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: update_order(oid, target, item|nil, cmp, amount) -> ok, err
    int lua_update_order(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 2) || !lua.is_number(4) || !lua.is_number(5)) err = STR("update_order needs oid, target, cmp, amount numbers");
            else
            {
                const int oid = arg_int(lua);
                const int target = arg_int(lua);
                UObject* item = arg_obj(lua);
                const int cmp = arg_int(lua);
                const int amount = arg_int(lua);
                ok = update_order(oid, target, item, cmp, amount, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: delete_order(sid, oid) -> ok, err
    int lua_delete_order(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 2)) err = STR("delete_order needs sid, oid numbers");
            else
            {
                const int sid = arg_int(lua);
                const int oid = arg_int(lua);
                ok = delete_order(sid, oid, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: autopilot(sid, on 0|1) -> ok, err
    int lua_autopilot(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 2)) err = STR("autopilot needs sid, on numbers");
            else
            {
                const int sid = arg_int(lua);
                const bool on = arg_int(lua) != 0;
                ok = set_autopilot(sid, on, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: skip_to(sid, idx) -> ok, err, before, after
    int lua_skip_to(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        int32_t before = -1, after = -1;
        try
        {
            if (!nums(lua, 2)) err = STR("skip_to needs sid, idx numbers");
            else
            {
                const int sid = arg_int(lua);
                const int idx = arg_int(lua);
                ok = skip_to(sid, idx, before, after, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        lua.set_bool(ok);
        lua.set_string(narrow(err));
        lua.set_integer(before);
        lua.set_integer(after);
        return 4;
    }

    // Lua: filter_send(sid, idx, item|nil, amount, action, remove 0|1) -> ok, err
    int lua_filter_send(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 2) || !lua.is_number(4) || !lua.is_number(5) || !lua.is_number(6)) err = STR("filter_send needs sid, idx, amount, action, remove numbers");
            else
            {
                const int sid = arg_int(lua);
                const int idx = arg_int(lua);
                UObject* item = arg_obj(lua);
                const int amount = arg_int(lua);
                const int action = arg_int(lua);
                const bool remove = arg_int(lua) != 0;
                ok = filter_send(sid, idx, item, amount, action, remove, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: warp_send(sid, sys, cx, cy, cz, ax, ay, az, wrap 0|1, set_target 0|1) -> ok, err
    // cx..cz = chunk, ax..az = arrival location (raw). wrap = piloting wrapper, set_target = bSetTarget.
    int lua_warp_send(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 10)) err = STR("warp_send needs 10 numbers");
            else
            {
                const int sid = arg_int(lua);
                const int sys = arg_int(lua);
                int64_t ch[3], arr[3];
                for (int i = 0; i < 3; ++i) ch[i] = lua.get_integer(1);
                for (int i = 0; i < 3; ++i) arr[i] = lua.get_integer(1);
                const bool wrap = lua.get_integer(1) != 0;
                const bool set_target = lua.get_integer(1) != 0;
                ok = warp_send(sid, sys, ch, arr, wrap, err, set_target);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: pinned() -> sector id (0 = none)
    int lua_pinned(const LuaMadeSimple::Lua& lua)
    {
        lua.set_integer(g_pinned_sector);
        return 1;
    }

    // Lua: pin_ensure() -> true once the pin hook is armed
    int lua_pin_ensure(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        try { ok = pin_hook_ensure(); }
        catch (...) { ok = false; }
        lua.set_bool(ok);
        return 1;
    }

    // Lua: capture_tick() -> nothing. Input capture probe (msc_capture.hpp), does nothing unless strafe_probe_on.txt exists.
    int lua_capture_tick(const LuaMadeSimple::Lua&)
    {
        try { capture_tick(); }
        catch (...) { g_cap_dead = true; }
        return 0;
    }

    // Lua: check(name) -> ok, detail (see check_one)
    int lua_check(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::string detail;
        try
        {
            if (!lua.is_string(1)) detail = "check needs a name";
            else ok = check_one(std::string(lua.get_string(1)), detail);
        }
        catch (...) { ok = false; detail = "exception"; }
        lua.set_bool(ok);
        lua.set_string(detail);
        return 2;
    }

    // one scan row as a text line: id|kind|sys|loaded|docked|piloted|dock_id|hull|max_hull|order_idx|order_dock|chX,chY,chZ|locX,locY,locZ|wX,wY,wZ|world_ok|name|type|order
    // WHY: Lua splits on '|'; name, type and order are cleaned so they cannot add fields.
    std::string scan_line(const MsScanRow& r)
    {
        auto i = [](long long v) { return std::to_string(v); };
        auto v3 = [&](const long long* a) { return i(a[0]) + "," + i(a[1]) + "," + i(a[2]); };
        auto d3 = [](const double* a) { return std::to_string(a[0]) + "," + std::to_string(a[1]) + "," + std::to_string(a[2]); };
        auto txt = [](const wchar_t* w) {
            std::string s = narrow(std::wstring(w));
            for (char& c : s) if (c == '|' || c == '\n' || c == '\r') c = ' ';
            return s;
        };
        return i(r.id) + "|" + i(r.kind) + "|" + i(r.sys) + "|" + i(r.loaded) + "|" + i(r.docked) + "|" + i(r.piloted) + "|" + i(r.dock_id) + "|" +
               i(r.hull) + "|" + i(r.max_hull) + "|" + i(r.order_idx) + "|" + i(r.order_dock) + "|" + v3(r.ch) + "|" + v3(r.loc) + "|" +
               d3(r.world) + "|" + i(r.world_ok) + "|" + txt(r.name) + "|" + txt(r.type) + "|" + txt(r.order);
    }

    // Lua: scan(kind, max_age_ms, type, sys, loaded_only) -> string, one scan_line per row, ok. "" when args are wrong or on exception.
    // ok = true only for a real native scan. ok = false when args are wrong, on exception, or when the native scan is off
    // (kill switch after a fault): the Lua side then runs its own scan, so an empty "" never passes as a scan result.
    int lua_scan(const LuaMadeSimple::Lua& lua)
    {
        std::string out;
        bool ok = false;
        try
        {
            if (nums(lua, 2) && lua.is_number(4) && lua.is_number(5) && lua.is_string(3))
            {
                const int kind = arg_int(lua);
                const long long age = lua.get_integer(1);
                const std::string type(lua.get_string(1));
                const int sys = arg_int(lua);
                const bool loaded = arg_int(lua) != 0;
                const auto rows = scan_rows(kind, age < 0 ? 0 : static_cast<ULONGLONG>(age), type, sys, loaded);
                for (const MsScanRow& r : rows) out += scan_line(r) + "\n";
                ok = true;
            }
        }
        catch (...) { out.clear(); ok = false; }
        lua.set_string(out);
        lua.set_bool(ok && !g_scan_off);
        return 2;
    }

    // Lua: chunk_hold(owner, sys, X, Y, Z) -> ok, err. ok is true for a set (1) and for an unchanged cube (2).
    int lua_chunk_hold(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 5)) err = STR("chunk_hold needs owner, sys, X, Y, Z numbers");
            else
            {
                const int owner = arg_int(lua);
                const int sys = arg_int(lua);
                int64_t c[3];
                for (int i = 0; i < 3; ++i) c[i] = lua.get_integer(1);
                ok = chunk_hold(owner, sys, c, err) != 0;
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: chunk_release(owner) -> ok. Drops the owner's cube (no-op without one).
    int lua_chunk_release(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        try
        {
            if (lua.is_number(1)) ok = chunk_hold_release_owner(arg_int(lua));
        }
        catch (...) { ok = false; }
        lua.set_bool(ok);
        return 1;
    }

    // ---------------- factory natives (API 4): args consumed in order, see arg_int / arg_obj ----------------

    // next arg as bytes: "" for nil or a non-string. Always consumes the arg.
    // WHY: lua.get_string() goes through lua_tostring + strlen, so binary data stopped at the first NUL. Copy by length.
    std::string arg_bytes(const LuaMadeSimple::Lua& lua)
    {
        // lua_type, not lua_tolstring alone: a number would be converted in place and accepted as bytes
        if (lua_type(lua.get_lua_state(), 1) != LUA_TSTRING) { lua.discard_value(1); return std::string(); }
        size_t n = 0;
        const char* p = lua_tolstring(lua.get_lua_state(), 1, &n);
        std::string s(p ? p : "", n);
        lua.discard_value(1);
        return s;
    }

    // Lua: send_typed(receiver, cmd UObject, typeId) -> ok, err
    int lua_send_typed(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!lua.is_number(1) || !lua.is_userdata(2) || !lua.is_number(3)) err = STR("send_typed needs receiver, cmd, typeId");
            else
            {
                const int receiver = arg_int(lua);
                UObject* cmd = arg_obj(lua);
                const int type_id = arg_int(lua);
                ok = factory_guard([&] { return send_typed(receiver, cmd, type_id, err); }, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: find_buildable(name) -> Buildable UObject, or nil
    int lua_find_buildable(const LuaMadeSimple::Lua& lua)
    {
        UObject* o = nullptr;
        try
        {
            const std::string name = arg_bytes(lua);
            if (!name.empty() && !seh_run([&] { o = find_buildable(name); }))
            {
                o = nullptr;
                msc_log(STR("[MSCore] find_buildable: native fault looking up ") + mscl::to_wide(name) + STR(" (nil returned)"));
            }
        }
        catch (...) { o = nullptr; }
        if (!alive(o)) { lua.set_nil(); return 1; }
        LuaType::auto_construct_object(lua, o);
        return 1;
    }

    // Lua: build_place(station, buildable UObject, x, y, h, rot, cfgBytes) -> ok, err
    int lua_build_place(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!lua.is_number(1) || !lua.is_userdata(2) || !lua.is_number(3) || !lua.is_number(4) || !lua.is_number(5) || !lua.is_number(6))
                err = STR("build_place needs station, buildable, x, y, h, rot");
            else
            {
                const int station = arg_int(lua);
                UObject* bdat = arg_obj(lua);
                const int x = arg_int(lua), y = arg_int(lua), h = arg_int(lua), rot = arg_int(lua);
                const std::string cfg = arg_bytes(lua);
                ok = factory_guard([&] { return build_place(station, bdat, x, y, h, rot, cfg, err); }, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: add_block(station, classPath, x, y, h, rot) -> ok, err
    int lua_add_block(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!lua.is_number(1) || lua_type(lua.get_lua_state(), 2) != LUA_TSTRING || !lua.is_number(3) || !lua.is_number(4) || !lua.is_number(5) || !lua.is_number(6))
                err = STR("add_block needs station, classPath, x, y, h, rot");
            else
            {
                const int station = arg_int(lua);
                const std::string path = arg_bytes(lua);
                const int x = arg_int(lua), y = arg_int(lua), h = arg_int(lua), rot = arg_int(lua);
                ok = factory_guard([&] { return add_block(station, path, x, y, h, rot, err); }, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: remove_block(station, classPath, x, y, h, rot) -> ok, err
    int lua_remove_block(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!lua.is_number(1) || lua_type(lua.get_lua_state(), 2) != LUA_TSTRING || !lua.is_number(3) || !lua.is_number(4) || !lua.is_number(5) || !lua.is_number(6))
                err = STR("remove_block needs station, classPath, x, y, h, rot");
            else
            {
                const int station = arg_int(lua);
                const std::string path = arg_bytes(lua);
                const int x = arg_int(lua), y = arg_int(lua), h = arg_int(lua), rot = arg_int(lua);
                ok = factory_guard([&] { return remove_block(station, path, x, y, h, rot, err); }, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: pending(station, apply 0|1, reset 0|1, primary, player, editing) -> ok, err
    int lua_pending(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 6)) err = STR("pending needs station, apply, reset, primary, player, editing numbers");
            else
            {
                const int station = arg_int(lua);
                const bool apply = arg_int(lua) != 0;
                const bool reset = arg_int(lua) != 0;
                const int primary = arg_int(lua), player = arg_int(lua), editing = arg_int(lua);
                ok = factory_guard([&] { return pending(station, apply, reset, primary, player, editing, err); }, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: read_block_class(classPath) -> true, text | false, err   (text format: msc_station.hpp)
    int lua_read_block_class(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        std::string out;
        try {
            const std::string path = arg_bytes(lua);
            if (path.empty()) err = STR("read_block_class needs a class path string");
            else ok = factory_guard([&] { return read_block_class(path, out, err); }, err);
        } catch (...) { ok = false; err = STR("exception"); }
        lua.set_bool(ok);
        if (ok) lua.set_string(out); else lua.set_string(narrow(err));
        return 2;
    }

    // Lua: disassemble(targetId, mass) -> ok, err
    int lua_disassemble(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 2)) err = STR("disassemble needs target, mass numbers");
            else
            {
                const int target = arg_int(lua);
                const int mass = arg_int(lua);
                ok = factory_guard([&] { return disassemble(target, mass, err); }, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: set_recipe(machine, recipe UObject, amount, cfgBytes) -> ok, err
    int lua_set_recipe(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!lua.is_number(1) || !lua.is_userdata(2) || !lua.is_number(3)) err = STR("set_recipe needs machine, recipe, amount");
            else
            {
                const int machine = arg_int(lua);
                UObject* recipe = arg_obj(lua);
                const int amount = arg_int(lua);
                const std::string cfg = arg_bytes(lua);
                ok = factory_guard([&] { return set_recipe(machine, recipe, amount, cfg, err); }, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: port_toggle(machine, port) -> ok, err
    int lua_port_toggle(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 2)) err = STR("port_toggle needs machine, port numbers");
            else
            {
                const int machine = arg_int(lua);
                const int port = arg_int(lua);
                ok = factory_guard([&] { return port_toggle(machine, port, err); }, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: inv_move(primary, secondary, slot, action [, receiver]) -> ok, err. receiver = player object id (default 0; the game drops receiver 0).
    int lua_inv_move(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try
        {
            if (!nums(lua, 4)) err = STR("inv_move needs primary, secondary, slot, action numbers");
            else
            {
                const int prim = arg_int(lua), sec = arg_int(lua), slot = arg_int(lua), action = arg_int(lua);
                const int receiver = lua.is_number(1) ? arg_int(lua) : 0;   // optional 5th arg (previous four are consumed)
                ok = factory_guard([&] { return inv_move(prim, sec, slot, action, err, receiver); }, err);
            }
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: read_station(stationId) -> true, text | false, err   (text format: msc_station.hpp)
    int lua_read_station(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        std::string out;
        try {
            if (!nums(lua, 1)) err = STR("read_station needs a station id number");
            else { const int id = arg_int(lua); ok = factory_guard([&] { return read_station(id, out, err); }, err); }
        } catch (...) { ok = false; err = STR("exception"); }
        lua.set_bool(ok);
        if (ok) lua.set_string(out); else lua.set_string(narrow(err));
        return 2;
    }

    // Lua: read_docks(stationId) -> true, text | false, err   (text format: msc_station.hpp)
    int lua_read_docks(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        std::string out;
        try {
            if (!nums(lua, 1)) err = STR("read_docks needs a station id number");
            else { const int id = arg_int(lua); ok = factory_guard([&] { return read_docks(id, out, err); }, err); }
        } catch (...) { ok = false; err = STR("exception"); }
        lua.set_bool(ok);
        if (ok) lua.set_string(out); else lua.set_string(narrow(err));
        return 2;
    }

    // Lua: fleets_read(maxAgeMs int) -> text, true | reason, false   (text format: msc_fleets.hpp)
    int lua_fleets_read(const LuaMadeSimple::Lua& lua)
    {
        try {
            const long long age = nums(lua, 1) ? static_cast<long long>(arg_int(lua)) : 0;
            std::vector<MsFleetTag> t; std::vector<MsFleetShip> s;
            const int r = fleets_get(age, t, s);
            if (r == 1) { lua.set_string(fleets_text(t, s)); lua.set_bool(true); return 2; }
            lua.set_string(r == -2 ? "fleets module off" : "fleets read fault"); lua.set_bool(false); return 2;
        } catch (...) { module_disable(STR("fleets"), STR("fleets_read exception")); lua.set_string("exception"); lua.set_bool(false); return 2; }
    }

    // Lua: fleet_cmd(op string, a int, b int, name string) -> ok, err   (writes only while cap 'fleets_write' is on: the selftest arms it only for the verified CreateInputFleetTag layout; unassign verified in game 2026-11-21)
    int lua_fleet_cmd(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try {
            if (lua_type(lua.get_lua_state(), 1) != LUA_TSTRING || lua_type(lua.get_lua_state(), 2) != LUA_TNUMBER || lua_type(lua.get_lua_state(), 3) != LUA_TNUMBER || lua_type(lua.get_lua_state(), 4) != LUA_TSTRING) err = STR("fleet_cmd needs op string, a number, b number, name string");
            else { const std::string op = arg_bytes(lua); const int a = arg_int(lua); const int b = arg_int(lua); const std::string name = arg_bytes(lua); ok = factory_guard([&] { return fleet_write(op, a, b, name, err); }, err); }
        } catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: module_disable(name) -> ok   (narrow: only 'fleets', 'fleets_write' and 'rockreg'; the Lua kill switches call it)
    int lua_module_disable(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        try {
            if (lua_type(lua.get_lua_state(), 1) == LUA_TSTRING)
            {
                const std::string name = arg_bytes(lua);
                if (name == "fleets") { module_disable(STR("fleets"), STR("lua kill switch")); ok = true; }
                else if (name == "rockreg") { module_disable(STR("rockreg"), STR("lua kill switch")); ok = true; }
                else if (name == "fleets_write") { module_disable(STR("fleets_write"), STR("lua kill switch")); ok = true; }
            }
        } catch (...) { ok = false; }
        lua.set_bool(ok);
        return 1;
    }

    // Lua: fleets_probe() -> text   (FleetTagInput layout dump)
    int lua_fleets_probe(const LuaMadeSimple::Lua& lua)
    {
        std::string out;
        try { if (!seh_run([&] { out = fleets_probe_text(); })) out = "fleets_probe faulted"; }
        catch (...) { out = "fleets_probe exception"; }
        lua.set_string(out);
        return 1;
    }

    // Lua: set_dock_filter(receiverId int, index int, itemName string, bLoading 0|1) -> ok, err   (native StationConsoleInput SetFilter)
    int lua_set_dock_filter(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try {
            if (!nums(lua, 2) || lua_type(lua.get_lua_state(), 3) != LUA_TSTRING || lua_type(lua.get_lua_state(), 4) != LUA_TNUMBER) err = STR("set_dock_filter needs receiver id, index, item name string, bLoading number");
            else { const int receiver = arg_int(lua); const int index = arg_int(lua); const std::string item = arg_bytes(lua); const int loading = arg_int(lua); ok = factory_guard([&] { return set_dock_filter(receiver, index, item, loading != 0, err); }, err); }
        } catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: rename_dock(receiverId int, text string) -> ok, err   (native SetTextInputCommand; the FText never reaches Lua)
    int lua_rename_dock(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try {
            if (!nums(lua, 1) || lua_type(lua.get_lua_state(), 2) != LUA_TSTRING) err = STR("rename_dock needs receiver id number and name string");
            else { const int receiver = arg_int(lua); const std::string text = arg_bytes(lua); ok = factory_guard([&] { return rename_dock(receiver, text, err); }, err); }
        } catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: read_inventory(invId) -> true, text | false, err   (text format: msc_station.hpp)
    int lua_read_inventory(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        std::string out;
        try {
            if (!nums(lua, 1)) err = STR("read_inventory needs an inventory id number");
            else { const int id = arg_int(lua); ok = factory_guard([&] { return read_inventory(id, out, err); }, err); }
        } catch (...) { ok = false; err = STR("exception"); }
        lua.set_bool(ok);
        if (ok) lua.set_string(out); else lua.set_string(narrow(err));
        return 2;
    }

    // Lua: inv_split(invId, src, amount, dst) -> true | false, err   (0-based slots, same inventory; msc_station.hpp inv_split)
    int lua_inv_split(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        std::wstring err;
        try {
            if (!nums(lua, 4)) err = STR("inv_split needs inv id, src, amount, dst numbers");
            else {
                const int id = arg_int(lua), src = arg_int(lua), amount = arg_int(lua), dst = arg_int(lua);
                ok = factory_guard([&] { return inv_split(id, src, amount, dst, err); }, err);
            }
        } catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // Lua: export_ok(name) -> true when this DLL exports name (GetProcAddress). Read-only, for selfcheck.
    int lua_export_ok(const LuaMadeSimple::Lua& lua)
    {
        bool ok = false;
        try
        {
            const std::string name = arg_bytes(lua);
            HMODULE hm = nullptr;
            if (!name.empty() && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                    reinterpret_cast<LPCWSTR>(&kVersion), &hm) && hm)
                ok = GetProcAddress(hm, name.c_str()) != nullptr;
        }
        catch (...) { ok = false; }
        lua.set_bool(ok);
        return 1;
    }

    // Lua: refund_enable(bool) -> status string. true validates, installs the hooks and turns the PROBE log lines on, false turns them off. Default off.
    int lua_refund_enable(const LuaMadeSimple::Lua& lua)
    {
        std::string st;
        try { st = ref_set(lua_toboolean(lua.get_lua_state(), 1) != 0); } catch (...) { st = "COMPAT refund failed: exception"; }
        lua.set_string(st);
        return 1;
    }

    // Lua: refund_status() / redirect_status() -> status string ("off", "on: redirect=.. probe_log=.. groups=..", or "COMPAT refund failed: <why>").
    int lua_refund_status(const LuaMadeSimple::Lua& lua) { if (ref_expired()) ref_refresh(); lua.set_string(g_ref.status); return 1; }

    // Lua: redirect_enable(bool) -> status string. Turns the refund / charge redirect on or off. Default OFF; Blueprint Unit turns it on only while it builds.
    int lua_redirect_enable(const LuaMadeSimple::Lua& lua)
    {
        std::string st;
        const bool on = lua_toboolean(lua.get_lua_state(), 1) != 0;
        try
        {
            if (!on && g_ref.redirect && g_ref.lua_armed)
            {
                // BPU's run ended but the game still refunds: keep on, ref_expired() switches off after idle.
                g_ref.draining = true;
                ref_refresh();
                st = g_ref.status;
            }
            else
            {
                g_ref.draining = false;
                st = ref_set_redirect(on);
                if (!on) g_ref.opt_in = false;   // explicit Lua off drops the file opt-in
                // Lua-armed: every enable(true) renews (armed_ms). opt-in file on = never expires.
                g_ref.lua_armed = on && g_ref.redirect && !g_ref.opt_in;
                g_ref.armed_ms = GetTickCount64();
            }
        }
        catch (...) { st = "COMPAT refund failed: exception"; }
        lua.set_string(st);
        return 1;
    }

    // Lua: redirect_stats() -> "refund=<n> refund_fallback=<n> charge=<n> skipped=<n> group_made=<n> group_fail=<n> errors=<n>"
    int lua_redirect_stats(const LuaMadeSimple::Lua& lua)
    {
        std::string t;
        try { t = ref_stats_text(); } catch (...) {}
        lua.set_string(t);
        return 1;
    }

    // Lua: refund_log() -> the last 200 probe lines, newline separated.
    int lua_refund_log(const LuaMadeSimple::Lua& lua)
    {
        std::string t;
        try { t = ref_log_text(); } catch (...) {}
        lua.set_string(t);
        return 1;
    }

    // WHAT: at MSCore Lua start (once): redirect ON only when Mods\MSCore\refund_redirect_on.txt exists (opt-in, old always-on behaviour) and
    //       refund_redirect_off.txt does not; probe log ON when refund_probe.txt exists. The DLL sits in Mods\MSCore\dlls.
    // WHY: 0.7.5-0.8.0 turned the redirect on at start, so manual deconstruction refunds went into the Blueprint Unit instead of the player.
    void refund_file_switch()
    {
        HMODULE hm = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&kVersion), &hm)) return;
        std::vector<wchar_t> buf(32768);
        const DWORD n = GetModuleFileNameW(hm, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0 || n >= buf.size()) return;
        const mscl::fs::path dir = mscl::fs::path(std::wstring(buf.data(), n)).parent_path().parent_path();
        std::error_code ec;
        static bool dgOnce = false;   // designer guard: decided once per process (the kill-switch file is read once)
        if (!dgOnce) { dgOnce = true; if (!mscl::fs::exists(dir / L"designer_guard_off.txt", ec)) dg_install(); }
        if (mscl::fs::exists(dir / L"weapons_probe.txt", ec)) g_weapons_probe.store(true);
        if (mscl::fs::exists(dir / L"combat_on.txt", ec)) g_combat_on.store(true);
        if (mscl::fs::exists(dir / L"threat_off.txt", ec)) g_threat_off.store(true);
        if (mscl::fs::exists(dir / L"strafe_off.txt", ec)) g_strafe_off.store(true);
        if (mscl::fs::exists(dir / L"strafe_probe_on.txt", ec)) g_cap_on.store(true);
        if (mscl::fs::exists(dir / L"combat_fire_console.txt", ec)) g_fire_console.store(true);
        if (mscl::fs::exists(dir / L"refund_probe.txt", ec)) ref_set(true);
        if (mscl::fs::exists(dir / L"refund_redirect_on.txt", ec) && !mscl::fs::exists(dir / L"refund_redirect_off.txt", ec)) { ref_set_redirect(true); g_ref.opt_in = true; }
    }

    // Lua: designer_guard(bool) -> status string. true installs the Designer IsBlockAvailable null guard (done at start unless
    // designer_guard_off.txt exists), false restores the original bytes (kill switch). designer_guard_status() -> "<status> blocked=<n>".
    int lua_designer_guard(const LuaMadeSimple::Lua& lua)
    {
        try
        {
            if (lua_toboolean(lua.get_lua_state(), 1) != 0) dg_install(); else dg_uninstall("lua kill switch");
        }
        catch (...) { module_disable(STR("designer_guard"), STR("exception")); }
        lua.set_string(g_dg.status + " blocked=" + std::to_string(dg_count()));
        return 1;
    }
    int lua_designer_guard_status(const LuaMadeSimple::Lua& lua)
    {
        lua.set_string(g_dg.status + " blocked=" + std::to_string(dg_count()));
        return 1;
    }

    // ---------------- loader: mod update checks (worker thread; Lua reads the rows) ----------------

    struct LoaderRow { std::string name, installed, remote, state; };
    std::mutex g_loader_mu;
    std::vector<LoaderRow> g_loader_rows;
    std::atomic<bool> g_loader_stop{false};

    // WHAT: '|' and newlines would break the status line format
    std::string clean(std::string s) { for (char& c : s) if (c == '|' || c == '\n' || c == '\r') c = ' '; return s; }

    // WHAT: win64 dir from this DLL's path (...\Win64\ue4ss\Mods\MSCore\dlls\main.dll). Empty when the DLL is elsewhere: loader stays off.
    mscl::fs::path win64_here()
    {
        HMODULE hm = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&kVersion), &hm)) return {};
        std::vector<wchar_t> buf(32768);
        const DWORD n = GetModuleFileNameW(hm, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0 || n >= buf.size()) return {};
        // main.dll -> dlls -> MSCore -> Mods -> ue4ss -> Win64
        mscl::fs::path cur(std::wstring(buf.data(), n));
        const char* want[5] = {"main.dll", "dlls", "MSCore", "Mods", "ue4ss"};
        for (int i = 0; i < 5; ++i)
        {
            if (mscl::lower(mscl::to_utf8(cur.filename().wstring())) != want[i]) return {};
            cur = cur.parent_path();
        }
        return cur;
    }

    // WHAT: worker body, scan + check only (never downloads or stages; updates are user-initiated in the manager). Only std/loader code, never UE objects. Stops between network steps when g_loader_stop is set.
    void loader_run(const mscl::Paths& paths)
    {
        auto say = [](const std::string& s) { msc_log(mscl::to_wide(s)); };
        const auto mods = mscl::scan_mods(paths);
        {
            std::lock_guard<std::mutex> g(g_loader_mu);
            g_loader_rows.clear();
            for (const auto& m : mods) g_loader_rows.push_back({m.name, m.version, "", "pending"});
        }
        for (const auto& m : mods)
        {
            if (g_loader_stop) return;
            mscl::Check c = mscl::check_mod(m, kVersion);
            const std::string state = c.state;
            if (state == "update") say("[MSCore] update available " + m.name + " " + m.version + "->" + c.m.version);
            else if (state.compare(0, 6, "error:") == 0) say("[MSCore] loader: " + m.name + " " + state);
            std::lock_guard<std::mutex> g(g_loader_mu);
            for (auto& r : g_loader_rows)
                if (r.name == m.name) { r.remote = c.m.version; r.state = clean(state); }
        }
    }

    void loader_worker(mscl::Paths paths)
    {
        try { loader_run(paths); }
        catch (...) { msc_log(STR("[MSCore] loader: failed")); }
        std::lock_guard<std::mutex> g(g_loader_mu);
        for (auto& r : g_loader_rows) if (r.state == "pending") r.state = "error:failed";
    }

    // WHAT: applies pending updates (DLL ctor, before Lua), then starts the detached check thread
    void loader_start()
    {
        const mscl::fs::path win64 = win64_here();
        if (win64.empty()) { msc_log(STR("loader: off (DLL not under ue4ss\\Mods\\MSCore\\dlls)")); return; }
        const mscl::Paths paths = mscl::paths_from_win64(win64);
        try { mscl::apply_pending(paths, [](const std::string& s) { msc_log(mscl::to_wide(s)); }); }
        catch (...) { msc_log(STR("[MSCore] apply pending failed")); }
        try { std::thread(loader_worker, paths).detach(); }
        catch (...) { msc_log(STR("[MSCore] loader thread failed")); }
    }

    // Lua: loader_status() -> string, one line per mod: name|installed|remote|state. Empty before the scan.
    int lua_loader_status(const LuaMadeSimple::Lua& lua)
    {
        std::string out;
        {
            std::lock_guard<std::mutex> g(g_loader_mu);
            for (const auto& r : g_loader_rows)
                out += clean(r.name) + "|" + clean(r.installed) + "|" + clean(r.remote) + "|" + clean(r.state) + "\n";
        }
        lua.set_string(out);
        return 1;
    }
}

// WHAT: registers the MSCoreNative_* Lua globals in one Lua state, once per state (guard by lua_State*).
// WHY: UE4SS fires the unnamed on_lua_start only for the Lua mod named MSCore, so other mods (BlueprintUnit) never got these globals. The named on_lua_start calls this for every started Lua mod.
static void register_globals(LuaMadeSimple::Lua& lua, const std::wstring& mod_name)
{
    static std::mutex mu;
    static std::set<void*> done;
    {
        std::lock_guard<std::mutex> g(mu);
        if (!done.insert(lua.get_lua_state()).second) return;
    }
    // table add_pair functions need userdata self; global functions don't. mscore.lua gathers them into MSCore.native
    lua.register_function("MSCoreNative_version", lua_version);
    lua.register_function("MSCoreNative_alive", lua_alive);
    lua.register_function("MSCoreNative_send_cmd", lua_send_cmd);
    lua.register_function("MSCoreNative_view_send", lua_view_send);
    lua.register_function("MSCoreNative_throttle", lua_throttle);
    lua.register_function("MSCoreNative_offsets", lua_offsets);
    lua.register_function("MSCoreNative_check_offsets", lua_check_offsets);
    lua.register_function("MSCoreNative_new_order", lua_new_order);
    lua.register_function("MSCoreNative_update_order", lua_update_order);
    lua.register_function("MSCoreNative_delete_order", lua_delete_order);
    lua.register_function("MSCoreNative_autopilot", lua_autopilot);
    lua.register_function("MSCoreNative_skip_to", lua_skip_to);
    lua.register_function("MSCoreNative_filter_send", lua_filter_send);
    lua.register_function("MSCoreNative_warp_send", lua_warp_send);
    lua.register_function("MSCoreNative_pinned", lua_pinned);
    lua.register_function("MSCoreNative_pin_ensure", lua_pin_ensure);
    lua.register_function("MSCoreNative_capture_tick", lua_capture_tick);
    lua.register_function("MSCoreNative_check", lua_check);
    lua.register_function("MSCoreNative_loader_status", lua_loader_status);
    lua.register_function("MSCoreNative_scan", lua_scan);
    lua.register_function("MSCoreNative_chunk_hold", lua_chunk_hold);
    lua.register_function("MSCoreNative_chunk_release", lua_chunk_release);
    lua.register_function("MSCoreNative_send_typed", lua_send_typed);
    lua.register_function("MSCoreNative_find_buildable", lua_find_buildable);
    lua.register_function("MSCoreNative_build_place", lua_build_place);
    lua.register_function("MSCoreNative_add_block", lua_add_block);
    lua.register_function("MSCoreNative_remove_block", lua_remove_block);
    lua.register_function("MSCoreNative_pending", lua_pending);
    lua.register_function("MSCoreNative_read_block_class", lua_read_block_class);
    lua.register_function("MSCoreNative_disassemble", lua_disassemble);
    lua.register_function("MSCoreNative_set_recipe", lua_set_recipe);
    lua.register_function("MSCoreNative_port_toggle", lua_port_toggle);
    lua.register_function("MSCoreNative_inv_move", lua_inv_move);
    lua.register_function("MSCoreNative_read_station", lua_read_station);
    lua.register_function("MSCoreNative_read_inventory", lua_read_inventory);
    lua.register_function("MSCoreNative_read_docks", lua_read_docks);
    lua.register_function("MSCoreNative_fleets_read", lua_fleets_read);
    lua.register_function("MSCoreNative_fleet_cmd", lua_fleet_cmd);
    lua.register_function("MSCoreNative_module_disable", lua_module_disable);
    lua.register_function("MSCoreNative_fleets_probe", lua_fleets_probe);
    lua.register_function("MSCoreNative_rename_dock", lua_rename_dock);
    lua.register_function("MSCoreNative_set_dock_filter", lua_set_dock_filter);
    lua.register_function("MSCoreNative_inv_split", lua_inv_split);
    lua.register_function("MSCoreNative_export_ok", lua_export_ok);
    lua.register_function("MSCoreNative_refund_enable", lua_refund_enable);
    lua.register_function("MSCoreNative_refund_status", lua_refund_status);
    lua.register_function("MSCoreNative_refund_log", lua_refund_log);
    lua.register_function("MSCoreNative_redirect_enable", lua_redirect_enable);
    lua.register_function("MSCoreNative_redirect_status", lua_refund_status);
    lua.register_function("MSCoreNative_redirect_stats", lua_redirect_stats);
    lua.register_function("MSCoreNative_designer_guard", lua_designer_guard);
    lua.register_function("MSCoreNative_designer_guard_status", lua_designer_guard_status);
    Output::send<LogLevel::Normal>(STR("[MSCore] Lua globals registered for mod {}\n"), mod_name);
}

class MSCoreMod : public RC::CppUserModBase
{
public:
    MSCoreMod() : CppUserModBase()
    {
        ModName = STR("MSCore");
        ModVersion = STR("0.8.15");
        ModDescription = STR("MSCore native helpers for Main Sequence mods");
        ModAuthors = STR("local");
        Output::send<LogLevel::Normal>(STR("[MSCore] native DLL loaded\n"));
        loader_start();
    }

    ~MSCoreMod()
    {
        g_loader_stop = true;   // detached worker checks this between network steps
        try { ref_uninstall(); } catch (...) {}
        try { dg_restore_bytes(); } catch (...) {}
        try { rr_listener_release(); } catch (...) {}
        try { chunk_hold_release_all(); }
        catch (...) { msc_log(STR("chunk_hold_release_all failed at unload")); }
    }

    auto on_lua_start(LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua*) -> void override
    {
        register_globals(lua, STR("MSCore"));
        try { static bool once = false; if (!once) { once = true; refund_file_switch(); } } catch (...) {}
        // WHAT: one startup line of the capabilities. split and refund offsets resolve on first use, so they are not in it yet.
        try { static bool caps_once = false; if (!caps_once) { caps_once = true; nat_resolve(); scan_resolve(); rr_resolve(); ships_resolve(); flight_resolve(); repair_resolve(); combat_resolve(); threat_resolve(); strafe_resolve(); world_resolve(); stnames_resolve(); fleets_resolve(); msc_log(STR("COMPAT CAPS ") + widen(caps_text()) + STR("(split/refund offsets resolve on first use)")); } } catch (...) {}
    }

    auto on_lua_start(RC::StringViewType mod_name, LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua*) -> void override
    {
        register_globals(lua, std::wstring(mod_name));
    }
};

#define MSCORE_API __declspec(dllexport)
extern "C"
{
    MSCORE_API RC::CppUserModBase* start_mod() { try { return new MSCoreMod(); } catch (...) { module_disable(STR("misc"), STR("start_mod exception")); return nullptr; } }
    MSCORE_API void uninstall_mod(RC::CppUserModBase* mod) { try { delete mod; } catch (...) { module_disable(STR("misc"), STR("uninstall_mod exception")); } }

    // C API for other native mods (MiningBenchMod). Each call returns 1 on success, 0 on failure; err gets the reason (errlen wide chars).
    // Native ABI counter, separate from the Lua API (MSCore.api in mscore.lua, now 3).
    // MSCore_api: 3 = scan and chunk hold (mining refuses to load below 3 when it needs them); 4 = factory exports below; 5 = add_block, pending, read_block_class (Lua only); 6 = read_docks and the block id column of read_station (Lua only); 7 = MSCore_chunk_hold2 (chunk radius); 8 = MSCore_class, MSCore_scan_rocks; 9 = MSCore_ship_shields; 10 = MSCore_ship_flight, MSCore_ship_set_throttle, MSCore_ship_zero, MSCore_ship_console, MSCore_actor_loc (10 also has MSCore_world_paused); 11 = MSCore_ship_repair, MSCore_ship_repair_need; 12 = station names export (MSCore_stations); 13 = fleet exports (MSCore_fleet_tags, MSCore_fleet_ships; Lua natives fleets_read, fleet_cmd, fleets_probe); 14 = MSCore_ship_hull; 15 = MSCore_ship_mining_caps, MSCore_presence_step; 16 = MSCore_rockreg_* (rock registry service). set_dock_filter is a Lua native only (no C export, number unchanged).
    MSCORE_API int MSCore_api() { try { return mscl::kNativeApi; } catch (...) { module_disable(STR("misc"), STR("MSCore_api exception")); return 0; } }   // 8 = MSCore_class, MSCore_scan_rocks; 9 = MSCore_ship_shields; 10 = flight exports; 11 = repair exports; 12 = station names export; 13 = fleet exports (MSCore_fleet_tags, MSCore_fleet_ships; Lua natives fleets_read, fleet_cmd, fleets_probe); 14 = MSCore_ship_hull; 15 = MSCore_ship_mining_caps, MSCore_presence_step; 16 = MSCore_rockreg_* (rock registry service); 17 = MSCore_ship_caps; 18 = MSCore_enemies_near, MSCore_ship_fire, MSCore_ship_weapon_range (stub); combat module is off until Mods\MSCore\combat_on.txt exists; 19 = MSCore_dock_kind; 20 = MSCore_spawn_practice_enemy (combat module); 21 = MSCore_ship_threat (threat module, on unless Mods\MSCore\threat_off.txt exists); 22 = MSCore_ship_strafe, MSCore_ship_strafe_zero, MSCore_strafe_tick (strafe module, on unless Mods\MSCore\strafe_off.txt exists); 23 = MSCore_enemy_velocity, MSCore_enemy_aim (combat module)

    MSCORE_API int MSCore_add_input(int receiver, void* cmd, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            UObject* o = static_cast<UObject*>(cmd);
            if (!alive(o)) { e = STR("cmd is not alive"); return false; }
            return add_input(receiver, o, e);
        }, err, errlen);
    }

    MSCORE_API int MSCore_view_send(int cid, long long yaw_raw, long long pitch_raw, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return view_send(cid, yaw_raw, pitch_raw, e); }, err, errlen);
    }

    MSCORE_API int MSCore_throttle(int sid, int delta, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return send_ship_throttle(sid, delta, e); }, err, errlen);
    }

    // item object for a resource name or numeric id; nullptr with why set on zero or several matches
    MSCORE_API void* MSCore_find_item(const char* res, wchar_t* why, int len)
    {
        std::wstring w;
        UObject* o = nullptr;
        try
        {
            if (res) o = find_item(res, w);
            else w = STR("null resource");
        }
        catch (...) { o = nullptr; w = STR("exception"); module_disable(STR("misc"), STR("find_item exception")); }
        copy_err(w, why, len);
        return o;
    }

    // dock id for an in-game dock name: >0 id, -1 none, -2 ambiguous (why says which)
    MSCORE_API int MSCore_find_dock(const char* name, wchar_t* why, int len)
    {
        std::wstring w;
        int id = -1;
        try
        {
            if (name) id = find_dock_by_name(name, w);
            else w = STR("null dock name");
        }
        catch (...) { id = -1; w = STR("exception"); module_disable(STR("misc"), STR("find_dock exception")); }
        copy_err(w, why, len);
        return id;
    }

    MSCORE_API int MSCore_new_order(int receiver, const wchar_t* cls, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            if (!cls) { e = STR("null cls"); return false; }
            return new_order(receiver, cls, e);
        }, err, errlen);
    }

    MSCORE_API int MSCore_update_order(int oid, int target, void* item, int cmp, int amount, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            UObject* it = static_cast<UObject*>(item);
            if (item && !alive(it)) { e = STR("item is not alive"); return false; }
            return update_order(oid, target, it, cmp, amount, e);
        }, err, errlen);
    }

    MSCORE_API int MSCore_delete_order(int sid, int oid, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return delete_order(sid, oid, e); }, err, errlen);
    }

    MSCORE_API int MSCore_autopilot(int sid, int on, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return set_autopilot(sid, on != 0, e); }, err, errlen);
    }

    // before/after = CurrentOrderIndex around the SkipToOrder call (either pointer may be null)
    MSCORE_API int MSCore_skip_to(int sid, int idx, int* before, int* after, wchar_t* err, int errlen)
    {
        int32_t b = -1, a = -1;
        const int ok = export_call([&](std::wstring& e) { return skip_to(sid, idx, b, a, e); }, err, errlen);
        if (before) *before = b;
        if (after) *after = a;
        return ok;
    }

    MSCORE_API int MSCore_filter_send(int sid, int idx, void* item, int amount, int action, int remove, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            UObject* it = static_cast<UObject*>(item);
            if (item && !alive(it)) { e = STR("item is not alive"); return false; }
            return filter_send(sid, idx, it, amount, action, remove != 0, e);
        }, err, errlen);
    }

    // count of ship's logistic filter rows, or -1. out gets min(count, max) rows; out = nullptr or max = 0: count only.
    MSCORE_API int MSCore_filters_read(void* ship, MsFilterRow* out, int max)
    {
        std::vector<MsFilterRow> rows;
        try { if (!filters_read(static_cast<UObject*>(ship), rows)) return -1; }
        catch (...) { module_disable(STR("misc"), STR("filters_read exception")); return -1; }
        const int n = static_cast<int>(rows.size());
        if (out && max > 0) for (int i = 0; i < n && i < max; ++i) out[i] = rows[i];
        return n;
    }

    MSCORE_API int MSCore_warp_send(int sid, int sys, const long long* ch3, const long long* arr3, int wrap, int set_target, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            if (!ch3 || !arr3) { e = STR("null chunk or location"); return false; }
            return warp_send(sid, sys, ch3, arr3, wrap != 0, e, set_target != 0);
        }, err, errlen);
    }

    // ship's jump arrival point in a sector: arr3 gets the raw PVector
    MSCORE_API int MSCore_arrival(void* ship, int sys, const long long* ch3, long long dist, long long* arr3, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            UObject* o = static_cast<UObject*>(ship);
            if (!alive(o) || !ch3 || !arr3) { e = STR("ship, chunk or location null or dead"); return false; }
            return arrival_of(o, sys, ch3, dist, arr3, e);
        }, err, errlen);
    }

    MSCORE_API int MSCore_jump_cost(void* ship, int sys, const long long* ch3, int* cost, wchar_t* err, int errlen)
    {
        int32_t c = 0;
        const int ok = export_call([&](std::wstring& e) {
            if (!ch3) { e = STR("null chunk"); return false; }
            return jump_cost(static_cast<UObject*>(ship), sys, ch3, c, e);
        }, err, errlen);
        if (cost) *cost = c;
        return ok;
    }

    // 1 + ch3 = chunk of obj (GetChunkLocation or ChunkLocation), 0 when unreadable
    MSCORE_API int MSCore_chunk_of(void* obj, long long* ch3)
    {
        try { return ch3 && get_chunk(static_cast<UObject*>(obj), ch3) ? 1 : 0; }
        catch (...) { module_disable(STR("chunkhold"), STR("chunk_of exception")); return 0; }
    }

    // 1 + out3 = struct property prop (X, Y, Z) of obj, raw; 0 when missing
    MSCORE_API int MSCore_axes_of(void* obj, const wchar_t* prop, long long* out3)
    {
        UObject* o = static_cast<UObject*>(obj);
        if (!alive(o) || !prop || !out3) return 0;
        try { return sec_axes(o, prop, out3) ? 1 : 0; }
        catch (...) { module_disable(STR("misc"), STR("axes_of exception")); return 0; }
    }

    MSCORE_API int MSCore_sys_id(void* obj)
    {
        UObject* o = static_cast<UObject*>(obj);
        try { return alive(o) ? sys_id(o) : 0; }
        catch (...) { module_disable(STR("misc"), STR("sys_id exception")); return 0; }
    }

    // 1 + out = GenParams.WarpArrivalDistance of a sector (raw); 0 when missing
    MSCORE_API int MSCore_sec_warp_dist(void* sec, long long* out)
    {
        if (!out) return 0;
        try { return sec_warp_dist(static_cast<UObject*>(sec), *out) ? 1 : 0; }
        catch (...) { module_disable(STR("misc"), STR("sec_warp_dist exception")); return 0; }
    }

    // sectors into out[0..max); returns the total found (-1 on exception). out = nullptr or max = 0: count only.
    MSCORE_API int MSCore_sectors(MsSector* out, int max)
    {
        try { return sectors_fill(out, max); }
        catch (...) { module_disable(STR("misc"), STR("sectors exception")); return -1; }
    }

    // 1 when the map-pin hook is armed (call every tick until it returns 1)
    MSCORE_API int MSCore_pin_ensure()
    {
        try { return pin_hook_ensure() ? 1 : 0; }
        catch (...) { module_disable(STR("misc"), STR("pin_ensure exception")); return 0; }
    }

    // last map-pinned sector id, 0 = none
    MSCORE_API int MSCore_pinned() { try { return g_pinned_sector; } catch (...) { module_disable(STR("misc"), STR("pinned exception")); return 0; } }

    // scanned rows of kind (0 ship, 1 asteroid, 2 station) that pass type and sys filters: returns the total, or -1 on exception.
    // out gets min(total, max) rows; out = nullptr or max = 0 counts only. loaded_only: asteroid kind = loaded rock actors only.
    MSCORE_API int MSCore_scan(int kind, long long max_age_ms, const char* type, int sys, int loaded_only, MsScanRow* out, int max)
    {
        try
        {
            const auto rows = scan_rows(kind, max_age_ms < 0 ? 0 : static_cast<ULONGLONG>(max_age_ms), type ? std::string(type) : std::string(),
                                        sys, loaded_only != 0);
            const int n = static_cast<int>(rows.size());
            if (out && max > 0) for (int i = 0; i < n && i < max; ++i) out[i] = rows[i];
            return n;
        }
        catch (...) { module_disable(STR("misc"), STR("scan exception")); return -1; }
    }

    // 1 = native scan on, 0 = off for this session (a walk faulted: MSCore_scan returns 0 rows; MSCore.scan uses its Lua fallback).
    MSCORE_API int MSCore_scan_ok() { try { return g_scan_off ? 0 : 1; } catch (...) { module_disable(STR("misc"), STR("scan_ok exception")); return 0; } }

    // class name of a logical name (API 8): 1 = found (out = ASCII, NUL-terminated), 0 = unknown name or out too small.
    MSCORE_API int MSCore_class(const char* logical, char* out, int outlen)
    {
        try { if (!logical || !out || outlen <= 0) return 0; const char* n = cls_name(logical);
              if (!n || static_cast<int>(std::strlen(n)) >= outlen) return 0;
              strcpy_s(out, static_cast<size_t>(outlen), n); return 1; }
        catch (...) { module_disable(STR("misc"), STR("class exception")); return 0; }
    }

    // ship shields (API 9): cur / max in whole units (-1 = unreadable). Returns bitmask 1 = cur ok, 2 = max ok, 0 = none readable (or ship not alive).
    // -2 when the ships module is off, -1 on exception. cur / max may be NULL.
    MSCORE_API int MSCore_ship_shields(void* ship, int* cur, int* max)
    {
        try { if (!module_ok(STR("ships"))) return -2;
              int32_t c = -1, m = -1;
              const int r = ship_shields_read(static_cast<UObject*>(ship), c, m);
              if (cur) *cur = c;
              if (max) *max = m;
              return r; }
        catch (...) { module_disable(STR("ships"), STR("ship_shields exception")); return -1; }
    }

    // ship hull (API 14): cur / max (GetCurrentHull / GetTotalHull) in whole units (-1 = unreadable). Returns bitmask 1 = cur ok, 2 = max ok, 0 = none readable (or ship not alive). Call from the game thread.
    // -2 when the ships module is off, -1 on exception. cur / max may be NULL.
    MSCORE_API int MSCore_ship_hull(void* ship, int* cur, int* max)
    {
        try { if (!module_ok(STR("ships"))) return -2;
              int32_t c = -1, m = -1;
              ship_hull_read(static_cast<UObject*>(ship), c, m);
              if (cur) *cur = c;
              if (max) *max = m;
              return (c >= 0 ? 1 : 0) | (m >= 0 ? 2 : 0); }
        catch (...) { module_disable(STR("ships"), STR("ship_hull exception")); return -1; }
    }

    // ship mining caps (API 15): bitmask 1 = MiningLaser, 2 = ItemScooper, 0 = readable with no mining weapon.
    // -1 unknown (ship null or dead, Weapons unreadable, count out of 0..100000, native fault), -2 ships module off, -1 on exception. Call from the game thread.
    MSCORE_API int MSCore_ship_mining_caps(void* ship)
    {
        try { if (!module_ok(STR("ships"))) return -2; return ship_mining_caps_read(static_cast<UObject*>(ship)); }
        catch (...) { module_disable(STR("ships"), STR("ship_mining_caps exception")); return -1; }
    }

    // ship caps (API 17): bitmask 1 mining laser, 2 scooper, 4 attack weapon, 8 auto defence (never armed), 16 tractor; 32 reserved (cargo, 0 for now). 0 = readable, none. -1 unknown, -2 ships module off.
    MSCORE_API int MSCore_ship_caps(void* ship)
    {
        try { if (!module_ok(STR("ships"))) return -2; return ship_caps_read(static_cast<UObject*>(ship)); }
        catch (...) { module_disable(STR("ships"), STR("ship_caps exception")); return -1; }
    }

    // combat (API 18, off until Mods\MSCore\combat_on.txt exists): enemy ships within radius_m of ship sid, nearest first, into out (max rows). Returns the total count, -1 fault, -2 combat off.
    MSCORE_API int MSCore_enemies_near(int sid, double radius_m, MsEnemy* out, int max)
    {
        try { return enemies_near(sid, radius_m, out, max); }
        catch (...) { module_disable(STR("combat"), STR("enemies_near exception")); return -1; }
    }

    // combat (API 18): fire primary weapons of ship sid on / off (command sent on change only). 1 ok, 0 fail (err says why).
    MSCORE_API int MSCore_ship_fire(int sid, int on, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return ship_fire(sid, on != 0, e); }, err, errlen, STR("combat"));
    }

    // combat (API 20): spawn a practice enemy near ship sid (game's own creative-enemy command). 1 ok, 0 fail (err says why). Off until combat_on.txt.
    MSCORE_API int MSCore_spawn_practice_enemy(int sid, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return combat_spawn_enemy(sid, e); }, err, errlen, STR("combat"));
    }

    // combat (API 18): weapon range in metres. STUB: -1 unknown, -2 combat off.
    MSCORE_API int MSCore_ship_weapon_range(int sid)
    {
        try { return ship_weapon_range(sid); }
        catch (...) { module_disable(STR("combat"), STR("ship_weapon_range exception")); return -1; }
    }

    // threat (API 21, on unless Mods\MSCore	hreat_off.txt exists): IsShipInCombat + TargettedBy of ship sid. hostile_n = entries within range_m, far_n = beyond range_m or unresolved,
    // nearest_d_m = nearest hostile in metres (-1 none). Any out pointer may be null. 1 ok, 0 fail or off.
    MSCORE_API int MSCore_ship_threat(int sid, double range_m, int* hostile_n, int* far_n, int* in_combat, double* nearest_d_m)
    {
        try { return ship_threat(sid, range_m, hostile_n, far_n, in_combat, nearest_d_m); }
        catch (...) { module_disable(STR("threat"), STR("ship_threat exception")); return 0; }
    }

    // strafe (API 22, on unless Mods\MSCore\strafe_off.txt exists): axis 0 = right(+)/left(-), 1 = up(+)/down(-); value -1..1 (|v| > 0.3 = push, magnitude ignored, unverified). Game slots: right = StrafeImpulse Y, up = X (live-verified). Fleet must call MSCore_strafe_tick about once a second while strafing.
    // Held by the game until changed: call MSCore_strafe_tick about once a second while strafing (a strafe not refreshed for 10 s is auto-zeroed). 1 ok, 0 fail (err says why).
    MSCORE_API int MSCore_ship_strafe(int sid, int axis, double value, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return ship_strafe(sid, axis, value, e); }, err, errlen, STR("strafe"));
    }

    // combat (API 23, needs combat on): enemy velocity from EnemyShipObject.CachedVelocity. v_mps[3] = metres per second (per-frame * kSimFps = 60, UNVERIFIED), v_per_frame[3] (or null) = metres per frame. 1 ok, 0 fail (err says why).
    MSCORE_API int MSCore_enemy_velocity(int enemy_id, double* v_mps, double* v_per_frame, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return enemy_velocity(enemy_id, v_mps, v_per_frame, e); }, err, errlen, STR("combat"));
    }

    // combat (API 23): enemy aim calibration, out[4] = AlignmentToTarget, RequiredAlignmentToFire (0..1), AttackDistance m, ReleaseTargetDistance m; missing prop = -1. 1 ok, 0 fail.
    MSCORE_API int MSCore_enemy_aim(int enemy_id, double* out, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return enemy_aim(enemy_id, out, e); }, err, errlen, STR("combat"));
    }

    // strafe (API 22): both axes 0 for ship sid (one zero command if either was non-zero). 1 ok, 0 fail.
    MSCORE_API int MSCore_ship_strafe_zero(int sid)
    {
        try { ship_strafe_zero(sid); return 1; }
        catch (...) { module_disable(STR("strafe"), STR("ship_strafe_zero exception")); return 0; }
    }

    // strafe (API 22): watchdog, zeroes strafes older than 10 s and forgets gone ships. Returns how many ships were zeroed, -1 fault.
    MSCORE_API int MSCore_strafe_tick()
    {
        try { return strafe_watchdog(); }
        catch (...) { module_disable(STR("strafe"), STR("strafe_tick exception")); return -1; }
    }

    // dock kind (API 19): 1 industrial, 0 normal, -1 unknown, -2 stations module off.
    MSCORE_API int MSCore_dock_kind(void* dock)
    {
        try { if (!module_ok(STR("stations"))) return -2; return dock_kind_read(static_cast<UObject*>(dock)); }
        catch (...) { module_disable(STR("stations"), STR("dock_kind exception")); return -1; }
    }

    // presence debounce (API 15): pure, no game access. read 1 present, 0 absent, -1 unknown. Returns the listed flag (1 / 0); bad_out (may be null) gets the absent count (0..3).
    MSCORE_API int MSCore_presence_step(int bad_n, int listed, int read, int* bad_out)
    {
        return presence_step(bad_n, listed, read, bad_out);
    }

    // world paused (API 10): 1 paused, 0 running, -1 unknown or exception, -2 world module off. Mods treat only 1 as paused.
    MSCORE_API int MSCore_world_paused()
    {
        try { if (!module_ok(STR("world"))) return -2; return world_paused_read(); }
        catch (...) { module_disable(STR("world"), STR("world_paused exception")); return -1; }
    }

    // station names (API 12): n stations written (at most max), or total count when out is null or max <= 0; -2 stations module off, -1 fault or exception. Each call does a full scan (count call + fill call = two scans).
    MSCORE_API int MSCore_stations(MsStationName* out, int max)
    {
        try
        {
            if (!module_ok(STR("stations"))) return -2;
            std::vector<MsStationName> rows;
            if (!stations_read(rows)) { module_disable(STR("stations"), STR("stations read fault")); return -1; }
            const int n = static_cast<int>(rows.size());
            if (out && max > 0) for (int i = 0; i < n && i < max; ++i) out[i] = rows[i];
            return n;
        }
        catch (...) { module_disable(STR("stations"), STR("stations exception")); return -1; }
    }

    // fleet tags (API 13): n tags written (at most max), or total count when out is null or max <= 0; -2 fleets module off, -1 fault or exception. max_age_ms: 0 = rescan, cap 10000.
    MSCORE_API int MSCore_fleet_tags(MsFleetTag* out, int max, int max_age_ms)
    {
        try
        {
            std::vector<MsFleetTag> t; std::vector<MsFleetShip> s;
            const int r = fleets_get(max_age_ms, t, s);
            if (r != 1) return r;
            const int n = static_cast<int>(t.size());
            if (out && max > 0) for (int i = 0; i < n && i < max; ++i) out[i] = t[i];
            return n;
        }
        catch (...) { module_disable(STR("fleets"), STR("fleet_tags exception")); return -1; }
    }

    // fleet ships (API 13): ships with at least one fleet tag, same return rules as MSCore_fleet_tags.
    MSCORE_API int MSCore_fleet_ships(MsFleetShip* out, int max, int max_age_ms)
    {
        try
        {
            std::vector<MsFleetTag> t; std::vector<MsFleetShip> s;
            const int r = fleets_get(max_age_ms, t, s);
            if (r != 1) return r;
            const int n = static_cast<int>(s.size());
            if (out && max > 0) for (int i = 0; i < n && i < max; ++i) out[i] = s[i];
            return n;
        }
        catch (...) { module_disable(STR("fleets"), STR("fleet_ships exception")); return -1; }
    }

    // flight state of ship sid (API 10): mask = FL_* groups, out = MsFlight with out->size = sizeof(MsFlight).
    // 1 read, 0 ship gone, -1 exception or a group faulted (module flight is then off), -2 flight module off, -3 bad size or null out.
    MSCORE_API int MSCore_ship_flight(int sid, int mask, MsFlight* out)
    {
        try {
            if (!module_ok(STR("flight"))) return -2;
            if (!out || out->size != static_cast<int32_t>(sizeof(MsFlight))) return -3;
            int r = 0;
            if (!seh_run([&] { r = ship_flight_read(sid, mask, *out); })) r = -1;
            if (r < 0) { module_disable(STR("flight"), STR("ship_flight fault")); return -1; }
            return r;
        }
        catch (...) { module_disable(STR("flight"), STR("ship_flight exception")); return -1; }
    }

    // Throttle = step on ship sid (IntProperty). 1 ok, 0 fail (err says why). err buffer errlen wide chars.
    MSCORE_API int MSCore_ship_set_throttle(int sid, int step, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            if (!module_ok(STR("flight"))) { e = STR("flight module off"); return false; }
            return flight_set_throttle(sid, step, e);
        }, err, errlen, STR("flight"));
    }

    // stop ship sid: Throttle = 0 and RotationImpulseInput xyz = 0. 1 ok, 0 fail (err says why).
    MSCORE_API int MSCore_ship_zero(int sid, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            if (!module_ok(STR("flight"))) { e = STR("flight module off"); return false; }
            return flight_zero(sid, e);
        }, err, errlen, STR("flight"));
    }

    // repair ship sid with the game's RepairFrameInputCommand(count, bCreative): creative 1 = free (no repair items used). 1 ok, 0 fail (err says why).
    MSCORE_API int MSCore_ship_repair(int sid, int count, int creative, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return ship_repair(sid, count, creative != 0, e); }, err, errlen, STR("repair"));
    }

    // repair items needed to fully repair ship sid: ceil((GetTotalHull - GetCurrentHull) / 10). 0 full, -1 unreadable or exception, -2 repair module off.
    MSCORE_API int MSCore_ship_repair_need(int sid)
    {
        try {
            if (!module_ok(STR("repair"))) return -2;
            int r = -1;
            if (!seh_run([&] { r = ship_repair_need(sid); })) { module_disable(STR("repair"), STR("repair_need fault")); return -1; }
            return r;
        }
        catch (...) { module_disable(STR("repair"), STR("repair_need exception")); return -1; }
    }

    // PilotConsoleObject id of ship sid (ShipID first, then ParentID). >0 id, 0 none, -2 flight module off, -1 exception.
    MSCORE_API int MSCore_ship_console(int sid)
    {
        try {
            if (!module_ok(STR("flight"))) return -2;
            int id = 0;
            if (!seh_run([&] { id = flight_console_id(sid); })) { module_disable(STR("flight"), STR("ship_console fault")); return -1; }
            return id;
        }
        catch (...) { module_disable(STR("flight"), STR("ship_console exception")); return -1; }
    }

    // K2_GetActorLocation of an actor into out3 (uu, 3 doubles). 1 ok, 0 fail, -2 flight module off, -1 exception or fault.
    MSCORE_API int MSCore_actor_loc(void* actor, double* out3)
    {
        try {
            if (!module_ok(STR("flight"))) return -2;
            if (!out3) return 0;
            int r = 0;
            if (!seh_run([&] { std::wstring why; double v[3] = {0, 0, 0}; if (flight_actor_loc(static_cast<UObject*>(actor), v, why)) { std::copy(v, v + 3, out3); r = 1; } })) { module_disable(STR("flight"), STR("actor_loc fault")); return -1; }
            return r;
        }
        catch (...) { module_disable(STR("flight"), STR("actor_loc exception")); return -1; }
    }

    // asteroid rows of solar system sys (0 = all; loaded_only 1 = rock actors in the world only, cached up to 1000 ms).
    // returns the total, -1 on exception, -2 when the rocks module is off. out gets min(total, max) rows; out = nullptr or max = 0 counts only.
    MSCORE_API int MSCore_scan_rocks(int sys, int loaded_only, MsScanRow* out, int max)
    {
        try { if (!module_ok(STR("rocks")) || g_scan_off) return -2;
              const auto rows = scan_rows(1, 1000, std::string(), sys, loaded_only != 0);
              const int n = static_cast<int>(rows.size());
              if (out && max > 0) for (int i = 0; i < n && i < max; ++i) out[i] = rows[i];
              return n; }
        catch (...) { module_disable(STR("rocks"), STR("scan_rocks_exception")); return -1; }
    }

    // ---- API 16: rock registry (msc_rockreg.hpp). Game thread only. Every call returns -2 when the rockreg module is off (or faulted), -1 on exception. ----
    // each mod calls this every ~0.25 s: owner 1 = standalone mining, 2 = Fleet. ships = n MsRegShip (positions in m), targets = nt rock ids of the ships' targets.
    // Runs the registry tick when due. Returns 1.
    MSCORE_API int MSCore_rockreg_feed(int owner, const MsRegShip* ships, int n, const int* targets, int nt)
    {
        return rr_run([&] { rr_feed(owner, ships, n, targets, nt); return 1; });
    }

    // fills out with the registry state. Returns 1.
    MSCORE_API int MSCore_rockreg_state(MsRegState* out)
    {
        return rr_run([&] {
            if (!out) return 0;
            const RrState& s = rr_st();
            out->world_ok = s.world_ok ? 1 : 0;
            out->world_wait = s.world_wait ? 1 : 0;
            out->seeding = s.seeding ? 1 : 0;
            out->near_done = s.near_done ? 1 : 0;
            out->rocks = static_cast<int64_t>(s.tab.rocks.size());
            out->alive = static_cast<int64_t>(rr_alive_count(s));
            out->version = s.tab.version;
            return 1;
        });
    }

    // rock by save id into out (dead rocks too: check flags bit0). Returns 1 found, 0 unknown.
    MSCORE_API int MSCore_rockreg_get(int id, MsRegRock* out)
    {
        return rr_run([&] {
            const RrRock* r = rr_st().tab.get(id);
            if (!r || !out) return 0;
            rr_fill(*r, *out);
            return 1;
        });
    }

    // alive rocks of system sys inside the box lo..hi (m). out gets min(total, max) rocks (out = nullptr or max = 0 counts only). Returns the total.
    MSCORE_API int MSCore_rockreg_box(int sys, const double lo[3], const double hi[3], MsRegRock* out, int max)
    {
        return rr_run([&] {
            if (!lo || !hi || !rr_ok3(lo) || !rr_ok3(hi) || lo[0] > hi[0] || lo[1] > hi[1] || lo[2] > hi[2]) return 0;
            const RrTable& t = rr_st().tab;
            int total = 0;
            t.for_box(sys, lo, hi, [&](size_t i) {
                const RrRock& r = t.rocks[i];
                if (!r.alive || r.sys != sys) return;
                for (int k = 0; k < 3; ++k) if (r.c[k] < lo[k] || r.c[k] > hi[k]) return;
                if (out && total < max) rr_fill(r, out[total]);
                ++total;
            });
            return total;
        });
    }

    // alive rocks of system sys whose centre lies within margin_m of the segment a->b (m). out gets min(total, max). Returns the total.
    MSCORE_API int MSCore_rockreg_seg(int sys, const double a[3], const double b[3], double margin_m, MsRegRock* out, int max)
    {
        return rr_run([&] {
            double mg = 0.0;
            if (!a || !b || !rr_ok3(a) || !rr_ok3(b) || !rr_len(margin_m, mg)) return 0;
            const RrTable& t = rr_st().tab;
            std::vector<size_t> idx;
            t.within_seg(sys, a, b, mg, idx);
            for (size_t k = 0; out && k < idx.size() && static_cast<int>(k) < max; ++k) rr_fill(t.rocks[idx[k]], out[k]);
            return static_cast<int>(idx.size());
        });
    }

    // up to n alive rocks of system sys nearest to p within max_m (> 0), nearest first. item = Item UObject (only rocks holding that ore name; an item no rock
    // has returns 0 rocks) or NULL (any ore). skip_ids = nskip rock ids to leave out. out gets the rocks, dist_out (may be NULL) their centre distances (m). Returns the count.
    MSCORE_API int MSCore_rockreg_nearest_n(int sys, void* item, const double p[3], const int* skip_ids, int nskip, double max_m, int n, MsRegRock* out, double* dist_out)
    {
        return rr_run([&] {
            double mm = 0.0;
            if (!p || !out || n <= 0 || !rr_ok3(p) || !rr_len(max_m, mm) || !(mm > 0.0)) return 0;
            n = std::min(n, RR_NEAREST_MAX_N);
            nskip = std::min(nskip, RR_SKIP_MAX);
            const RrState& s = rr_st();
            int16_t want = -1;
            if (item)
            {
                const int k = rr_ore_idx(s, static_cast<UObject*>(item));
                if (k < 0) return 0;
                want = static_cast<int16_t>(k);
            }
            static std::set<int> skip;   // game thread only: scratch reused across calls
            static std::vector<std::pair<double, size_t>> rows;
            skip.clear();
            if (skip_ids) for (int i = 0; i < nskip; ++i) skip.insert(skip_ids[i]);
            s.tab.nearest_n(sys, want, p, skip.empty() ? nullptr : &skip, mm, static_cast<size_t>(n), rows);
            for (size_t k = 0; k < rows.size(); ++k)
            {
                rr_fill(s.tab.rocks[rows[k].second], out[k]);
                if (dist_out) dist_out[k] = rows[k].first;
            }
            return static_cast<int>(rows.size());
        });
    }

    // rock of system sys with the smallest surface distance (centre distance - radius, m) to p; an unread radius counts as unknown_rad_m; skip_id > 0 is left out.
    // out = the rock, surf_m (may be NULL) = the surface distance. Returns 1 found, 0 none.
    MSCORE_API int MSCore_rockreg_nearest_surface(int sys, const double p[3], int skip_id, double unknown_rad_m, MsRegRock* out, double* surf_m)
    {
        return rr_run([&] {
            double ur = 0.0;
            if (!p || !out || !rr_ok3(p) || !rr_len(unknown_rad_m, ur)) return 0;
            const RrTable& t = rr_st().tab;
            double sm = 0.0;
            const size_t i = t.nearest_surface(sys, p, ur, sm, skip_id);
            if (i == RrTable::npos) return 0;
            rr_fill(t.rocks[i], *out);
            if (surf_m) *surf_m = sm;
            return 1;
        });
    }

    // 1 when the area around p (m, one chunk each way, system sys) is inside a seeded zone (all its saves read), 0 when not (use the rocks already known).
    MSCORE_API int MSCore_rockreg_area_ready(int sys, const double p[3])
    {
        return rr_run([&] { return (p && rr_ok3(p) && rr_area_ready(sys, p)) ? 1 : 0; });
    }

    // writes a report file to <MSCore mod folder>\rockreg and the one-line text into out. kind 0 dump around p (RockRegDump.txt), 1 query (arg = radius m,
    // RockRegQuery.txt), 2 check (arg = n rocks, RockRegCheck.txt: registry vs loaded actor positions). Returns 1 (the text may start with "error").
    MSCORE_API int MSCore_rockreg_report(int kind, int sys, const double p[3], double arg, wchar_t* out, int len)
    {
        return rr_run([&] {
            const double zero[3] = {0, 0, 0};
            copy_err(rr_report(kind, sys, p ? p : zero, arg), out, len);
            return 1;
        });
    }


    // chunk hold for owner: returns 0 fail (err says why), 1 set, 2 unchanged. ch3 = chunk X, Y, Z (raw int64).
    MSCORE_API int MSCore_chunk_hold(int owner, int sys, const long long* ch3, wchar_t* err, int errlen)
    {
        std::wstring e;
        int r = 0;
        try
        {
            if (!ch3) e = STR("null chunk");
            else r = chunk_hold(owner, sys, ch3, e);
        }
        catch (...) { r = 0; e = STR("exception"); module_disable(STR("chunkhold"), STR("chunk_hold exception")); }
        copy_err(e, err, errlen);
        return r;
    }

    // chunk hold with radius: radius 1 = 3x3x3 chunks (as MSCore_chunk_hold), 0 = centre chunk only. Same returns and err as MSCore_chunk_hold.
    MSCORE_API int MSCore_chunk_hold2(int owner, int sys, const long long* ch3, int radius, wchar_t* err, int errlen)
    {
        std::wstring e;
        int r = 0;
        try
        {
            if (!ch3) e = STR("null chunk");
            else if (radius != 0 && radius != 1) e = STR("radius must be 0 or 1");
            else r = chunk_hold(owner, sys, ch3, e, radius);
        }
        catch (...) { r = 0; e = STR("exception"); module_disable(STR("chunkhold"), STR("chunk_hold2 exception")); }
        copy_err(e, err, errlen);
        return r;
    }

    // drops the owner's chunk hold. 1 = done (also when there was none), 0 = a native call failed or exception (feature off).
    MSCORE_API int MSCore_chunk_release(int owner)
    {
        try { return chunk_hold_release_owner(owner) ? 1 : 0; }
        catch (...) { module_disable(STR("chunkhold"), STR("chunk_release exception")); return 0; }
    }

    // 1 when the chunk-hold natives are found (why gets the reason otherwise). Read-only.
    MSCORE_API int MSCore_chunk_ready(wchar_t* why, int len)
    {
        std::wstring w;
        int ok = 0;
        try { ok = chunk_ready(w) ? 1 : 0; }
        catch (...) { ok = 0; w = STR("exception"); module_disable(STR("chunkhold"), STR("chunk_ready exception")); }
        copy_err(w, why, len);
        return ok;
    }

    // ---- API 4: factory commands (msc_factory.hpp). Type ids: BuildInput 0x2e, DisassembleInput 0x2f, InventoryInput 12, SetRecipeInput 20, MachinePortInput 17. ----

    // sends cmd (live input command UObject) to receiver with type id type_id (written at +0x28 when that int is 0). 1 ok, 0 fail.
    MSCORE_API int MSCore_send_typed(int receiver, void* cmd, int type_id, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            return factory_guard([&] { return send_typed(receiver, static_cast<UObject*>(cmd), type_id, e); }, e);
        }, err, errlen);
    }

    // Buildable data asset by name (e.g. DAT_Refinery). NULL when none. No error text: check the name.
    MSCORE_API void* MSCore_find_buildable(const char* dat_name)
    {
        if (!dat_name) return nullptr;
        UObject* o = nullptr;
        try
        {
            if (!seh_run([&] { o = find_buildable(dat_name); }))
            {
                msc_log(STR("[MSCore] MSCore_find_buildable: native fault looking up ") + mscl::to_wide(dat_name) + STR(" (NULL returned)"));
                return nullptr;
            }
        }
        catch (...) { module_disable(STR("misc"), STR("find_buildable exception")); return nullptr; }
        return o;
    }

    // BuildInput at receiver 0. bdat = MSCore_find_buildable result. cfg = SerializedMachineConfig bytes (cfg may be NULL when cfglen = 0). cfglen > 64 KiB is refused.
    MSCORE_API int MSCore_build_place(int station, void* buildable, int x, int y, int height, int rot,
                                      const uint8_t* cfg, int cfglen, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            if (cfglen > kMaxCfgBytes) { e = STR("cfg larger than 64 KiB"); return false; }
            return factory_guard([&] { return build_place(station, static_cast<UObject*>(buildable), x, y, height, rot, bytes_of(cfg, cfglen), e); }, e);
        }, err, errlen);
    }

    // DisassembleInput at receiver 0. mass != 0 = bMassDisassemble.
    MSCORE_API int MSCore_disassemble(int target_id, int mass, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return factory_guard([&] { return disassemble(target_id, mass, e); }, e); }, err, errlen);
    }

    // SetRecipeInput at receiver = machine. cfg = 0 or 4 bytes (int32 Config, little endian). recipe = Recipe asset UObject.
    MSCORE_API int MSCore_set_recipe(int machine, void* recipe, int amount, const uint8_t* cfg, int cfglen, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            if (cfglen > kMaxCfgBytes) { e = STR("cfg larger than 64 KiB"); return false; }
            return factory_guard([&] { return set_recipe(machine, static_cast<UObject*>(recipe), amount, bytes_of(cfg, cfglen), e); }, e);
        }, err, errlen);
    }

    // MachinePortInput at receiver = machine: toggles port index port.
    MSCORE_API int MSCore_port_toggle(int machine, int port, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) { return factory_guard([&] { return port_toggle(machine, port, e); }, e); }, err, errlen);
    }

    // InventoryInput at receiver 0 (the game DROPS receiver 0: use MSCore_inv_move_to). action 2 = quick move.
    MSCORE_API int MSCore_inv_move(int primary_inv_id, int secondary_inv_id, int slot, int action, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            return factory_guard([&] { return inv_move(primary_inv_id, secondary_inv_id, slot, action, e); }, e);
        }, err, errlen);
    }

    // 0.7.6: local move of n items between two slots of one Inventory (no game command). Slots 0-based. Not a game command: co-op desync risk.
    MSCORE_API int MSCore_inv_split(int inv_id, int src_slot, int amount, int dst_slot, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            return factory_guard([&] { return inv_split(inv_id, src_slot, amount, dst_slot, e); }, e);
        }, err, errlen, STR("split"));
    }

    // InventoryInput at receiver = the player object id (the game applies it there). action 2 = quick move.
    MSCORE_API int MSCore_inv_move_to(int primary_inv_id, int secondary_inv_id, int slot, int action, int receiver, wchar_t* err, int errlen)
    {
        return export_call([&](std::wstring& e) {
            return factory_guard([&] { return inv_move(primary_inv_id, secondary_inv_id, slot, action, e, receiver); }, e);
        }, err, errlen);
    }
}
