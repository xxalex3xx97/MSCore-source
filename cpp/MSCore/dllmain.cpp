// MSCore native part. Exposes the Lua globals MSCoreNative_* (Lua mod MSCore) and the C exports used by MiningBenchMod (api 2).
#define NOMINMAX  // Windows.h min/max macros break UE4SS headers
#include <Windows.h>
#include <cmath>
#include <atomic>
#include <mutex>
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

using namespace RC;
using namespace RC::Unreal;

#include "msc_api.hpp"
#include "msc_cmd.hpp"
#include "msc_ue.hpp"
#include "msc_orders.hpp"
#include "msc_warp.hpp"
#include "msc_pin.hpp"
#include "msc_loader.hpp"

namespace
{
    constexpr const char* kVersion = "0.5.1";   // keep equal to MSCore.version in mscore.lua and mscore.mod

    std::string narrow(const std::wstring& s) { std::string out; for (wchar_t c : s) out += static_cast<char>(c); return out; }   // error texts are ASCII

    // copies err text into the caller buffer, always NUL-terminated
    void copy_err(const std::wstring& s, wchar_t* out, int outlen)
    {
        if (out && outlen > 0) wcsncpy_s(out, static_cast<size_t>(outlen), s.c_str(), _TRUNCATE);
    }

    // runs fn(err) for an export: exceptions become a failure, err goes to the caller buffer, returns 1 on success
    template <typename F>
    int export_call(F fn, wchar_t* err, int errlen)
    {
        std::wstring e;
        bool ok = false;
        try { ok = fn(e); }
        catch (...) { ok = false; e = STR("exception"); }
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

    // UObject of Lua arg i; nullptr for nil or a non-userdata
    UObject* lua_obj(const LuaMadeSimple::Lua& lua, int i)
    {
        return lua.is_userdata(i) ? lua.get_userdata<LuaType::UObject>(i).get_remote_cpp_object() : nullptr;
    }

    int arg_int(const LuaMadeSimple::Lua& lua, int i) { return static_cast<int>(lua.get_integer(i)); }

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
                UObject* cmd = lua.get_userdata<LuaType::UObject>(2).get_remote_cpp_object();
                if (!alive(cmd)) err = STR("cmd is not alive");
                else
                {
                    fix_type_id(cmd);
                    ok = add_input(static_cast<int>(lua.get_integer(1)), cmd, err);
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
                ok = view_send(static_cast<int>(lua.get_integer(1)),
                               static_cast<int64_t>(lua.get_float(2) * kViewRawPerDeg),
                               static_cast<int64_t>(lua.get_float(3) * kViewRawPerDeg), err);
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
            else ok = send_ship_throttle(arg_int(lua, 1), arg_int(lua, 2), err);
        }
        catch (...) { ok = false; err = STR("exception"); }
        return lua_result(lua, ok, err);
    }

    // WHY: text form of the msc_cmd.hpp offsets; selfcheck native.offsets looks for 0x28 in it.
    int lua_offsets(const LuaMadeSimple::Lua& lua)
    {
        lua.set_string("typeid_off=0x28 throttle_id=0x3a view_raw_per_deg=65536");
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
            else ok = new_order(arg_int(lua, 1), widen(std::string(lua.get_string(2))), err);
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
            else ok = update_order(arg_int(lua, 1), arg_int(lua, 2), lua_obj(lua, 3), arg_int(lua, 4), arg_int(lua, 5), err);
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
            else ok = delete_order(arg_int(lua, 1), arg_int(lua, 2), err);
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
            else ok = set_autopilot(arg_int(lua, 1), arg_int(lua, 2) != 0, err);
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
            else ok = skip_to(arg_int(lua, 1), arg_int(lua, 2), before, after, err);
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
            else ok = filter_send(arg_int(lua, 1), arg_int(lua, 2), lua_obj(lua, 3), arg_int(lua, 4), arg_int(lua, 5), arg_int(lua, 6) != 0, err);
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
                const int64_t ch[3] = {lua.get_integer(3), lua.get_integer(4), lua.get_integer(5)};
                const int64_t arr[3] = {lua.get_integer(6), lua.get_integer(7), lua.get_integer(8)};
                ok = warp_send(arg_int(lua, 1), arg_int(lua, 2), ch, arr, lua.get_integer(9) != 0, err, lua.get_integer(10) != 0);
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

class MSCoreMod : public RC::CppUserModBase
{
public:
    MSCoreMod() : CppUserModBase()
    {
        ModName = STR("MSCore");
        ModVersion = STR("0.5.1");
        ModDescription = STR("MSCore native helpers for Main Sequence mods");
        ModAuthors = STR("local");
        Output::send<LogLevel::Normal>(STR("[MSCore] native DLL loaded\n"));
        loader_start();
    }

    ~MSCoreMod()
    {
        g_loader_stop = true;   // detached worker checks this between network steps
    }

    auto on_lua_start(LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua*) -> void override
    {
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
        lua.register_function("MSCoreNative_check", lua_check);
        lua.register_function("MSCoreNative_loader_status", lua_loader_status);
        Output::send<LogLevel::Normal>(STR("[MSCore] Lua globals MSCoreNative_* registered\n"));
    }
};

#define MSCORE_API __declspec(dllexport)
extern "C"
{
    MSCORE_API RC::CppUserModBase* start_mod() { return new MSCoreMod(); }
    MSCORE_API void uninstall_mod(RC::CppUserModBase* mod) { delete mod; }

    // C API for other native mods (MiningBenchMod). Each call returns 1 on success, 0 on failure; err gets the reason (errlen wide chars).
    // MSCore_api: 2 = the set below (mining refuses to load below 2).
    MSCORE_API int MSCore_api() { return 2; }

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
        catch (...) { o = nullptr; w = STR("exception"); }
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
        catch (...) { id = -1; w = STR("exception"); }
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
        catch (...) { return -1; }
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
        catch (...) { return 0; }
    }

    // 1 + out3 = struct property prop (X, Y, Z) of obj, raw; 0 when missing
    MSCORE_API int MSCore_axes_of(void* obj, const wchar_t* prop, long long* out3)
    {
        UObject* o = static_cast<UObject*>(obj);
        if (!alive(o) || !prop || !out3) return 0;
        try { return sec_axes(o, prop, out3) ? 1 : 0; }
        catch (...) { return 0; }
    }

    MSCORE_API int MSCore_sys_id(void* obj)
    {
        UObject* o = static_cast<UObject*>(obj);
        try { return alive(o) ? sys_id(o) : 0; }
        catch (...) { return 0; }
    }

    // 1 + out = GenParams.WarpArrivalDistance of a sector (raw); 0 when missing
    MSCORE_API int MSCore_sec_warp_dist(void* sec, long long* out)
    {
        if (!out) return 0;
        try { return sec_warp_dist(static_cast<UObject*>(sec), *out) ? 1 : 0; }
        catch (...) { return 0; }
    }

    // sectors into out[0..max); returns the total found (-1 on exception). out = nullptr or max = 0: count only.
    MSCORE_API int MSCore_sectors(MsSector* out, int max)
    {
        try { return sectors_fill(out, max); }
        catch (...) { return -1; }
    }

    // 1 when the map-pin hook is armed (call every tick until it returns 1)
    MSCORE_API int MSCore_pin_ensure()
    {
        try { return pin_hook_ensure() ? 1 : 0; }
        catch (...) { return 0; }
    }

    // last map-pinned sector id, 0 = none
    MSCORE_API int MSCore_pinned() { return g_pinned_sector; }
}
