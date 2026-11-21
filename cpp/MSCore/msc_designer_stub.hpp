// FILE: cpp/MSCore/msc_designer_stub.hpp
// WHAT: pure builder of the machine-code stub for the Designer IsBlockAvailable guard (see msc_designer.hpp). No game or Windows API, so
//       tests\test_designer_stub.cpp runs it against a fake function.
// STUB (x64, Win64 ABI, rcx = this, rdx = const TSubclassOf<BuildingBlock>*):
//       if (rdx != 0 && *(uint64_t*)rdx == 0) { ++counter; return false; }  else run the 10 stolen entry bytes and jump to the rest of the function.
//       Layout: +0 test rdx,rdx / je orig / cmp [rdx],0 / jne orig / lock inc [counter] / xor eax,eax / ret / +21 orig: stolen 10 bytes / jmp [rip+0] back.
//       Counter (uint32) at +64. Uses no stack and no register other than rax (volatile) before the early return.
// PATCH (dg_patch8): the 10 stolen bytes are two whole instructions, but only the first 8 bytes of the target are ever written, in ONE
//       interlocked 8-byte compare-exchange: `E9 rel32` + the ORIGINAL bytes 5..7. Bytes 8..9 are never touched and bytes 5..9 stay the
//       unchanged second instruction, so a thread that is already executing at target+5 keeps decoding valid original code (no NOP
//       padding needed, no window with a half written instruction). The restore is one interlocked 8-byte store of the original 8 bytes.
// DEPENDS ON: standard library only (plus the compiler intrinsic for the 8-byte compare-exchange).
// USED BY: msc_designer.hpp, tests\test_designer_stub.cpp.
#pragma once

#include <cstdint>
#include <cstring>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace
{
    constexpr size_t DG_STOLEN = 10;      // two whole instructions: mov [rsp+10h],rbx (5) + mov [rsp+18h],rbp (5), no rip-relative operand
    constexpr size_t DG_COUNTER_OFF = 64;
    constexpr size_t DG_STUB_SIZE = 80;
    constexpr size_t DG_PATCH = 8;        // bytes really written (one 8-byte interlocked compare-exchange)
    constexpr size_t DG_ORIG_OFF = 21;    // stolen bytes inside the stub

    // WHAT: writes the stub into mem (>= DG_STUB_SIZE bytes). stolen = the first DG_STOLEN bytes of the guarded function, back = address of the
    //       function + DG_STOLEN. Counter is zeroed.
    [[maybe_unused]] void dg_build_stub(uint8_t* mem, const uint8_t* stolen, uint64_t back)
    {
        memset(mem, 0xCC, DG_STUB_SIZE);
        const uint8_t head[DG_ORIG_OFF] = {
            0x48, 0x85, 0xD2,                    // 0  test rdx,rdx
            0x74, 0x10,                          // 3  je orig (+21)
            0x48, 0x83, 0x3A, 0x00,              // 5  cmp qword ptr [rdx],0
            0x75, 0x0A,                          // 9  jne orig (+21)
            0xF0, 0xFF, 0x05, 0, 0, 0, 0,        // 11 lock inc dword ptr [rip+disp] (disp below)
            0x31, 0xC0,                          // 18 xor eax,eax
            0xC3 };                              // 20 ret
        memcpy(mem, head, sizeof head);
        const int32_t disp = static_cast<int32_t>(DG_COUNTER_OFF) - 18;   // rip after the lock inc = +18
        memcpy(mem + 14, &disp, 4);
        memcpy(mem + DG_ORIG_OFF, stolen, DG_STOLEN);
        const uint8_t jmp[6] = { 0xFF, 0x25, 0, 0, 0, 0 };                // jmp [rip+0] -> the qword that follows
        memcpy(mem + 31, jmp, sizeof jmp);
        memcpy(mem + 37, &back, 8);
        memset(mem + DG_COUNTER_OFF, 0, 4);
    }

    // WHAT: bytes written over the target's first DG_PATCH bytes: `E9 rel32` + orig[5..7]. rel = stub - (target + 5).
    [[maybe_unused]] void dg_patch_bytes(const uint8_t* orig, int32_t rel, uint8_t out[DG_PATCH])
    {
        memcpy(out, orig, DG_PATCH);
        out[0] = 0xE9;
        memcpy(out + 1, &rel, 4);
    }

    // WHAT: atomically replaces the 8 bytes at p (any alignment; a locked op is atomic even across a cache line) when they equal expect.
    //       true = replaced. Needs p writable.
    [[maybe_unused]] bool dg_cas8(uint8_t* p, const uint8_t* expect, const uint8_t* want)
    {
        uint64_t e, w;
        memcpy(&e, expect, 8);
        memcpy(&w, want, 8);
#ifdef _MSC_VER
        return static_cast<uint64_t>(_InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(p), static_cast<long long>(w), static_cast<long long>(e))) == e;
#else
        return __sync_val_compare_and_swap(reinterpret_cast<volatile uint64_t*>(p), e, w) == e;
#endif
    }
}
