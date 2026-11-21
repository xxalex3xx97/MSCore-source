// FILE: cpp/MSCore/msc_designer.hpp
// WHAT: Designer IsBlockAvailable null guard. WORKAROUND for a game bug: the native UFunction Designer::IsBlockAvailable(TSubclassOf<BuildingBlock>)
//       (impl RVA 0x43262b0 in build ts 0x38ec2947, exec thunk 0x4228760) does Cast<UClass>(block) and reads +0x110 (ClassDefaultObject) with no null
//       check. The Designer UMG Blueprint calls it with a NULL class, so opening the design editor crashed (EXCEPTION_ACCESS_VIOLATION reading 0x110,
//       RIP RVA 0x4326308, even in a brand new save).
// HOW: one inline detour. The impl is found by a 35 byte pattern (must match exactly once in .text, no wildcards: the bytes are its prologue).
//      The first 10 bytes (two whole instructions) are replaced by `jmp stub` + 5 NOP. The stub (msc_designer_stub.hpp) returns false when
//      the block class pointer [rdx] is null and counts it, otherwise it runs the stolen bytes and continues in the original. Nothing else changes.
// FAIL CLOSED: pattern not unique, prologue bytes differ, no memory near the module, VirtualProtect/write fault => nothing is patched, caps row
//      designer_guard=off:<why>, log "COMPAT designer_guard ...". Kill switch: file Mods\MSCore\designer_guard_off.txt (checked at start) or
//      MSCoreNative_designer_guard(false) (restores the original bytes; the stub stays allocated because a thread may still be inside it).
// LOG: "COMPAT designer_guard on target=0x<rva>" at install; Lua (mscore_designer.lua) logs "DESIGNER GUARD null block class (n=<count>)".
// DEPENDS ON: msc_refund.hpp (ref_alloc_near, ref_seh_*), msc_natives.hpp (nat_ranges), msc_pattern.hpp, msc_caps.hpp, msc_designer_stub.hpp.
// USED BY: dllmain.cpp (MSCoreNative_designer_guard / _status, on_lua_start install, destructor uninstall).
#pragma once

#include "msc_refund.hpp"
#include "msc_designer_stub.hpp"

namespace
{
    // prologue of Designer::IsBlockAvailable impl (unique in the exe, verified with a whole-file count)
    constexpr const char* DG_PAT = "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 20 48 8B 1A 49 8B E8 4C 8B F2 48 8B F9 48 85 DB 74 33";

    struct DgState
    {
        bool tried = false, patched = false;
        uint8_t* target = nullptr;
        uint8_t* mem = nullptr;
        uint8_t orig[DG_STOLEN] = {};
        std::string status = "off";
    };
    DgState g_dg;

    bool dg_write(const uint8_t* buf)
    {
        DWORD old = 0;
        if (!VirtualProtect(g_dg.target, DG_STOLEN, PAGE_EXECUTE_READWRITE, &old)) return false;
        const bool ok = ref_seh_copy(buf, g_dg.target, DG_STOLEN);
        DWORD tmp = 0;
        VirtualProtect(g_dg.target, DG_STOLEN, old, &tmp);
        FlushInstructionCache(GetCurrentProcess(), g_dg.target, DG_STOLEN);
        return ok;
    }

    // WHAT: number of times the guard returned false for a null class (0 when not installed).
    uint32_t dg_count()
    {
        if (!g_dg.mem) return 0;
        uint32_t v = 0;
        ref_seh_copy(g_dg.mem + DG_COUNTER_OFF, reinterpret_cast<uint8_t*>(&v), 4);
        return v;
    }

    bool dg_fail(const std::string& why)
    {
        g_dg.status = "COMPAT designer_guard failed: " + why;
        caps_add("designer_guard", false, why);
        msc_log(widen(g_dg.status));
        return false;
    }

    // WHAT: validate and install (once). true = patched.
    bool dg_install()
    {
        if (g_dg.tried) return g_dg.patched;
        g_dg.tried = true;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) return dg_fail("no module base");
        const std::vector<NatRange> ranges = nat_ranges(base);
        if (ranges.size() != 1) return dg_fail(".text section not found");
        std::vector<int16_t> pat;
        if (!pat_parse(DG_PAT, pat)) return dg_fail("pattern invalid");
        size_t c = 0, f = 0;
        if (!ref_seh_count(ranges[0].p, ranges[0].n, pat.data(), pat.size(), c, f)) return dg_fail("scan faulted");
        if (c != 1) return dg_fail("pattern matched " + std::to_string(c) + " times");
        g_dg.target = const_cast<uint8_t*>(ranges[0].p) + f;
        // the first DG_STOLEN pattern bytes are the stolen instructions: both are whole instructions (the pattern fixes the exact bytes)
        for (size_t i = 0; i < DG_STOLEN; ++i) g_dg.orig[i] = static_cast<uint8_t>(pat[i]);
        g_dg.mem = ref_alloc_near(g_dg.target);
        if (!g_dg.mem) return dg_fail("no executable memory near the module");
        dg_build_stub(g_dg.mem, g_dg.orig, reinterpret_cast<uint64_t>(g_dg.target) + DG_STOLEN);
        FlushInstructionCache(GetCurrentProcess(), g_dg.mem, DG_STUB_SIZE);
        const int64_t rel = reinterpret_cast<int64_t>(g_dg.mem) - reinterpret_cast<int64_t>(g_dg.target + 5);
        if (rel <= -0x7fff0000LL || rel >= 0x7fff0000LL) return dg_fail("stub out of rel32 range");
        uint8_t buf[DG_STOLEN];
        memset(buf, 0x90, sizeof buf);
        buf[0] = 0xE9;
        const int32_t r32 = static_cast<int32_t>(rel);
        memcpy(buf + 1, &r32, 4);
        if (!dg_write(buf)) return dg_fail("patch write faulted");
        g_dg.patched = true;
        char s[96];
        snprintf(s, sizeof s, "on target=0x%llx", static_cast<unsigned long long>(g_dg.target - base));
        g_dg.status = s;
        caps_add("designer_guard", true, "");
        msc_log(widen(std::string("COMPAT designer_guard ") + s));
        return true;
    }

    // WHAT: kill switch: original bytes back. Safe to call when not patched.
    void dg_uninstall(const char* why)
    {
        if (!g_dg.patched) return;
        if (dg_write(g_dg.orig)) { g_dg.patched = false; g_dg.status = std::string("off: ") + why; module_disable(L"designer_guard", widen(why)); }
    }
}
