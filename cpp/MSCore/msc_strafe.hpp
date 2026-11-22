// FILE: cpp/MSCore/msc_strafe.hpp
// WHAT: strafe module (default ON): ship_strafe (side/vertical movement) via ShipMovementInputCommand.StrafeImpulse, ship_strafe_zero, watchdog.
// WHY: mods ask "strafe right/up with value" by logical name; the game names (ShipMovementInputCommand, StrafeImpulse) live only here (msc_cmd.hpp).
// ACT: only AddInputCommand (send_ship_move, co-op safe). No ship property is written.
// HELD: the game keeps strafe until a command with another value arrives, so state is per ship (g_strafe, msc_cmd.hpp) and EVERY movement send
//       carries it (send_ship_throttle too). A strafe not refreshed for kStrafeHoldMs is zeroed by strafe_watchdog (export MSCore_strafe_tick
//       + lazily on each ship_strafe call): there is no DLL tick, so the caller must call MSCore_strafe_tick about once a second while strafing.
// SAFETY: off when Mods\MSCore\strafe_off.txt exists (g_strafe_off); module 'strafe' (kill switch module_disable("strafe")). Fail closed.
// USED BY: dllmain.cpp (strafe_resolve, check_one "strafe", MSCore_ship_strafe, MSCore_ship_strafe_zero, MSCore_strafe_tick). Test: tests/test_strafe_logic.cpp.
// LIVE-VERIFIED (user test): StrafeImpulse Y (+0x54) = right(+)/left(-), X (+0x50) = up(+)/down(-). Logical axis 0 = right, 1 = up (str_slot maps them). Observed: speed after 1 s only ~1.1-2.6 m/s, so the impulse acts like an acceleration with sign-only input.
// UNVERIFIED: magnitude ignored (only sign used).
#pragma once
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void strafe_resolve() { static bool done = false; if (done) return; done = true; caps_add("strafe", true, ""); }
    bool strafe_enabled() { return !g_strafe_off.load() && module_ok(STR("strafe")); }

    // WHAT: read-only layout of ShipMovementInputCommand: offsets of DeltaThrottle, RotationImpulse, StrafeImpulse (logged once).
    // RETURNS: true when DeltaThrottle and StrafeImpulse resolve with X and Y int axes.
    bool strafe_selftest(std::string& detail)
    {
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ShipMovementInputCommand"));
        if (!c) { detail = "ShipMovementInputCommand class not found"; return false; }
        auto off = [&](const TCHAR* n) { FProperty* p = c->FindProperty(FName(n, FNAME_Add)); return p ? static_cast<int>(p->GetOffset_Internal()) : -1; };
        const int d = off(STR("DeltaThrottle")), r = off(STR("RotationImpulse")), s = off(STR("StrafeImpulse"));
        bool x64 = false, y64 = false;
        uint8_t dummy[0x100] = {};
        uint8_t* px = raw_vec_axis_ptr(dummy, c, STR("StrafeImpulse"), 0, x64);
        uint8_t* py = raw_vec_axis_ptr(dummy, c, STR("StrafeImpulse"), 1, y64);
        const int xo = px ? static_cast<int>(px - dummy) : -1, yo = py ? static_cast<int>(py - dummy) : -1;
        long long raw[3];
        str_slots(1, 1, raw);
        detail = "writes +-" + std::to_string(kStrafeRaw) + " (right slot " + std::to_string(kStrafeSlotRight) + ", up slot " + std::to_string(kStrafeSlotUp) + ") DeltaThrottle=" + std::to_string(d) + " RotationImpulse=" + std::to_string(r) + " StrafeImpulse=" + std::to_string(s) +
                 " X(up)@" + std::to_string(xo) + (x64 ? "(i64)" : "(i32)") + " Y(right)@" + std::to_string(yo) + (y64 ? "(i64)" : "(i32)");
        static bool logged = false;
        if (!logged) { logged = true; msc_log(STR("strafe layout ") + std::wstring(detail.begin(), detail.end())); }
        return d >= 0 && s >= 0 && xo >= 0 && yo >= 0;
    }

    // sends the state to the game; a fault in the send turns the module off
    bool strafe_send(int sid, int sright, int sup, std::wstring& err, bool log = true)
    {
        if (log) msc_log(STR("strafe ship=") + std::to_wstring(sid) + STR(" right=") + std::to_wstring(sright) + STR(" up=") + std::to_wstring(sup));
        bool ok = false;
        if (!seh_run([&] { ok = send_ship_move(sid, 0, sright, sup, err); }))
        {
            module_disable(STR("strafe"), STR("send fault"));
            err = STR("strafe send fault");
            return false;
        }
        return ok;
    }

    unsigned long g_strafe_resends = 0;   // keep-alive resends (counter only, not logged per resend)

    // 3 fixed-point axes of vec property `name` of struct st inside base, raw (int32 or int64); false when not found or a read faults
    bool strafe_vec_raw(uint8_t* base, UStruct* st, const TCHAR* name, long long out[3])
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

    // reads vec prop `name` (3 axes) of obj, searching the class chain; false when not found or a read faults
    bool strafe_read_vec(UObject* o, const TCHAR* name, long long out[3])
    {
        uint8_t* base = reinterpret_cast<uint8_t*>(o);
        for (UStruct* s = o->GetClassPrivate(); s; s = s->GetSuperStruct())
        {
            if (!s->FindProperty(FName(name, FNAME_Add))) continue;
            return strafe_vec_raw(base, s, name, out);
        }
        return false;
    }

    // WHAT: PROBE line with the ship's ImpulseInput / ImpulseMomentum / WorldImpulse (PNum raw / 65536, "?" when absent). Read-only, SEH.
    void strafe_readback(int sid, const char* ev)
    {
        UObject* ship = flight_ship(sid);
        if (!ship) return;
        std::string a = "?", b = "?", w = "?";
        seh_run([&] {
            long long r[3];
            a = str_fmt3(strafe_read_vec(ship, STR("ImpulseInput"), r), r);
            b = str_fmt3(strafe_read_vec(ship, STR("ImpulseMomentum"), r), r);
            w = str_fmt3(strafe_read_vec(ship, STR("WorldImpulse"), r), r);
        });
        msc_log(STR("PROBE strafe_readback ship=") + std::to_wstring(sid) + STR(" ImpulseInput=") + std::wstring(a.begin(), a.end()) +
                STR(" ImpulseMomentum=") + std::wstring(b.begin(), b.end()) + STR(" WorldImpulse=") + std::wstring(w.begin(), w.end()) +
                STR(" ev=") + std::wstring(ev, ev + std::strlen(ev)) + STR(" resends=") + std::to_wstring(g_strafe_resends));
    }

    // WHAT: zeroes (and sends zero for) every ship whose strafe is older than kStrafeHoldMs; drops entries of ships that no longer exist.
    // RETURNS: how many ships were zeroed.
    int strafe_watchdog()
    {
        if (!strafe_enabled()) return 0;
        const ULONGLONG now = GetTickCount64();
        std::vector<int> ids;
        {
            std::lock_guard<std::mutex> lk(g_strafe_mx);
            for (auto it = g_strafe.begin(); it != g_strafe.end();)
            {
                const int sid = it->first;
                if (!flight_ship(sid)) { it = g_strafe.erase(it); continue; }   // gone: nothing to send to
                if (str_expired(it->second, now)) { ids.push_back(sid); it = g_strafe.erase(it); continue; }
                ++it;
            }
        }
        for (int sid : ids) { std::wstring e; strafe_send(sid, 0, 0, e); strafe_readback(sid, "stop"); }
        // keep-alive: the game re-sends its input every tick, so resend a held strafe every kStrafeKeepMs (watchdog above still zeroes after 10 s;
        // a resend does not refresh it). Readback probe once per second per held ship.
        std::vector<std::pair<int, StrafeSt>> keep;
        std::vector<int> rb;
        {
            std::lock_guard<std::mutex> lk(g_strafe_mx);
            for (auto& kv : g_strafe)
            {
                if (str_needs_resend(kv.second, now)) { kv.second.sent = now; keep.push_back(kv); }
                if (now - kv.second.rb >= kStrafeReadbackMs) { kv.second.rb = now; rb.push_back(kv.first); }
            }
        }
        static bool kept_logged = false;
        if (!keep.empty() && !kept_logged) { kept_logged = true; msc_log(STR("strafe keep-alive active (resend every ") + std::to_wstring(kStrafeKeepMs) + STR(" ms while held)")); }
        for (auto& kv : keep) { std::wstring e; strafe_send(kv.first, kv.second.s[0], kv.second.s[1], e, false); ++g_strafe_resends; }
        for (int sid : rb) strafe_readback(sid, "tick");
        return static_cast<int>(ids.size());
    }

    // WHAT: set strafe axis (0 right, 1 up) of ship sid from value -1..1 (|v| > 0.3 = push, sign = direction). Sends unless nothing changed and it is zero.
    bool ship_strafe(int sid, int axis, double value, std::wstring& err)
    {
        if (!strafe_enabled()) { err = STR("strafe off"); return false; }
        if (axis < 0 || axis > 1) { err = STR("axis must be 0 or 1"); return false; }
        strafe_watchdog();
        if (!flight_ship(sid))
        {
            std::lock_guard<std::mutex> lk(g_strafe_mx);
            g_strafe.erase(sid);
            err = STR("ship not found");
            return false;
        }
        StrafeSt cur, prev;
        bool send;
        {
            std::lock_guard<std::mutex> lk(g_strafe_mx);
            StrafeSt& st = g_strafe[sid];
            prev = st;
            send = str_update(st, axis, str_sign(value), GetTickCount64());
            cur = st;
            if (!cur.nz()) g_strafe.erase(sid);
        }
        if (!send) return true;
        { std::lock_guard<std::mutex> lk(g_strafe_mx); auto it = g_strafe.find(sid); if (it != g_strafe.end()) { it->second.sent = GetTickCount64(); if (!prev.nz()) it->second.rb = it->second.sent; } }
        const bool ok = strafe_send(sid, cur.s[0], cur.s[1], err, cur.s[0] != prev.s[0] || cur.s[1] != prev.s[1]);   // log on change only
        if (!prev.nz() && cur.nz())   // start: show the raw slots of the command and the ship's readback
        {
            long long raw[3];
            str_slots(cur.s[0], cur.s[1], raw);
            msc_log(STR("PROBE strafe_cmd ship=") + std::to_wstring(sid) + STR(" slots=") + [&] { const std::string t = str_fmt_slots(raw); return std::wstring(t.begin(), t.end()); }());
            strafe_readback(sid, "start");
        }
        else if (prev.nz() && !cur.nz()) strafe_readback(sid, "stop");
        return ok;
    }

    // WHAT: both axes 0 for ship sid; one zero command only if either was non-zero.
    void ship_strafe_zero(int sid)
    {
        if (g_strafe_off.load() || !module_ok(STR("strafe"))) return;
        bool send;
        {
            std::lock_guard<std::mutex> lk(g_strafe_mx);
            auto it = g_strafe.find(sid);
            send = it != g_strafe.end() && str_zero(it->second);
            if (it != g_strafe.end()) g_strafe.erase(it);
        }
        std::wstring e;
        if (send) { strafe_send(sid, 0, 0, e); strafe_readback(sid, "stop"); }
    }
}
