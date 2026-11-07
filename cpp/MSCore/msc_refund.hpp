// FILE: cpp/MSCore/msc_refund.hpp
// WHAT: Blueprint Unit refund / build-charge REDIRECT (0.7.5, default ON) plus the 0.7.4 log probe (default OFF). Two hand rolled inline detours:
//       (a) DisassembleSelf (machine refund; rcx machine, rdx player)  (b) the build-charge inventory helper (rcx player; the stub also
//       passes rdi = caller BuildCommand* and the return address). Each hook CALLS the original through the trampoline.
//       REFUND: when the machine's station (frame) holds a Blueprint Unit container (BP_BPUnitStoreObj_01_C), [player+0x288] is swapped for the
//         game's InventoryGroup [unit_inventory, player_inventory] while the original runs (SEH __finally restores it), so refund items fill the
//         unit first and overflow to the player inventory, and only then to the game's drop object.
//       CHARGE: for the 4 whitelisted sim call sites the helper returns the group [unit_inventory, normal inventory] instead of the normal
//         inventory (GetAmount sums members, Remove takes unit first).
//       Groups: one persistent rooted InventoryGroup per path, made with the game's own constructor (RVA 0x421a0b0, byte guarded) on the game
//       thread at first use; the two member slots (TArray at +0x70, count +0x78) are overwritten per use. Refund group members are nulled after
//       use. If no group can be made, refund falls back to swapping [player+0x288] to the unit inventory itself; charge then does nothing.
// WHY: lets players keep build materials and deconstruction refunds in the Blueprint Unit.
// SAFETY: fail closed. Nothing is installed unless: exe size == 149200896, both entry patterns match exactly once in .text, the expected 7
//         entry bytes are there, and exactly 4 call sites `48 8B 4F 60 E8 rel32` call the charge helper. Any failure installs nothing and logs
//         "COMPAT refund ...". Any doubt in a lookup (frame not a station, no unit, inventory dead, creative sender) calls the original unchanged.
//         All raw game reads run inside SEH; hook bodies catch everything and have reentrancy guards.
// Co-op (future work): decision uses sim state only (frame, container class, ids). The game is lockstep: a client without the hook would put
//         refund/charge in different inventories => desync. Client_ValidateDeterminism behaviour is unknown. Needs every player on the same MSCore
//         version with the redirect on. Fail closed on ambiguity. The pak with BP_BPUnitStoreObj_01_C is mandatory. Designer/teardown paths are untested.
// SWITCHES: redirect is ON at MSCore start when validation passes; OFF with file Mods\MSCore\refund_redirect_off.txt or Lua
//         MSCoreNative_redirect_enable(false). Probe logging: file Mods\MSCore\refund_probe.txt or MSCoreNative_refund_enable(true).
// LOG LINES (also in a 200 line ring buffer, Lua MSCoreNative_refund_log()):
//   REDIRECT refund machine=<id> unit_inv=<id> mode=<group|fallback> n=<count>      (first 20, then every 25th; leftover is not cheaply readable)
//   REDIRECT charge site=<0..3> station=<id> unit_inv=<id> n=<count>                (same rate limit)
//   REDIRECT on (groups=<yes|no>)   /   COMPAT refund ...                            (validation, group problems)
//   PROBE refund / PROBE charge / PROBE other-site                                    (only when the probe switch is on)
// STATS: Lua MSCoreNative_redirect_stats() -> "refund=<n> refund_fallback=<n> charge=<n> skipped=<n> group_made=<n> group_fail=<n> errors=<n>"
// DEPENDS ON: msc_cmd.hpp (alive, seh_run), msc_ue.hpp (msc_log, widen), msc_natives.hpp (nat_ranges), msc_pattern.hpp (pat_parse, pat_count).
// USED BY: dllmain.cpp (MSCoreNative_refund_* / _redirect_*, destructor uninstall, on_lua_start switches).
#pragma once

#include <Windows.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>
#include "msc_natives.hpp"

namespace
{
    constexpr uint64_t REF_EXE_SIZE = 149200896;   // the verified build
    constexpr const char* REF_PAT_DISASM = "48 8B C4 55 48 8B EC 48 83 EC 70 48 89 58 08 48 89 70 10 48 89 78 18 48 8B F9 4C 89 60 20 45 33 E4 4C 89 70 F0 4C 89 78 E8 48 8B 81 80 01 00 00 4C 8B BA 88 02 00 00";   // va 0x144226170
    constexpr const char* REF_PAT_CHARGE = "48 8B 81 40 02 00 00 48 85 C0 74 18 8B 40 08 0F BA E0 1E 72 0F 48 8B 81 40 02 00 00";   // va 0x14427f120
    constexpr uint8_t REF_HEAD_DISASM[7] = { 0x48, 0x8B, 0xC4, 0x55, 0x48, 0x8B, 0xEC };   // mov rax,rsp; push rbp; mov rbp,rsp
    constexpr uint8_t REF_HEAD_CHARGE[7] = { 0x48, 0x8B, 0x81, 0x40, 0x02, 0x00, 0x00 };   // mov rax,[rcx+0x240]
    // FindObject(map owner, id) -> UObject*: a pure hash lookup (many identical template copies exist, so an RVA + byte check, not a unique pattern)
    constexpr uint64_t REF_FINDOBJ_RVA = 0x4220ee0;
    constexpr const char* REF_PAT_FINDOBJ = "40 53 48 83 EC 20 8B 41 48 8B DA 3B 41 74 74 47 44 8B 81 88 00 00 00 4C 8D 51 78 4D 8B 4A 08 41 FF C8 4C 23 C3";
    constexpr size_t REF_SITES = 4;
    constexpr size_t REF_RING = 200;
    constexpr const char* REF_CONTAINER_CLASS = "BP_BPUnitStoreObj_01_C";
    constexpr const char* REF_STATION_CLASS = "BP_StationObject_C";
    // InventoryGroup constructor (primary, secondary) -> group, va 0x14421a0b0. '?' = rip-relative displacement (4 bytes).
    constexpr uint64_t REF_GCTOR_RVA = 0x421a0b0;
    constexpr const char* REF_PAT_GCTOR = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 A0 00 00 00 48 8B EA 48 8B F1 E8";
    constexpr uint64_t REF_GROUP_VT_RVA = 0x734e238;   // inner vtable of an InventoryGroup (at group+0x68), va 0x14734e238
    constexpr int REF_LOG_FIRST = 20;                  // REDIRECT lines: first N, then every REF_LOG_EVERY-th
    constexpr int REF_LOG_EVERY = 25;

    // one inline hook: entry bytes saved, stub + trampoline in one executable page near the module
    struct RefHook
    {
        uint8_t* target = nullptr;
        uint8_t orig[7] = {};
        uint8_t* mem = nullptr;   // stub at +0, trampoline at +0x100; never freed (a thread may still be inside)
        bool patched = false;
    };

    struct RefState
    {
        bool enabled = false;
        bool tried = false;       // validated once; a failed validation is sticky
        bool valid = false;
        std::string status = "off";
        uint8_t* base = nullptr;
        uint8_t* findobj = nullptr;
        uint8_t* gctor = nullptr;           // group constructor (null = groups unavailable)
        bool redirect = false;              // redirect switch (default on at start)
        void* grp_refund = nullptr;         // persistent rooted InventoryGroup objects
        void* grp_charge = nullptr;
        int group_fail_n = 0;               // sticky: after 3 failed creations stop trying
        std::atomic<uint64_t> st_refund{0}, st_refund_fb{0}, st_charge{0}, st_skip{0}, st_gmade{0}, st_gfail{0}, st_err{0};
        std::unordered_map<void*, uint32_t> cls_flags;   // UClass* -> bit0 unit container, bit1 BPU class, bit2 station, bit3 known
        uint64_t sites[REF_SITES] = {};   // return addresses (absolute) of the four whitelisted call sites
        RefHook a, b;
        std::deque<std::string> ring;
        std::mutex mu;
        uint64_t win_start = 0;
        int win_lines = 0;
        int win_dropped = 0;
        uint64_t other_ret[8] = {};
        uint64_t other_n[8] = {};
    };
    RefState g_ref;

    // ---------------- SEH POD helpers ----------------

    template <typename T>
    bool ref_rd(const void* p, T& out)
    {
        __try { out = *reinterpret_cast<const T*>(p); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // pat_count inside SEH (the scan reads the whole exe)
    bool ref_seh_count(const uint8_t* buf, size_t n, const int16_t* pat, size_t plen, size_t& cnt, size_t& first)
    {
        __try { cnt = pat_count(buf, n, pat, plen, &first); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: call sites `48 8B 4F 60 E8 rel32` whose target is `target`. out gets the return address (call + 5), cnt the total (may exceed max).
    bool ref_seh_sites(const uint8_t* p, size_t n, const uint8_t* target, uint64_t* out, size_t max, size_t& cnt)
    {
        __try
        {
            cnt = 0;
            for (size_t i = 0; i + 9 <= n; ++i)
            {
                if (p[i] != 0x48 || p[i + 1] != 0x8B || p[i + 2] != 0x4F || p[i + 3] != 0x60 || p[i + 4] != 0xE8) continue;
                int32_t rel = *reinterpret_cast<const int32_t*>(p + i + 5);
                if (p + i + 9 + rel != target) continue;
                if (cnt < max) out[cnt] = reinterpret_cast<uint64_t>(p + i + 9);
                ++cnt;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ref_seh_copy(const uint8_t* p, uint8_t* out, size_t n)
    {
        __try { memcpy(out, p, n); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ref_seh_eq(const uint8_t* p, const uint8_t* want, size_t n)
    {
        __try { return memcmp(p, want, n) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // FindObject(owner, id) through the game's lookup; null on a fault or when absent
    void* ref_find(void* owner, int32_t id)
    {
        if (!g_ref.findobj || !owner || id <= 0) return nullptr;
        __try { return reinterpret_cast<void* (*)(void*, uint32_t)>(g_ref.findobj)(owner, static_cast<uint32_t>(id)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // ---------------- logging ----------------

    // WHAT: ring buffer + UE4SS log. At most 60 lines per second, the rest are counted and reported on the next line.
    void ref_say(const std::string& s)
    {
        std::lock_guard<std::mutex> g(g_ref.mu);
        const uint64_t now = GetTickCount64();
        if (now - g_ref.win_start >= 1000) { g_ref.win_start = now; g_ref.win_lines = 0; }
        if (g_ref.win_lines >= 60) { ++g_ref.win_dropped; return; }
        ++g_ref.win_lines;
        std::string line = s;
        if (g_ref.win_dropped) { line += " (dropped " + std::to_string(g_ref.win_dropped) + " lines)"; g_ref.win_dropped = 0; }
        g_ref.ring.push_back(line);
        while (g_ref.ring.size() > REF_RING) g_ref.ring.pop_front();
        msc_log(widen(line));
    }

    // class name of a game object (empty when not alive)
    std::string ref_cls(void* o)
    {
        std::string out;
        seh_run([&] {
            UObject* u = reinterpret_cast<UObject*>(o);
            if (alive(u)) { for (wchar_t c : u->GetClassPrivate()->GetName()) out += static_cast<char>(c); }
        });
        return out;
    }

    std::string ref_num(bool ok, int64_t v) { return ok ? std::to_string(v) : std::string("none"); }

    // id at +0x30 of a game object (-1 when unreadable)
    int32_t ref_id(void* o)
    {
        int32_t v = -1;
        if (!o || !ref_rd(reinterpret_cast<uint8_t*>(o) + 0x30, v)) return -1;
        return v;
    }

    // ---------------- probe bodies (log only) ----------------

    void ref_probe_disasm(void* machine, void* player)
    {
        uint8_t* m = reinterpret_cast<uint8_t*>(machine);
        uint8_t* p = reinterpret_cast<uint8_t*>(player);
        int32_t frame_id = 0;
        void* sim = nullptr;
        ref_rd(m + 0x34, frame_id);
        ref_rd(m + 0x28, sim);
        const std::string mcls = ref_cls(machine);
        void* frame = ref_find(sim, frame_id);
        const std::string fcls = frame ? ref_cls(frame) : std::string();

        uint8_t* data = nullptr;
        int32_t num = 0, mx = 0;
        bool arr = frame && ref_rd(reinterpret_cast<uint8_t*>(frame) + 0x368, data) && ref_rd(reinterpret_cast<uint8_t*>(frame) + 0x370, num) &&
                   ref_rd(reinterpret_cast<uint8_t*>(frame) + 0x374, mx) && data && num >= 0 && num <= mx && num < 200000;
        void* container = nullptr;
        int32_t cid = 0x7fffffff;
        if (arr)
            for (int32_t i = 0; i < num && i < 20000; ++i)
            {
                void* e = nullptr;
                if (!ref_rd(data + static_cast<size_t>(i) * 8, e) || !e) continue;
                if (ref_cls(e) != REF_CONTAINER_CLASS) continue;
                const int32_t id = ref_id(e);
                if (id >= 0 && id < cid) { cid = id; container = e; }
            }
        void* cinv = nullptr;
        int32_t cinv_id = 0;
        const bool has_cinv = container && ref_rd(reinterpret_cast<uint8_t*>(container) + 0x1b0, cinv) && cinv && ref_id(cinv) >= 0;
        if (has_cinv) cinv_id = ref_id(cinv);
        void* pinv = nullptr;
        const bool has_pinv = ref_rd(p + 0x288, pinv) && pinv && ref_id(pinv) >= 0;

        const char* why = "ok";
        if (!frame) why = "frame not found";
        else if (!arr) why = "frame machine array invalid";
        else if (!container) why = "no Blueprint Unit container in frame";
        else if (container == machine) why = "machine is the container";
        else if (mcls.rfind("BP_BPUnit", 0) == 0) why = "machine is a Blueprint Unit class";
        else if (!has_cinv) why = "container has no inventory";
        else if (!has_pinv) why = "player has no inventory";
        const bool yes = why[0] == 'o';

        ref_say("PROBE refund machine=" + std::to_string(ref_id(machine)) + " cls=" + mcls + " frame=" + std::to_string(frame_id) + "(" +
                (frame ? fcls : std::string("nofind")) + ") machines=" + (arr ? std::to_string(num) + "/" + std::to_string(mx) : std::string("?")) +
                " arr=" + (arr ? "ok" : "bad") + " container=" + (container ? std::to_string(cid) : std::string("none")) +
                " cont_inv=" + ref_num(has_cinv, cinv_id) + " player=" + std::to_string(ref_id(player)) + " player_inv=" +
                ref_num(has_pinv, ref_id(pinv)) + " would_redirect=" + (yes ? "yes" : "no") + " reason=" + why);
    }

    void ref_probe_charge(void* player, void* cmd, uint64_t ret)
    {
        int site = -1;
        for (size_t i = 0; i < REF_SITES; ++i) if (g_ref.sites[i] == ret) site = static_cast<int>(i);
        const uint64_t rva = ret - reinterpret_cast<uint64_t>(g_ref.base);
        char hex[32];
        snprintf(hex, sizeof hex, "0x%llx", static_cast<unsigned long long>(rva));
        if (site < 0)
        {
            // cheap path: count per return address, log the first and every 500th
            int slot = -1;
            for (int i = 0; i < 8; ++i) if (g_ref.other_ret[i] == ret) { slot = i; break; }
            if (slot < 0) for (int i = 0; i < 8; ++i) if (!g_ref.other_ret[i]) { g_ref.other_ret[i] = ret; slot = i; break; }
            if (slot < 0) return;
            const uint64_t n = ++g_ref.other_n[slot];
            if (n == 1 || n % 500 == 0) ref_say(std::string("PROBE other-site ret=") + hex + " n=" + std::to_string(n));
            return;
        }
        int32_t attach = 0;
        uint8_t creative = 0;
        void* sim = nullptr;
        ref_rd(reinterpret_cast<uint8_t*>(cmd) + 0x34, attach);
        ref_rd(reinterpret_cast<uint8_t*>(player) + 0x370, creative);
        ref_rd(reinterpret_cast<uint8_t*>(player) + 0x28, sim);
        std::string chain;
        int levels = 0;
        int32_t id = attach;
        for (int i = 0; i < 4 && id > 0; ++i)
        {
            void* o = ref_find(sim, id);
            if (!o) break;
            chain += (chain.empty() ? "" : ">") + ref_cls(o) + "(" + std::to_string(id) + ")";
            ++levels;
            int32_t parent = 0;
            if (!ref_rd(reinterpret_cast<uint8_t*>(o) + 0x34, parent)) break;
            id = parent;
        }
        const char* why = "ok";
        if (creative) why = "sender creative";
        else if (!levels) why = "attach target not found";
        const bool yes = why[0] == 'o';
        ref_say("PROBE charge site=" + std::to_string(site) + " ret=" + hex + " player=" + std::to_string(ref_id(player)) +
                " attach=" + std::to_string(attach) + " creative=" + std::to_string(creative) + " chain=" + (chain.empty() ? std::string("none") : chain) +
                " would_redirect=" + (yes ? "yes" : "no") + " reason=" + why);
    }

    // ---------------- redirect: lookups (sim state only; co-op later: decision uses sim state only) ----------------

    using RefFn2 = uint64_t (*)(void*, void*);
    RefFn2 ref_tramp(const RefHook& h) { return reinterpret_cast<RefFn2>(h.mem + 0x100); }   // the original, through the trampoline

    std::mutex g_cls_mu;

    void* ref_class_ptr(void* o)
    {
        __try { UObject* u = reinterpret_cast<UObject*>(o); if (!alive(u)) return nullptr; return u->GetClassPrivate(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // WHAT: class flags of an object, cached per UClass*: 1 unit container, 2 BP_BPUnit* class, 4 station, 8 known. 0 when the object is not alive.
    uint32_t ref_cflags(void* o)
    {
        void* cls = ref_class_ptr(o);
        if (!cls) return 0;
        {
            std::lock_guard<std::mutex> g(g_cls_mu);
            auto it = g_ref.cls_flags.find(cls);
            if (it != g_ref.cls_flags.end()) return it->second;
        }
        const std::string n = ref_cls(o);
        uint32_t f = 8;
        if (n == REF_CONTAINER_CLASS) f |= 1;
        if (n.rfind("BP_BPUnit", 0) == 0) f |= 2;
        if (n == REF_STATION_CLASS) f |= 4;
        std::lock_guard<std::mutex> g(g_cls_mu);
        g_ref.cls_flags[cls] = f;
        return f;
    }

    struct RefUnit { void* container = nullptr; void* inv = nullptr; int32_t cid = -1, iid = -1, station = -1; };

    // WHAT: the Blueprint Unit container (lowest id) in a station object's machine list and its Inventory. false on any doubt.
    // ponytail: linear scan per call (O(machines)); cache per sim frame if mass deconstruct of thousands of machines proves slow.
    bool ref_unit_in(void* station, RefUnit& u)
    {
        uint8_t* st = reinterpret_cast<uint8_t*>(station);
        uint8_t* data = nullptr;
        int32_t num = 0, mx = 0;
        if (!ref_rd(st + 0x368, data) || !ref_rd(st + 0x370, num) || !ref_rd(st + 0x374, mx) || !data || num < 0 || num > mx || num >= 200000) return false;
        int32_t best = 0x7fffffff;
        for (int32_t i = 0; i < num && i < 20000; ++i)
        {
            void* e = nullptr;
            if (!ref_rd(data + static_cast<size_t>(i) * 8, e) || !e) continue;
            if (!(ref_cflags(e) & 1)) continue;
            const int32_t id = ref_id(e);
            if (id >= 0 && id < best) { best = id; u.container = e; u.cid = id; }
        }
        if (!u.container) return false;
        void* inv = nullptr;
        if (!ref_rd(reinterpret_cast<uint8_t*>(u.container) + 0x1b0, inv) || !inv || !alive(reinterpret_cast<UObject*>(inv))) return false;
        u.iid = ref_id(inv);
        if (u.iid < 0) return false;
        u.inv = inv;
        return true;
    }

    // machine -> its frame ([machine+0x34]) must be a BP_StationObject_C
    bool ref_unit_machine(void* machine, RefUnit& u)
    {
        int32_t fid = 0;
        void* sim = nullptr;
        if (!ref_rd(reinterpret_cast<uint8_t*>(machine) + 0x34, fid) || !ref_rd(reinterpret_cast<uint8_t*>(machine) + 0x28, sim)) return false;
        void* frame = ref_find(sim, fid);
        if (!frame || !(ref_cflags(frame) & 4)) return false;
        u.station = fid;
        return ref_unit_in(frame, u);
    }

    // build command AttachTargetID -> walk [obj+0x34] up (max 4 levels) to the BP_StationObject_C
    bool ref_unit_attach(void* sim, int32_t id, RefUnit& u)
    {
        for (int i = 0; i < 4 && id > 0; ++i)
        {
            void* o = ref_find(sim, id);
            if (!o) return false;
            if (ref_cflags(o) & 4) { u.station = id; return ref_unit_in(o, u); }
            int32_t parent = 0;
            if (!ref_rd(reinterpret_cast<uint8_t*>(o) + 0x34, parent)) return false;
            id = parent;
        }
        return false;
    }

    // ---------------- redirect: InventoryGroup management ----------------

    bool ref_group_ok(void* g)
    {
        __try
        {
            if (!alive(reinterpret_cast<UObject*>(g))) return false;
            const uint8_t* b = reinterpret_cast<const uint8_t*>(g);
            if (*reinterpret_cast<const uint64_t*>(b + 0x68) != reinterpret_cast<uint64_t>(g_ref.base) + REF_GROUP_VT_RVA) return false;
            return *reinterpret_cast<void* const*>(b + 0x70) && *reinterpret_cast<const int32_t*>(b + 0x78) == 2 && *reinterpret_cast<const int32_t*>(b + 0x7c) >= 2;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ref_group_fill(void* g, void* a, void* b)
    {
        __try { void** d = *reinterpret_cast<void***>(reinterpret_cast<uint8_t*>(g) + 0x70); d[0] = a; d[1] = b; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void ref_group_null(void* g) { ref_group_fill(g, nullptr, nullptr); }

    // WHAT: new group through the game's constructor, checked (vtable, 2 members == a, b) and rooted. null on any failure.
    void* ref_group_make(void* a, void* b)
    {
        __try
        {
            void* g = reinterpret_cast<void* (*)(void*, void*)>(g_ref.gctor)(a, b);
            if (!g || !ref_group_ok(g)) return nullptr;
            void** d = *reinterpret_cast<void***>(reinterpret_cast<uint8_t*>(g) + 0x70);
            if (d[0] != a || d[1] != b) return nullptr;
            reinterpret_cast<UObject*>(g)->SetRootSet();
            return g;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // WHAT: the persistent group in slot, members set to [unit, other]. Created on first use (game thread). null = no group (caller falls back).
    void* ref_group_get(void*& slot, void* unit, void* other)
    {
        if (!g_ref.gctor || g_ref.group_fail_n >= 3) return nullptr;
        if (slot && !ref_group_ok(slot)) { slot = nullptr; msc_log(STR("COMPAT refund: persistent group invalid, recreating")); }
        if (!slot)
        {
            slot = ref_group_make(unit, other);
            if (!slot)
            {
                ++g_ref.st_gfail;
                if (++g_ref.group_fail_n >= 3) ref_say("COMPAT refund: group creation failed 3 times, groups off (refund falls back to unit inventory swap, charge redirect off)");
                return nullptr;
            }
            ++g_ref.st_gmade;
        }
        return ref_group_fill(slot, unit, other) ? slot : nullptr;
    }

    bool ref_log_due(uint64_t n) { return n <= REF_LOG_FIRST || n % REF_LOG_EVERY == 0; }

    // ---------------- redirect: plans (read-only decisions; the swap itself is done by the hook) ----------------

    // WHAT: inventory to place in [player+0x288] for this refund (group or unit inventory), or null = leave unchanged. saved = the current value.
    void* ref_plan_refund(void* machine, void* player, void*& saved, void*& grp)
    {
        if (!ref_rd(reinterpret_cast<uint8_t*>(player) + 0x288, saved) || !saved || !alive(reinterpret_cast<UObject*>(saved))) { ++g_ref.st_skip; return nullptr; }
        const uint32_t mf = ref_cflags(machine);
        RefUnit u;
        if (!(mf & 8) || (mf & 3) || !ref_unit_machine(machine, u) || u.container == machine || u.inv == saved) { ++g_ref.st_skip; return nullptr; }
        void* g = ref_group_get(g_ref.grp_refund, u.inv, saved);
        const uint64_t n = g ? ++g_ref.st_refund : ++g_ref.st_refund_fb;
        if (ref_log_due(n))
            ref_say("REDIRECT refund machine=" + std::to_string(ref_id(machine)) + " unit_inv=" + std::to_string(u.iid) + " mode=" + (g ? "group" : "fallback") +
                    " n=" + std::to_string(n));
        grp = g;
        return g ? g : u.inv;
    }

    // WHAT: group to return from the charge helper, or null = return the normal inventory. Only for the whitelisted sim call sites.
    void* ref_plan_charge(void* player, void* cmd, uint64_t ret, void* orig)
    {
        int site = -1;
        for (size_t i = 0; i < REF_SITES; ++i) if (g_ref.sites[i] == ret) site = static_cast<int>(i);
        if (site < 0) return nullptr;
        uint8_t creative = 0;
        int32_t attach = 0;
        void* sim = nullptr;
        if (!orig || !alive(reinterpret_cast<UObject*>(orig)) || !ref_rd(reinterpret_cast<uint8_t*>(player) + 0x370, creative) || creative ||
            !ref_rd(reinterpret_cast<uint8_t*>(cmd) + 0x34, attach) || !ref_rd(reinterpret_cast<uint8_t*>(player) + 0x28, sim) || attach <= 0)
        { ++g_ref.st_skip; return nullptr; }
        RefUnit u;
        if (!ref_unit_attach(sim, attach, u) || u.inv == orig) { ++g_ref.st_skip; return nullptr; }
        void* g = ref_group_get(g_ref.grp_charge, u.inv, orig);
        if (!g) { ++g_ref.st_skip; return nullptr; }
        const uint64_t n = ++g_ref.st_charge;
        if (ref_log_due(n))
            ref_say("REDIRECT charge site=" + std::to_string(site) + " station=" + std::to_string(u.station) + " unit_inv=" + std::to_string(u.iid) + " n=" + std::to_string(n));
        return g;
    }

    // ---------------- hook entries (the stub tail-jumps here with r8 = rdi, r9 = return address; they replace the game functions) ----------------

    thread_local bool t_dis_busy = false;
    thread_local bool t_chg_busy = false;
    struct RefBusy { bool& f; explicit RefBusy(bool& x) : f(x) { f = true; } ~RefBusy() { f = false; } };

    // WHAT: runs the original with [player+0x288] = newinv; the slot (and the group members) are restored even when the original faults.
    uint64_t ref_call_swapped(RefFn2 orig, void* machine, void* player, void* newinv, void* saved, void* grp)
    {
        void** slot = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(player) + 0x288);
        uint64_t r = 0;
        __try { *slot = newinv; r = orig(machine, player); }
        __finally { *slot = saved; if (grp) ref_group_null(grp); t_dis_busy = false; }
        return r;
    }

    uint64_t ref_hook_disasm(void* machine, void* player, void*, void*)
    {
        const RefFn2 orig = ref_tramp(g_ref.a);
        if (t_dis_busy || (!g_ref.enabled && !g_ref.redirect)) return orig(machine, player);
        void* newinv = nullptr;
        void* saved = nullptr;
        void* grp = nullptr;
        {
            RefBusy guard(t_dis_busy);   // plan only; a fault inside the original is handled by ref_call_swapped (a destructor would not run on SEH)
            try
            {
                if (!seh_run([&] {
                        if (g_ref.enabled) ref_probe_disasm(machine, player);
                        if (g_ref.redirect) newinv = ref_plan_refund(machine, player, saved, grp);
                    }))
                    newinv = nullptr;
            }
            catch (...) { newinv = nullptr; ++g_ref.st_err; }
        }
        if (!newinv) return orig(machine, player);
        t_dis_busy = true;   // nested DisassembleSelf calls inside the original pass through unchanged; cleared by ref_call_swapped
        return ref_call_swapped(orig, machine, player, newinv, saved, grp);
    }

    void* ref_hook_charge(void* player, void*, void* cmd, void* ret)
    {
        void* const orig = reinterpret_cast<void*>(ref_tramp(g_ref.b)(player, nullptr));
        if (t_chg_busy || (!g_ref.enabled && !g_ref.redirect)) return orig;
        RefBusy guard(t_chg_busy);
        void* res = nullptr;
        try
        {
            if (!seh_run([&] {
                    if (g_ref.enabled) ref_probe_charge(player, cmd, reinterpret_cast<uint64_t>(ret));
                    if (g_ref.redirect) res = ref_plan_charge(player, cmd, reinterpret_cast<uint64_t>(ret), orig);
                }))
                res = nullptr;
        }
        catch (...) { res = nullptr; ++g_ref.st_err; }
        return res ? res : orig;
    }

    // ---------------- inline hook machinery ----------------

    // WHAT: one executable page within +-1.5 GB of target (so a rel32 jmp reaches it). null when none is free.
    uint8_t* ref_alloc_near(uint8_t* target)
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        const uint64_t gran = si.dwAllocationGranularity, t = reinterpret_cast<uint64_t>(target);
        for (uint64_t d = gran; d < 0x60000000ULL; d += gran)
            for (int sign = 0; sign < 2; ++sign)
            {
                const uint64_t a = ((sign ? t - d : t + d) / gran) * gran;
                void* m = VirtualAlloc(reinterpret_cast<void*>(a), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
                if (m) return static_cast<uint8_t*>(m);
            }
        return nullptr;
    }

    // WHAT: builds the stub (r8 = rdi, r9 = return address, tail jump to hook: the hook replaces the function and calls the original through the
    //       trampoline) and the trampoline (the 7 stolen bytes + jmp [rip+0] to target+7). Does not touch the target.
    bool ref_build(RefHook& h, void* hook, const uint8_t* head)
    {
        if (!h.mem) h.mem = ref_alloc_near(h.target);
        if (!h.mem) return false;
        uint8_t* s = h.mem;
        size_t n = 0;
        auto put = [&](std::initializer_list<uint8_t> b) { for (uint8_t v : b) s[n++] = v; };
        auto put64 = [&](uint64_t v) { memcpy(s + n, &v, 8); n += 8; };
        put({ 0x4C, 0x8B, 0xC7 });                                      // mov r8,rdi
        put({ 0x4C, 0x8B, 0x0C, 0x24 });                                // mov r9,[rsp] (return address of the hooked function)
        put({ 0x48, 0xB8 }); put64(reinterpret_cast<uint64_t>(hook));   // mov rax,hook
        put({ 0xFF, 0xE0 });                                            // jmp rax
        uint8_t* t = s + 0x100;
        memcpy(t, head, 7);
        t[7] = 0xFF; t[8] = 0x25; t[9] = t[10] = t[11] = t[12] = 0;     // jmp [rip+0]
        const uint64_t back = reinterpret_cast<uint64_t>(h.target + 7);
        memcpy(t + 13, &back, 8);
        FlushInstructionCache(GetCurrentProcess(), s, 4096);
        const int64_t rel = reinterpret_cast<int64_t>(s) - reinterpret_cast<int64_t>(h.target + 5);
        return rel > -0x7fff0000LL && rel < 0x7fff0000LL;
    }

    // WHAT: writes `jmp stub` (E9 rel32 + 2 NOP) over the 7 entry bytes, or restores them.
    bool ref_patch(RefHook& h, bool on)
    {
        uint8_t buf[7];
        if (on)
        {
            const int32_t rel = static_cast<int32_t>(h.mem - (h.target + 5));
            buf[0] = 0xE9; memcpy(buf + 1, &rel, 4); buf[5] = buf[6] = 0x90;
        }
        else memcpy(buf, h.orig, 7);
        DWORD old = 0;
        if (!VirtualProtect(h.target, 7, PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy(h.target, buf, 7);
        DWORD tmp = 0;
        VirtualProtect(h.target, 7, old, &tmp);
        FlushInstructionCache(GetCurrentProcess(), h.target, 7);
        h.patched = on;
        return true;
    }

    // WHAT: validation (once, sticky): exe size, patterns, entry bytes, call sites. Fills g_ref targets. false + status on failure.
    bool ref_validate()
    {
        if (g_ref.tried) return g_ref.valid;
        g_ref.tried = true;
        const auto fail = [](const std::string& why) {
            g_ref.valid = false;
            g_ref.status = "COMPAT refund failed: " + why;
            msc_log(widen(g_ref.status));
            return false;
        };
        wchar_t path[MAX_PATH * 2];
        const DWORD pn = GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!pn || pn >= MAX_PATH * 2 || !GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return fail("cannot read exe size");
        const uint64_t size = (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        if (size != REF_EXE_SIZE) return fail("exe size " + std::to_string(size) + " != " + std::to_string(REF_EXE_SIZE));
        const uint8_t* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) return fail("no module base");
        const std::vector<NatRange> ranges = nat_ranges(base);   // msc_natives.hpp: the .text section
        if (ranges.size() != 1) return fail(".text section not found");
        g_ref.base = const_cast<uint8_t*>(base);

        struct Want { const char* name; const char* text; uint8_t** out; };
        uint8_t* ta = nullptr; uint8_t* tb = nullptr;
        const Want wants[2] = { { "disassemble", REF_PAT_DISASM, &ta }, { "charge", REF_PAT_CHARGE, &tb } };
        for (const Want& w : wants)
        {
            std::vector<int16_t> pat;
            if (!pat_parse(w.text, pat)) return fail(std::string(w.name) + " pattern invalid");
            size_t c = 0, f = 0;
            if (!ref_seh_count(ranges[0].p, ranges[0].n, pat.data(), pat.size(), c, f)) return fail(std::string(w.name) + " scan faulted");
            if (c != 1) return fail(std::string(w.name) + " pattern matched " + std::to_string(c) + " times");
            *w.out = const_cast<uint8_t*>(ranges[0].p) + f;
        }
        if (!ref_seh_eq(ta, REF_HEAD_DISASM, 7) || !ref_seh_eq(tb, REF_HEAD_CHARGE, 7)) return fail("entry bytes differ");
        size_t sites = 0;
        if (!ref_seh_sites(ranges[0].p, ranges[0].n, tb, g_ref.sites, REF_SITES, sites)) return fail("call site scan faulted");
        if (sites != REF_SITES) return fail("charge helper call sites = " + std::to_string(sites) + " (need " + std::to_string(REF_SITES) + ")");

        // optional lookup native: failure only disables the lookups (the probe then logs nofind), the hooks stay valid
        std::vector<int16_t> fp;
        uint8_t* fo = const_cast<uint8_t*>(base) + REF_FINDOBJ_RVA;
        if (pat_parse(REF_PAT_FINDOBJ, fp))
        {
            // compare the pattern bytes at the fixed RVA (no wildcards in this pattern)
            std::vector<uint8_t> want;
            for (int16_t v : fp) want.push_back(static_cast<uint8_t>(v));
            if (ref_seh_eq(fo, want.data(), want.size())) g_ref.findobj = fo;
        }
        if (!g_ref.findobj) msc_log(STR("COMPAT refund: FindObject entry bytes differ, lookups off (no redirect)"));
        // InventoryGroup constructor: byte compare (wildcards = rip displacement); failure only turns groups off
        std::vector<int16_t> gp;
        uint8_t* gc = const_cast<uint8_t*>(base) + REF_GCTOR_RVA;
        if (pat_parse(REF_PAT_GCTOR, gp))
        {
            std::vector<uint8_t> have(gp.size());
            bool ok = ref_seh_copy(gc, have.data(), have.size());
            for (size_t i = 0; ok && i < gp.size(); ++i) if (gp[i] >= 0 && have[i] != static_cast<uint8_t>(gp[i])) ok = false;
            if (ok) g_ref.gctor = gc;
        }
        if (!g_ref.gctor) msc_log(STR("COMPAT refund: InventoryGroup constructor bytes differ, groups off (refund swaps to the unit inventory, no charge redirect)"));

        g_ref.a.target = ta;
        g_ref.b.target = tb;
        memcpy(g_ref.a.orig, REF_HEAD_DISASM, 7);
        memcpy(g_ref.b.orig, REF_HEAD_CHARGE, 7);
        g_ref.valid = true;
        char buf[256];
        snprintf(buf, sizeof buf, "COMPAT refund validated disassemble=0x%llx charge=0x%llx sites=%zu findobj=%s groups=%s",
                 static_cast<unsigned long long>(ta - base), static_cast<unsigned long long>(tb - base), sites, g_ref.findobj ? "ok" : "off", g_ref.gctor ? "ok" : "off");
        g_ref.status = buf;
        msc_log(widen(g_ref.status));
        return true;
    }

    // WHAT: installs both hooks (after validation). All or nothing: a failure on the second restores the first. Behaviour is chosen by the flags.
    bool ref_install()
    {
        if (!ref_validate()) return false;
        if (g_ref.a.patched && g_ref.b.patched) return true;
        if (!ref_build(g_ref.a, reinterpret_cast<void*>(&ref_hook_disasm), REF_HEAD_DISASM) ||
            !ref_build(g_ref.b, reinterpret_cast<void*>(&ref_hook_charge), REF_HEAD_CHARGE))
        { g_ref.status = "COMPAT refund failed: no executable memory near the module"; msc_log(widen(g_ref.status)); return false; }
        if (!ref_patch(g_ref.a, true)) { g_ref.status = "COMPAT refund failed: patch a"; msc_log(widen(g_ref.status)); return false; }
        if (!ref_patch(g_ref.b, true))
        {
            ref_patch(g_ref.a, false);
            g_ref.status = "COMPAT refund failed: patch b";
            msc_log(widen(g_ref.status));
            return false;
        }
        return true;
    }

    void ref_unpatch()
    {
        if (g_ref.a.patched) ref_patch(g_ref.a, false);
        if (g_ref.b.patched) ref_patch(g_ref.b, false);
    }

    // WHAT: restores both entries. Stub memory stays (a thread may still be inside); the hooks call the original once both flags are false.
    void ref_uninstall()
    {
        g_ref.enabled = false;
        g_ref.redirect = false;
        ref_unpatch();
        if (g_ref.valid) g_ref.status = "off";
    }

    // WHAT: after a flag change: unpatch when nothing is on, refresh the status text.
    void ref_refresh()
    {
        if (!g_ref.valid) return;
        if (!g_ref.enabled && !g_ref.redirect) { ref_unpatch(); g_ref.status = "off"; return; }
        g_ref.status = std::string("on: redirect=") + (g_ref.redirect ? "on" : "off") + " probe_log=" + (g_ref.enabled ? "on" : "off") +
                       " groups=" + (g_ref.gctor && g_ref.group_fail_n < 3 ? "yes" : "no");
    }

    // WHAT: probe log switch (MSCoreNative_refund_enable). Returns the status string.
    std::string ref_set(bool on)
    {
        if (on) { if (!ref_install()) return g_ref.status; g_ref.enabled = true; }
        else g_ref.enabled = false;
        ref_refresh();
        return g_ref.status;
    }

    // WHAT: redirect switch (MSCoreNative_redirect_enable; default on at start). Returns the status string.
    std::string ref_set_redirect(bool on)
    {
        if (on)
        {
            if (!ref_install()) return g_ref.status;
            g_ref.redirect = true;
            ref_refresh();
            msc_log(widen(std::string("REDIRECT on (groups=") + (g_ref.gctor ? "yes" : "no") + ")"));
        }
        else { g_ref.redirect = false; ref_refresh(); }
        return g_ref.status;
    }

    std::string ref_stats_text()
    {
        return "refund=" + std::to_string(g_ref.st_refund.load()) + " refund_fallback=" + std::to_string(g_ref.st_refund_fb.load()) +
               " charge=" + std::to_string(g_ref.st_charge.load()) + " skipped=" + std::to_string(g_ref.st_skip.load()) +
               " group_made=" + std::to_string(g_ref.st_gmade.load()) + " group_fail=" + std::to_string(g_ref.st_gfail.load()) +
               " errors=" + std::to_string(g_ref.st_err.load());
    }

    std::string ref_log_text()
    {
        std::lock_guard<std::mutex> g(g_ref.mu);
        std::string out;
        for (const std::string& l : g_ref.ring) out += l + "\n";
        return out;
    }
}
