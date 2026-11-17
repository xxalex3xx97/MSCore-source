// FILE: cpp/MSCore/msc_caps.hpp
// WHAT: one list of the feature capabilities (name, ok, why) that the resolvers register when they decide.
// WHY: one startup line ("COMPAT CAPS ...") says which features are on, so a game patch shows up in the log at once.
// LOG: caps_text() is appended to the lua_offsets text and logged at startup by dllmain.cpp.
// USED BY: msc_natives.hpp (chunkhold), msc_refund.hpp (refund, refund_groups, refund_lookup, refund_offsets), msc_station.hpp (split), dllmain.cpp.
#pragma once

#include <string>
#include <vector>

namespace
{
    struct CapRow { std::string name; bool ok; std::string why; };
    std::vector<CapRow> g_caps;
    void caps_add(const std::string& name, bool ok, const std::string& why) { g_caps.push_back({ name, ok, why }); }
    // "name=ok name=off:why ..." (each entry followed by a space)
    std::string caps_text() { std::string s; for (auto& c : g_caps) s += c.name + (c.ok ? "=ok" : "=off:" + c.why) + " "; return s; }
}
