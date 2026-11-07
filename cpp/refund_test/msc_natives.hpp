// FILE: cpp/refund_test/msc_natives.hpp
// WHAT: FAKE stand-in for the UE4SS-dependent headers (msc_natives/msc_cmd/msc_ue) so msc_refund.hpp compiles in a plain MSVC test (scripts\test-refund.ps1).
//       Only what msc_refund.hpp uses: UObject/UClass fakes, alive, seh_run, msc_log, widen, STR, NatRange/nat_ranges, pat_*.
#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "msc_pattern.hpp"

#define STR(x) L##x

struct UClass { std::wstring name; std::wstring GetName() const { return name; } };
// raw layout like the game for the fields msc_refund.hpp touches by offset is built by the test; these members sit at +0x10 / +0x14
struct UObject
{
    uint8_t pad[0x10];
    UClass* cls;
    int32_t root;
    UClass* GetClassPrivate() { return cls; }
    bool IsUnreachable() { return false; }
    void SetRootSet() { root = 1; }
};

namespace
{
    std::vector<std::wstring> g_test_logs;
    void msc_log(const std::wstring& s) { g_test_logs.push_back(s); }
    std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
    bool alive(UObject* o) { return o && o->cls; }
    template <typename F> bool seh_run(F&& f) { __try { f(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    struct NatRange { const uint8_t* p; size_t n; };
    std::vector<NatRange> nat_ranges(const uint8_t*) { return {}; }
}
