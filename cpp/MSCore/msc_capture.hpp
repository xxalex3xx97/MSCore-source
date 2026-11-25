// FILE: cpp/MSCore/msc_capture.hpp
// WHAT: input capture PROBE (OFF unless Mods\MSCore\strafe_probe_on.txt exists, g_cap_on): logs the ShipMovementInputCommand / ViewInput the game sends
//       while the player pilots, on content change, plus a once-a-second command count. Read-only, SEH, fail closed. Our own sends show too.
// MECHANISM: (a) pre-hook on DMSPlayerController.AddInputCommand (same route as msc_pin.hpp); reads the command fields raw by property name.
//            (b) fallback while the hook is not armed: poll the player ship cached strafe/rotation fields (ship+0x510/+0x518 / +0x520/+0x528, int64) and throttle.
//            capture_tick() (Lua native, ~250 ms) arms the hook, runs (b) when not armed, and prints the rate line.
// LOG (via msc_log, "[MSCore] " prefix is added by msc_log):
//   PROBE input_capture ship=<recv> DeltaThrottle=<i> RotationImpulse=<x,y,z> StrafeImpulse=<x,y,z> t_ms=<ms since last change of that receiver>
//   PROBE input_capture view ship=<recv> ViewRotation=<x,y,z> t_ms=<n>
//   PROBE input_capture poll ship=<sid> strafe_cache=<a>,<b> rot_cache=<c>,<d> throttle=<t>
//   PROBE input_capture rate cmds_per_s=<n> (all commands through AddInputCommand; printed when a command or the tick comes after >= 1 s)
// UNVERIFIED: AddInputCommand is the route of the player's own input (pin proves it for map pins); ViewInput command holds an InputView-like struct with ViewRotation;
//             cache offsets in (b) and their int64 width. The marker is read at start only (no re-read on selfcheck).
// USED BY: dllmain.cpp (g_cap_on marker, MSCoreNative_capture_tick, check_one "capture"). Test: tests/test_capture_logic.cpp.
#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include "msc_capture_logic.hpp"

namespace
{
    std::atomic<bool> g_cap_on{false};     // marker strafe_probe_on.txt (default OFF)
    bool g_cap_hooked = false;
    bool g_cap_dead = false;               // a fault turned the probe off for the session
    int g_cap_tag = 0;
    size_t g_cap_off_cmd = 0, g_cap_off_recv = 0;
    UClass* g_cap_move_cls = nullptr;
    UClass* g_cap_view_cls = nullptr;
    std::mutex g_cap_mx;
    std::map<int, CapState> g_cap_state;   // key: receiver * 4 + kind (0 move, 1 view, 2 poll)
    CapRate g_cap_rate;

    bool cap_read_vec(uint8_t* base, UStruct* st, const TCHAR* name, long long out[3])
    {
        for (int a = 0; a < 3; ++a)
        {
            bool is64 = false;
            uint8_t* p = raw_vec_axis_ptr(base, st, name, a, is64);
            if (!p) return false;
            if (is64) { int64_t v = 0; if (!ms_rd_seh(p, &v)) return false; out[a] = v; }
            else { int32_t v = 0; if (!ms_rd_seh(p, &v)) return false; out[a] = v; }
        }
        return true;
    }

    // ViewRotation sits in a struct property of the ViewInput command: find it by name once
    bool cap_read_view(UObject* cmd, long long out[3])
    {
        static bool tried = false;
        static int off = -1;
        static UStruct* inner = nullptr;
        if (!tried)
        {
            tried = true;
            std::wstring names;
            for (FProperty* p : cmd->GetClassPrivate()->ForEachProperty())
            {
                names += p->GetName() + STR(" ");
                auto* sp = CastField<FStructProperty>(p);
                if (sp && sp->GetStruct() && sp->GetStruct()->FindProperty(FName(STR("ViewRotation"), FNAME_Add))) { off = p->GetOffset_Internal(); inner = sp->GetStruct(); }
            }
            msc_log(STR("PROBE input_capture view layout: ") + (inner ? STR("ViewRotation struct at +") + std::to_wstring(off) : STR("ViewRotation NOT found, props: ") + names));
        }
        if (!inner) return false;
        return cap_read_vec(reinterpret_cast<uint8_t*>(cmd) + off, inner, STR("ViewRotation"), out);
    }

    void cap_rate_report(ULONGLONG now)
    {
        int n = 0;
        std::lock_guard<std::mutex> lk(g_cap_mx);
        if (cap_rate_tick(g_cap_rate, now, n)) msc_log(STR("PROBE input_capture rate cmds_per_s=") + std::to_wstring(n));
    }

    // WHAT: reads one command from the hook; logs it when its content changed.
    void cap_on_command(UObject* cmd, int recv)
    {
        const ULONGLONG now = GetTickCount64();
        { std::lock_guard<std::mutex> lk(g_cap_mx); cap_rate_add(g_cap_rate); }
        UClass* c = cmd->GetClassPrivate();
        const bool mv = c == g_cap_move_cls, vw = c == g_cap_view_cls;
        if (!mv && !vw) { cap_rate_report(now); return; }
        long long v[8] = {};
        std::wstring line;
        if (mv)
        {
            long long rot[3] = {}, str[3] = {};
            int32_t dt = 0;
            FProperty* dp = c->FindProperty(FName(STR("DeltaThrottle"), FNAME_Add));
            uint8_t* base = reinterpret_cast<uint8_t*>(cmd);
            if (!dp || !ms_rd_seh(base + dp->GetOffset_Internal(), &dt)) return;
            if (!cap_read_vec(base, c, STR("RotationImpulse"), rot) || !cap_read_vec(base, c, STR("StrafeImpulse"), str)) return;
            v[0] = dt; for (int i = 0; i < 3; ++i) { v[1 + i] = rot[i]; v[4 + i] = str[i]; }
            line = STR(" ship=") + std::to_wstring(recv) + STR(" DeltaThrottle=") + std::to_wstring(dt) + STR(" RotationImpulse=") +
                   std::to_wstring(rot[0]) + STR(",") + std::to_wstring(rot[1]) + STR(",") + std::to_wstring(rot[2]) + STR(" StrafeImpulse=") +
                   std::to_wstring(str[0]) + STR(",") + std::to_wstring(str[1]) + STR(",") + std::to_wstring(str[2]);
        }
        else
        {
            long long vr[3] = {};
            if (!cap_read_view(cmd, vr)) return;
            for (int i = 0; i < 3; ++i) v[i] = vr[i];
            line = STR(" view ship=") + std::to_wstring(recv) + STR(" ViewRotation=") + std::to_wstring(vr[0]) + STR(",") + std::to_wstring(vr[1]) + STR(",") + std::to_wstring(vr[2]);
        }
        long long dtm = 0;
        bool ch;
        { std::lock_guard<std::mutex> lk(g_cap_mx); ch = cap_changed(g_cap_state[recv * 4 + (vw ? 1 : 0)], v, mv ? 7 : 3, now, dtm); }
        if (ch) msc_log(STR("PROBE input_capture") + line + STR(" t_ms=") + std::to_wstring(dtm));
        cap_rate_report(now);
    }

    void cap_hook_pre(RC::Unreal::UnrealScriptFunctionCallableContext& ctx, void*)
    {
        if (g_cap_dead || !g_cap_on.load()) return;
        try
        {
            uint8_t* base = ctx.TheStack.Locals();
            if (!base) return;
            UObject* cmd = *reinterpret_cast<UObject* const*>(base + g_cap_off_cmd);
            int32_t recv = 0;
            if (!alive(cmd) || !ms_rd_seh(base + g_cap_off_recv, &recv)) return;
            if (!seh_run([&] { cap_on_command(cmd, recv); })) { g_cap_dead = true; msc_log(STR("PROBE input_capture fault, probe off")); }
        }
        catch (...) { g_cap_dead = true; }
    }
    void cap_hook_post(RC::Unreal::UnrealScriptFunctionCallableContext&, void*) {}

    // WHAT: registers cap_hook_pre on DMSPlayerController.AddInputCommand once; true when hooked.
    bool cap_hook_ensure()
    {
        if (g_cap_hooked) return true;
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        UFunction* fn = alive(ctrl) ? ctrl->GetFunctionByNameInChain(STR("AddInputCommand")) : nullptr;
        if (!fn) return false;
        FProperty* pc = fn->FindProperty(FName(STR("InCommand"), FNAME_Add));
        FProperty* pr = fn->FindProperty(FName(STR("InReceiverID"), FNAME_Add));
        UClass* mc = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ShipMovementInputCommand"));
        UClass* vc = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ViewInput"));
        if (!pc || !pr || !mc) return false;
        std::wstring path = fn->GetFullName();
        { size_t sp = path.find(L' '); if (sp != std::wstring::npos) path = path.substr(sp + 1); }
        g_cap_off_cmd = static_cast<size_t>(pc->GetOffset_Internal());
        g_cap_off_recv = static_cast<size_t>(pr->GetOffset_Internal());
        g_cap_move_cls = mc; g_cap_view_cls = vc;
        try { UObjectGlobals::RegisterHook(path, cap_hook_pre, cap_hook_post, &g_cap_tag); }
        catch (...) { msc_log(STR("PROBE input_capture hook failed on ") + path); return false; }
        g_cap_hooked = true;
        msc_log(STR("PROBE input_capture hook armed on ") + path);
        return true;
    }

    // WHAT: fallback (b): poll the player ship strafe/rotation caches and throttle; log on change.
    void cap_poll()
    {
        UObject* ship = first_of(STR("DMSPlayerShipObject"));
        if (!alive(ship)) return;
        const int sid = obj_id(ship);
        long long v[5] = {};
        int64_t t[4] = {};
        const size_t offs[4] = {0x510, 0x518, 0x520, 0x528};
        for (int i = 0; i < 4; ++i) if (!ms_rd(ship, offs[i], t[i])) return;
        MsFlight f{};
        if (ship_flight_read(sid, FL_STATE, f) < 0) return;
        for (int i = 0; i < 4; ++i) v[i] = t[i];
        v[4] = static_cast<long long>(f.throttle);
        long long dtm = 0;
        bool ch;
        { std::lock_guard<std::mutex> lk(g_cap_mx); ch = cap_changed(g_cap_state[sid * 4 + 2], v, 5, GetTickCount64(), dtm); }
        if (ch) msc_log(STR("PROBE input_capture poll ship=") + std::to_wstring(sid) + STR(" strafe_cache=") + std::to_wstring(v[0]) + STR(",") + std::to_wstring(v[1]) +
                        STR(" rot_cache=") + std::to_wstring(v[2]) + STR(",") + std::to_wstring(v[3]) + STR(" throttle=") + std::to_wstring(v[4]));
    }

    // WHAT: Lua tick (~250 ms). Off without the marker: does nothing, registers nothing.
    void capture_tick()
    {
        if (!g_cap_on.load() || g_cap_dead) return;
        if (!seh_run([&] { if (!cap_hook_ensure()) cap_poll(); else cap_rate_report(GetTickCount64()); }))
        { g_cap_dead = true; msc_log(STR("PROBE input_capture tick fault, probe off")); }
    }
}
