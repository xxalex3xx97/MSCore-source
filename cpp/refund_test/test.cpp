// FILE: cpp/refund_test/test.cpp
// WHAT: fake-function harness for msc_refund.hpp (run by scripts\test-refund.ps1). Builds REAL x64 machine code for a fake DisassembleSelf and a fake
//       charge helper with the exact 7 entry bytes of the game functions, installs the real hooks (stub + trampoline + patch) over them and checks
//       the group swap / restore, the charge redirect, the fall-backs and the fail-closed paths on fake game memory.
//       It does NOT prove anything about the real game: object layouts are the offsets taken from the REA notes, the group is a fake.
#include <cstdlib>
#include "msc_refund.hpp"

namespace
{
    int g_fail = 0;
#define CHECK(c) do { if (!(c)) { ++g_fail; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

    UClass c_station{ L"BP_StationObject_C" }, c_unit{ L"BP_BPUnitStoreObj_01_C" }, c_inv{ L"Inventory" }, c_group{ L"InventoryGroup" },
        c_mach{ L"BP_Smelter_C" }, c_bpu{ L"BP_BPUnitOther_C" }, c_other{ L"Other_C" }, c_player{ L"Player" };
    uint8_t* g_base = reinterpret_cast<uint8_t*>(0x10000000);   // fake image base, only used for RVA arithmetic
    std::vector<std::pair<int32_t, void*>> g_objs;

    uint8_t* mk(UClass* c, int32_t id)
    {
        uint8_t* o = static_cast<uint8_t*>(_aligned_malloc(0x600, 16));
        memset(o, 0, 0x600);
        reinterpret_cast<UObject*>(o)->cls = c;
        *reinterpret_cast<int32_t*>(o + 0x30) = id;
        g_objs.push_back({ id, o });
        return o;
    }
    void* fake_find(void*, uint32_t id)
    {
        for (auto& p : g_objs) if (p.first == static_cast<int32_t>(id)) return p.second;
        return nullptr;
    }
    template <typename T> void put(uint8_t* o, size_t off, T v) { memcpy(o + off, &v, sizeof v); }
    template <typename T> T get(uint8_t* o, size_t off) { T v; memcpy(&v, o + off, sizeof v); return v; }

    void* fake_gctor(void* a, void* b)
    {
        uint8_t* g = mk(&c_group, 9000 + static_cast<int>(g_objs.size()));
        put<uint64_t>(g, 0x68, reinterpret_cast<uint64_t>(g_base) + REF_GROUP_VT_RVA);
        void** d = static_cast<void**>(calloc(4, sizeof(void*)));
        d[0] = a; d[1] = b;
        put<void*>(g, 0x70, d); put<int32_t>(g, 0x78, 2); put<int32_t>(g, 0x7c, 4);
        return g;
    }

    // what the fake DisassembleSelf saw in [player+0x288]
    void* g_seen = nullptr; void* g_m0 = nullptr; void* g_m1 = nullptr; bool g_fault = false;
    uint64_t seen_cb(void* slot)
    {
        g_seen = slot; g_m0 = g_m1 = nullptr;
        if (slot && get<uint64_t>(static_cast<uint8_t*>(slot), 0x68) == reinterpret_cast<uint64_t>(g_base) + REF_GROUP_VT_RVA)
        {
            void** d = get<void**>(static_cast<uint8_t*>(slot), 0x70);
            g_m0 = d[0]; g_m1 = d[1];
        }
        if (g_fault) *static_cast<volatile int*>(nullptr) = 1;
        return reinterpret_cast<uint64_t>(slot);
    }

    uint8_t* code(std::initializer_list<uint8_t> b)
    {
        uint8_t* m = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        size_t n = 0;
        for (uint8_t v : b) m[n++] = v;
        return m;
    }
    void put64(uint8_t* m, size_t at, uint64_t v) { memcpy(m + at, &v, 8); }

    using F2 = uint64_t (*)(void*, void*);
    F2 g_dis; F2 g_thunk1; F2 g_thunk2;
    uint8_t *g_dis_code, *g_chg_code;

    void build_fakes()
    {
        // fake DisassembleSelf: 7 head bytes, mov rcx,[rdx+0x288]; call seen_cb(rcx); ret
        g_dis_code = code({ 0x48, 0x8B, 0xC4, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x8B, 0x8A, 0x88, 0x02, 0x00, 0x00, 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
                            0x48, 0x83, 0xEC, 0x20, 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20, 0x5D, 0xC3 });
        put64(g_dis_code, 16, reinterpret_cast<uint64_t>(&seen_cb));
        // unwind info so a fault inside the callback can unwind through this fake frame (rbp frame, prolog = 7 bytes)
        const uint8_t unwind[] = { 0x01, 0x07, 0x02, 0x05, 0x07, 0x03, 0x04, 0x50 };   // v1, prolog 7, 2 codes, frame reg rbp; SET_FPREG@7, PUSH rbp@4
        memcpy(g_dis_code + 0x200, unwind, sizeof unwind);
        static RUNTIME_FUNCTION rf;
        rf.BeginAddress = 0; rf.EndAddress = 0x100; rf.UnwindData = 0x200;
        RtlAddFunctionTable(&rf, 1, reinterpret_cast<DWORD64>(g_dis_code));
        // fake charge helper: mov rax,[rcx+0x240]; ret
        g_chg_code = code({ 0x48, 0x8B, 0x81, 0x40, 0x02, 0x00, 0x00, 0xC3 });
        // thunks (rcx = player, rdx = cmd): push rdi; sub rsp,0x20; mov rdi,rdx; mov rax,helper; call rax; add rsp,0x20; pop rdi; ret
        for (int i = 0; i < 2; ++i)
        {
            uint8_t* t = code({ 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xD7, 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3 });
            put64(t, 10, reinterpret_cast<uint64_t>(g_chg_code));
            (i ? g_thunk2 : g_thunk1) = reinterpret_cast<F2>(t);
            if (!i) g_ref.sites[0] = reinterpret_cast<uint64_t>(t) + 20;   // return address after `call rax`
        }
    }

    bool call_faulting(void* m, void* p)
    {
        __try { g_dis(m, p); return false; } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }

    void setup_ref(bool with_groups)
    {
        g_ref.base = g_base;
        g_ref.findobj = reinterpret_cast<uint8_t*>(&fake_find);
        g_ref.gctor = with_groups ? reinterpret_cast<uint8_t*>(&fake_gctor) : nullptr;
        g_ref.grp_refund = g_ref.grp_charge = nullptr;
        g_ref.group_fail_n = 0;
        g_ref.tried = g_ref.valid = true;
    }
}

int main()
{
    build_fakes();
    uint8_t* S = mk(&c_other, 1);
    uint8_t* F = mk(&c_station, 1000);
    uint8_t* UINV = mk(&c_inv, 2001);
    uint8_t* C = mk(&c_unit, 2000);
    put<void*>(C, 0x1b0, UINV);
    uint8_t* M = mk(&c_mach, 3000);
    uint8_t* MB = mk(&c_bpu, 3001);
    uint8_t* PINV = mk(&c_inv, 2312);
    uint8_t* SHIP = mk(&c_inv, 2500);
    uint8_t* P = mk(&c_player, 7);
    uint8_t* child = mk(&c_other, 1500);
    void** list = static_cast<void**>(calloc(8, sizeof(void*)));
    list[0] = M; list[1] = C; list[2] = MB;
    put<void*>(F, 0x368, list); put<int32_t>(F, 0x370, 3); put<int32_t>(F, 0x374, 8);
    for (uint8_t* m : { M, MB, C }) { put<void*>(m, 0x28, S); put<int32_t>(m, 0x34, 1000); }
    put<void*>(child, 0x28, S); put<int32_t>(child, 0x34, 1000);
    put<void*>(P, 0x28, S); put<void*>(P, 0x288, PINV); put<void*>(P, 0x240, SHIP);
    // a second station without a unit, and a non-station frame
    uint8_t* F2s = mk(&c_station, 1100);
    uint8_t* M2 = mk(&c_mach, 3100);
    void** l2 = static_cast<void**>(calloc(8, sizeof(void*))); l2[0] = M2;
    put<void*>(F2s, 0x368, l2); put<int32_t>(F2s, 0x370, 1); put<int32_t>(F2s, 0x374, 8);
    put<void*>(M2, 0x28, S); put<int32_t>(M2, 0x34, 1100);
    uint8_t* NF = mk(&c_other, 1200);
    uint8_t* M3 = mk(&c_mach, 3200);
    put<void*>(M3, 0x28, S); put<int32_t>(M3, 0x34, 1200);

    setup_ref(true);
    g_ref.a.target = g_dis_code; g_ref.b.target = g_chg_code;
    memcpy(g_ref.a.orig, REF_HEAD_DISASM, 7); memcpy(g_ref.b.orig, REF_HEAD_CHARGE, 7);
    g_dis = reinterpret_cast<F2>(g_dis_code);
    uint8_t* cmd = mk(&c_other, 8000);
    const auto charge = [&](F2 t, int32_t attach) { put<int32_t>(cmd, 0x34, attach); return reinterpret_cast<uint8_t*>(t(P, cmd)); };

    // A. before install: originals only
    CHECK(g_dis(M, P) == reinterpret_cast<uint64_t>(PINV) && g_seen == PINV);
    CHECK(charge(g_thunk1, 1000) == SHIP);

    // B. install, redirect on
    ref_set_redirect(true);
    CHECK(g_ref.a.patched && g_ref.b.patched && g_ref.redirect);
    CHECK(g_ref.status.rfind("on: redirect=on", 0) == 0);

    // 1. refund: group seen during the original, members [unit, player], slot + members restored after
    uint64_t r = g_dis(M, P);
    CHECK(r == reinterpret_cast<uint64_t>(g_seen));            // return value of the original passes through
    CHECK(g_seen == g_ref.grp_refund && g_seen != nullptr);
    CHECK(g_m0 == UINV && g_m1 == PINV);
    CHECK(get<void*>(P, 0x288) == PINV);
    { void** d = get<void**>(static_cast<uint8_t*>(g_ref.grp_refund), 0x70); CHECK(!d[0] && !d[1]); }
    CHECK(reinterpret_cast<UObject*>(g_ref.grp_refund)->root == 1);
    CHECK(g_ref.st_refund == 1);
    CHECK(ref_log_text().find("REDIRECT refund machine=3000 unit_inv=2001 mode=group") != std::string::npos);
    void* old_grp = g_ref.grp_refund;
    g_dis(M, P);
    CHECK(g_ref.grp_refund == old_grp && g_ref.st_gmade == 1);       // persistent, not recreated

    // 2. charge: group [unit, ship]; separate object; child attach walks up; same object on the next call
    uint8_t* cg = charge(g_thunk1, 1000);
    CHECK(cg == g_ref.grp_charge && cg != nullptr && cg != g_ref.grp_refund);
    { void** d = get<void**>(cg, 0x70); CHECK(d[0] == UINV && d[1] == SHIP); }
    CHECK(charge(g_thunk1, 1500) == cg);
    CHECK(g_ref.st_charge == 2);
    CHECK(ref_log_text().find("REDIRECT charge site=0 station=1000 unit_inv=2001") != std::string::npos);

    // 3. charge: no redirect cases return the original inventory
    CHECK(charge(g_thunk1, 0) == SHIP);              // AttachTargetID 0
    CHECK(charge(g_thunk1, 777) == SHIP);            // unresolved
    CHECK(charge(g_thunk1, 1100) == SHIP);           // station without unit
    CHECK(charge(g_thunk1, 1200) == SHIP);           // not a station, no parent
    CHECK(charge(g_thunk2, 1000) == SHIP);           // not a whitelisted call site
    put<uint8_t>(P, 0x370, 1); CHECK(charge(g_thunk1, 1000) == SHIP); put<uint8_t>(P, 0x370, 0);   // creative sender
    put<void*>(P, 0x240, nullptr);                   // orig inventory null -> original result (null) passes through
    CHECK(charge(g_thunk1, 1000) == nullptr);
    put<void*>(P, 0x240, SHIP);

    // 4. refund: no redirect cases leave the slot alone
    g_seen = nullptr; g_dis(MB, P); CHECK(g_seen == PINV);     // BPU class machine
    g_seen = nullptr; g_dis(C, P);  CHECK(g_seen == PINV);     // the unit container itself
    g_seen = nullptr; g_dis(M2, P); CHECK(g_seen == PINV);     // station without unit
    g_seen = nullptr; g_dis(M3, P); CHECK(g_seen == PINV);     // frame is not a station
    put<void*>(P, 0x288, nullptr); g_seen = reinterpret_cast<void*>(1); g_dis(M, P); CHECK(g_seen == nullptr); put<void*>(P, 0x288, PINV);   // no player inventory

    // 5. a fault inside the original: slot and members restored, exception continues to the caller
    g_fault = true;
    const bool caught = call_faulting(M, P);
    g_fault = false;
    CHECK(caught && get<void*>(P, 0x288) == PINV);
    { void** d = get<void**>(static_cast<uint8_t*>(g_ref.grp_refund), 0x70); CHECK(!d[0] && !d[1]); }
    CHECK(!t_dis_busy);

    // 6. a damaged persistent group is recreated
    put<uint64_t>(static_cast<uint8_t*>(g_ref.grp_refund), 0x68, 0);
    g_dis(M, P);
    CHECK(g_ref.grp_refund != old_grp && g_seen == g_ref.grp_refund && g_ref.st_gmade == 3);

    // 7. probe log on: PROBE lines appear, redirect keeps working
    ref_set(true);
    g_dis(M, P); charge(g_thunk1, 1000);
    CHECK(g_seen == g_ref.grp_refund);
    const std::string lg = ref_log_text();
    CHECK(lg.find("PROBE refund machine=3000") != std::string::npos && lg.find("PROBE charge site=0") != std::string::npos);
    ref_set(false);

    // 8. no group constructor: refund swaps to the unit inventory itself, charge untouched
    setup_ref(false);
    g_seen = nullptr; g_dis(M, P);
    CHECK(g_seen == UINV && get<void*>(P, 0x288) == PINV && g_ref.st_refund_fb == 1);
    CHECK(charge(g_thunk1, 1000) == SHIP);
    CHECK(ref_log_text().find("mode=fallback") != std::string::npos);

    // 9. rate limit helper
    CHECK(ref_log_due(1) && ref_log_due(20) && !ref_log_due(21) && !ref_log_due(24) && ref_log_due(25) && ref_log_due(50));
    CHECK(ref_stats_text().find("refund=") == 0);

    // 10. redirect off: unpatched again, originals restored byte for byte
    ref_set_redirect(false);
    CHECK(!g_ref.a.patched && !g_ref.b.patched && g_ref.status == "off");
    CHECK(memcmp(g_dis_code, REF_HEAD_DISASM, 7) == 0 && memcmp(g_chg_code, REF_HEAD_CHARGE, 7) == 0);
    g_seen = nullptr; g_dis(M, P); CHECK(g_seen == PINV);

    printf(g_fail ? "refund harness: %d FAILED\n" : "refund harness: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
