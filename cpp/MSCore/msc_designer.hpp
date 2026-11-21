// FILE: cpp/MSCore/msc_designer.hpp
// WHAT: Designer IsBlockAvailable null guard. WORKAROUND for a game bug: the native UFunction Designer::IsBlockAvailable(TSubclassOf<BuildingBlock>)
//       (impl RVA 0x43262b0 in build ts 0x38ec2947, exec thunk 0x4228760) does Cast<UClass>(block) and reads +0x110 (ClassDefaultObject) with no null
//       check. The Designer UMG Blueprint calls it with a NULL class, so opening the design editor crashed (EXCEPTION_ACCESS_VIOLATION reading 0x110,
//       RIP RVA 0x4326308, even in a brand new save).
// HOW: one inline detour. The impl is found by a 35 byte pattern (must match exactly once in .text, no wildcards: the bytes are its prologue).
//      The first 10 bytes are two whole instructions; the stub (msc_designer_stub.hpp) holds a copy of them. Only the first 8 bytes of the target
//      are written, in ONE interlocked 8-byte compare-exchange (`jmp stub` + the unchanged bytes 5..7), see the PATCH note in msc_designer_stub.hpp.
//      If the 8 bytes straddle a cache line (addr%64 > 56) all other threads are suspended around the write (never the current one; if any
//      suspend fails nothing is written). The stub returns false when the block class pointer [rdx] is null and counts it (lock inc), otherwise it
//      runs the stolen bytes and continues in the original. Nothing else changes.
// PATTERN GUARD: besides the 35 byte pattern the live bytes 0..9 must equal the two expected instructions (whole instruction boundary at 5 and 10).
//      LIMIT: the guard only covers the fall-through path of `test rbx,rbx / je` in the original (the null class read at [rdx] happens before
//      it). If a future build changes the je-taken path the guard cannot protect it; the pattern then no longer matches and nothing is patched.
// FAIL CLOSED: pattern not unique, prologue bytes differ, no memory near the module, VirtualProtect/write fault => nothing is patched, caps row
//      designer_guard=off:<why>, log "COMPAT designer_guard ...". Kill switch: file Mods\MSCore\designer_guard_off.txt (checked at start) or
//      MSCoreNative_designer_guard(false) (restores the original bytes with one interlocked store; the stub stays allocated because a thread may still
//      be inside it; a failed restore keeps the guard "on" and reports "restore failed"; a successful one lets MSCoreNative_designer_guard(true)
//      re-verify and re-patch).
// LOG: "COMPAT designer_guard on target=0x<rva>" at install; Lua (mscore_designer.lua) logs "DESIGNER GUARD null block class (n=<count>)".
// DEPENDS ON: msc_refund.hpp (ref_alloc_near, ref_seh_*), msc_natives.hpp (nat_ranges), msc_pattern.hpp, msc_caps.hpp, msc_designer_stub.hpp.
// USED BY: dllmain.cpp (MSCoreNative_designer_guard / _status, on_lua_start install, destructor uninstall).
#pragma once

#include "msc_refund.hpp"
#include "msc_designer_stub.hpp"
#include <TlHelp32.h>

namespace
{
    // prologue of Designer::IsBlockAvailable impl (unique in the exe, verified with a whole-file count)
    constexpr const char* DG_PAT = "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 20 48 8B 1A 49 8B E8 4C 8B F2 48 8B F9 48 85 DB 74 33";
    // the two stolen instructions: mov [rsp+10h],rbx / mov [rsp+18h],rbp (5 + 5 bytes, no rip-relative operand)
    constexpr uint8_t DG_EXPECT[DG_STOLEN] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18 };

    struct DgState
    {
        bool tried = false, patched = false, restoreLogged = false;
        uint8_t* target = nullptr;
        uint8_t* mem = nullptr;
        uint8_t orig[DG_STOLEN] = {};
        uint8_t jmp8[DG_PATCH] = {};
        std::string status = "off";
    };
    DgState g_dg;

    bool dg_seh_cas8(uint8_t* p, const uint8_t* expect, const uint8_t* want)
    {
        __try { return dg_cas8(p, expect, want); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // Suspends every other thread of the process (never the calling one). ok=false when a suspend failed (then nothing stays suspended).
    struct DgFreeze
    {
        std::vector<HANDLE> h;
        bool ok = true;
        DgFreeze()
        {
            const DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap == INVALID_HANDLE_VALUE) { ok = false; return; }
            THREADENTRY32 te;
            te.dwSize = sizeof te;
            for (BOOL b = Thread32First(snap, &te); b && ok; b = Thread32Next(snap, &te))
            {
                if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
                HANDLE t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (!t) { ok = false; break; }
                if (SuspendThread(t) == static_cast<DWORD>(-1)) { CloseHandle(t); ok = false; break; }
                h.push_back(t);
            }
            CloseHandle(snap);
            if (!ok) release();
        }
        void release() { for (HANDLE t : h) { ResumeThread(t); CloseHandle(t); } h.clear(); }
        ~DgFreeze() { release(); }
    };

    // WHAT: atomically swaps the 8 bytes at the target (expect -> want). false + why on failure. Page made RWX around the write.
    bool dg_swap(const uint8_t* expect, const uint8_t* want, const char*& why)
    {
        DWORD old = 0;
        if (!VirtualProtect(g_dg.target, DG_PATCH, PAGE_EXECUTE_READWRITE, &old)) { why = "VirtualProtect failed"; return false; }
        bool ok = false;
        why = "target bytes changed or write faulted";
        if ((reinterpret_cast<uintptr_t>(g_dg.target) & 63) > 56)
        {
            // 8 bytes straddle a cache line: a locked op is still atomic, but instruction fetch of a split line is not guaranteed
            // consistent, so park every other thread for the duration.
            DgFreeze fz;
            if (fz.ok) ok = dg_seh_cas8(g_dg.target, expect, want); else why = "suspending other threads failed";
        }
        else ok = dg_seh_cas8(g_dg.target, expect, want);
        DWORD tmp = 0;
        VirtualProtect(g_dg.target, DG_PATCH, old, &tmp);
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

    // dg_fail + free the stub page allocated by this install (nothing runs in it: the target was not patched)
    bool dg_fail_free(const std::string& why, bool fresh)
    {
        if (fresh && g_dg.mem) { VirtualFree(g_dg.mem, 0, MEM_RELEASE); g_dg.mem = nullptr; }
        return dg_fail(why);
    }

    // WHAT: validate and install. true = patched. A failed verification is final for the process; after a successful dg_uninstall the
    //       whole verification runs again before the target is patched again.
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
        uint8_t* const target = const_cast<uint8_t*>(ranges[0].p) + f;
        // the live stolen bytes must be exactly the two expected whole instructions
        uint8_t live[DG_STOLEN];
        if (!ref_seh_copy(target, live, sizeof live)) return dg_fail("target unreadable");
        if (memcmp(live, DG_EXPECT, sizeof live) != 0) return dg_fail("stolen bytes differ from the expected two instructions");
        if (g_dg.mem && g_dg.target != target) return dg_fail("target moved since the last install");
        g_dg.target = target;
        memcpy(g_dg.orig, live, sizeof live);
        const bool fresh = g_dg.mem == nullptr;   // an old stub page (after a kill switch) is reused as is: a thread may still be inside it
        if (fresh)
        {
            g_dg.mem = ref_alloc_near(target);
            if (!g_dg.mem) return dg_fail("no executable memory near the module");
        }
        const int64_t rel = reinterpret_cast<int64_t>(g_dg.mem) - reinterpret_cast<int64_t>(target + 5);
        if (rel <= -0x7fff0000LL || rel >= 0x7fff0000LL) return dg_fail_free("stub out of rel32 range", fresh);
        if (fresh)
        {
            dg_build_stub(g_dg.mem, g_dg.orig, reinterpret_cast<uint64_t>(target) + DG_STOLEN);
            FlushInstructionCache(GetCurrentProcess(), g_dg.mem, DG_STUB_SIZE);
        }
        dg_patch_bytes(g_dg.orig, static_cast<int32_t>(rel), g_dg.jmp8);
        const char* why = "";
        if (!dg_swap(g_dg.orig, g_dg.jmp8, why)) return dg_fail_free(std::string("patch failed: ") + why, fresh);
        g_dg.patched = true;
        char s[96];
        snprintf(s, sizeof s, "on target=0x%llx", static_cast<unsigned long long>(target - base));
        g_dg.status = s;
        caps_add("designer_guard", true, "");
        msc_log(widen(std::string("COMPAT designer_guard ") + s));
        return true;
    }

    // WHAT: original bytes back (one interlocked 8-byte store). Silent, never throws. true = bytes are original again (or never patched).
    bool dg_restore_bytes() noexcept
    {
        if (!g_dg.patched) return true;
        const char* why = "";
        try { if (!dg_swap(g_dg.jmp8, g_dg.orig, why)) return false; } catch (...) { return false; }
        g_dg.patched = false;
        return true;
    }

    // WHAT: kill switch. Success: status "off: <why>", module disabled, install may run again. Failure: guard stays "on", status says
    //       "restore failed: <reason>" (logged once). Safe to call when not patched.
    bool dg_uninstall(const char* why)
    {
        if (!g_dg.patched) return true;
        if (dg_restore_bytes())
        {
            g_dg.tried = false;
            g_dg.status = std::string("off: ") + why;
            module_disable(L"designer_guard", widen(why));
            return true;
        }
        g_dg.status = std::string("restore failed: could not write the original bytes back, guard still ON (") + why + ")";
        if (!g_dg.restoreLogged) { g_dg.restoreLogged = true; msc_log(widen("COMPAT designer_guard " + g_dg.status)); }
        return false;
    }
}
