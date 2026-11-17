// FILE: cpp/MSCore/msc_caps.hpp
// WHAT: one list of the feature capabilities (name, ok, why) that the resolvers register when they decide.
// WHY: one startup line ("COMPAT CAPS ...") says which features are on, so a game patch shows up in the log at once.
// LOG: caps_text() is appended to the lua_offsets text and logged at startup by dllmain.cpp.
// MODULES: a module is a cap row. module_disable = runtime kill switch (one log line per name), module_ok = query, caps_dep = dependency off.
// USED BY: msc_natives.hpp (chunkhold), msc_refund.hpp (refund, refund_groups, refund_lookup, refund_offsets), msc_station.hpp (split), dllmain.cpp.
#pragma once

#include <set>
#include <string>
#include <vector>
#include <mutex>

namespace
{
    struct CapRow { std::string name; bool ok; std::string why; };
    std::vector<CapRow> g_caps;
    std::set<std::string> g_disable_logged;   // names whose runtime disable line is already logged
    std::mutex g_caps_mx;   // caps_add runs on the game thread, caps_text on the Lua thread
    void caps_add(const std::string& name, bool ok, const std::string& why) { std::lock_guard<std::mutex> lk(g_caps_mx); g_caps.push_back({ name, ok, why }); }
    // "name=ok name=off:why ..." (each entry followed by a space)
    std::string caps_text() { std::lock_guard<std::mutex> lk(g_caps_mx); std::string s; for (auto& c : g_caps) s += c.name + (c.ok ? "=ok" : "=off:" + c.why) + " "; return s; }

    std::string caps_nar(const std::wstring& s) { return std::string(s.begin(), s.end()); }   // names and reasons are ASCII
    // caller holds g_caps_mx. Sets the row name (added when missing).
    void caps_set_locked(const std::string& name, bool ok, const std::string& why)
    {
        for (auto& c : g_caps) if (c.name == name) { c.ok = ok; c.why = why; return; }
        g_caps.push_back({ name, ok, why });
    }
    // WHAT: runtime kill switch: cap name off with why. Logs ONE line per name. WHY: export catch blocks call it, the module stops for this session.
    void module_disable(const std::wstring& name, const std::wstring& why)
    {
        bool first = false;
        {
            std::lock_guard<std::mutex> lk(g_caps_mx);
            caps_set_locked(caps_nar(name), false, caps_nar(why));
            first = g_disable_logged.insert(caps_nar(name)).second;
        }
        if (first) Output::send<LogLevel::Normal>(STR("[MSCore] COMPAT CAPS {}=off:{} (runtime)\n"), name, why);
    }
    // WHAT: true when the cap row name exists and is ok. A missing row is false (fail closed).
    bool module_ok(const std::wstring& name)
    {
        std::lock_guard<std::mutex> lk(g_caps_mx);
        for (auto& c : g_caps) if (c.name == caps_nar(name)) return c.ok;
        return false;
    }
    // WHAT: when dep is off, name is set off with why dep:<dep>. Returns module_ok(name) after the check.
    bool caps_dep(const std::wstring& name, const std::wstring& dep)
    {
        if (module_ok(dep)) return module_ok(name);
        std::lock_guard<std::mutex> lk(g_caps_mx);
        caps_set_locked(caps_nar(name), false, "dep:" + caps_nar(dep));
        return false;
    }
}
