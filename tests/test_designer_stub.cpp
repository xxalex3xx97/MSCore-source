// FILE: tests/test_designer_stub.cpp
// WHAT: runs the Designer IsBlockAvailable guard stub (cpp/MSCore/msc_designer_stub.hpp) against a fake function in executable memory.
//       Also applies and reverts the 8-byte interlocked patch (dg_patch_bytes/dg_cas8) 1000 times on the fake function and checks the bytes,
//       including a straddling-cache-line address. Cases: rdx null -> original runs; [rdx] null -> returns 0 and counts; [rdx] non-null -> original runs. Windows x64 only (SKIP elsewhere).
// RUN: scripts\test-native.ps1. Exit 1 on any failed CHECK.
#include <cstdio>
#include "msc_designer_stub.hpp"
#if defined(_WIN64)
#include <Windows.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    uint8_t* mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    CHECK(mem != nullptr);
    if (!mem) return 1;
    uint8_t* fake = mem + 512;   // the "original": the 10 stolen bytes, then mov eax,7 / ret
    const uint8_t body[16] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0xB8, 0x07, 0, 0, 0, 0xC3 };
    memcpy(fake, body, sizeof body);
    dg_build_stub(mem, fake, reinterpret_cast<uint64_t>(fake) + DG_STOLEN);
    using Fn = int (*)(void*, void*);
    const Fn stub = reinterpret_cast<Fn>(mem);
    volatile uint32_t* counter = reinterpret_cast<volatile uint32_t*>(mem + DG_COUNTER_OFF);
    uint64_t nullClass = 0, someClass = 0x1234;
    CHECK(stub(nullptr, nullptr) == 7);            // rdx null: untouched path
    CHECK(*counter == 0);
    CHECK(stub(nullptr, &nullClass) == 0);         // null TSubclassOf: guarded
    CHECK(*counter == 1);
    CHECK(stub(nullptr, &someClass) == 7);         // valid class: original
    CHECK(stub(nullptr, &nullClass) == 0);
    CHECK(*counter == 2);
    // patch / revert cycles on the fake function (and one copy that straddles a 64 byte line)
    for (int variant = 0; variant < 2; ++variant)
    {
        uint8_t* fn = variant ? mem + 1024 + 60 : fake;           // 1024+60: addr%64 == 60 > 56
        if (variant) memcpy(fn, body, sizeof body);
        uint8_t orig[DG_STOLEN];
        memcpy(orig, fn, sizeof orig);
        if (variant) dg_build_stub(mem, fn, reinterpret_cast<uint64_t>(fn) + DG_STOLEN);
        const int32_t rel = static_cast<int32_t>(mem - (fn + 5));
        uint8_t jmp8[DG_PATCH];
        dg_patch_bytes(orig, rel, jmp8);
        CHECK(jmp8[0] == 0xE9 && memcmp(jmp8 + 5, orig + 5, 3) == 0);
        const Fn f = reinterpret_cast<Fn>(fn);
        for (int i = 0; i < 1000; ++i)
        {
            CHECK(dg_cas8(fn, orig, jmp8));
            CHECK(memcmp(fn, jmp8, DG_PATCH) == 0 && memcmp(fn + 8, body + 8, sizeof body - 8) == 0);   // bytes 8.. untouched
            CHECK(!dg_cas8(fn, orig, jmp8));                       // already patched: expect mismatch, no change
            if (i == 0)
            {
                const uint32_t c0 = *counter;
                CHECK(f(nullptr, &nullClass) == 0 && *counter == c0 + 1);   // runs through the patch into the stub
                CHECK(f(nullptr, &someClass) == 7);
            }
            CHECK(dg_cas8(fn, jmp8, orig));
            CHECK(memcmp(fn, body, sizeof body) == 0);
            CHECK(!dg_cas8(fn, jmp8, orig));
        }
    }
    std::printf(g_fail ? "FAILED\n" : "test_designer_stub OK\n");
    return g_fail ? 1 : 0;
}
#else
int main() { std::printf("test_designer_stub SKIP (not win64)\n"); return 0; }
#endif
