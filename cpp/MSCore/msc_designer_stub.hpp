// FILE: cpp/MSCore/msc_designer_stub.hpp
// WHAT: pure builder of the machine-code stub for the Designer IsBlockAvailable guard (see msc_designer.hpp). No game or Windows API, so
//       tests\test_designer_stub.cpp runs it against a fake function.
// STUB (x64, Win64 ABI, rcx = this, rdx = const TSubclassOf<BuildingBlock>*):
//       if (rdx != 0 && *(uint64_t*)rdx == 0) { ++counter; return false; }  else run the 10 stolen entry bytes and jump to the rest of the function.
//       Layout: +0 test rdx,rdx / je orig / cmp [rdx],0 / jne orig / inc [counter] / xor eax,eax / ret / +20 orig: stolen 10 bytes / jmp [rip+0] back.
//       Counter (uint32) at +64. Uses no stack and no register other than rax (volatile) before the early return.
// DEPENDS ON: standard library only.
// USED BY: msc_designer.hpp, tests\test_designer_stub.cpp.
#pragma once

#include <cstdint>
#include <cstring>

namespace
{
    constexpr size_t DG_STOLEN = 10;      // two whole instructions: mov [rsp+10h],rbx (5) + mov [rsp+18h],rbp (5), no rip-relative operand
    constexpr size_t DG_COUNTER_OFF = 64;
    constexpr size_t DG_STUB_SIZE = 80;

    // WHAT: writes the stub into mem (>= DG_STUB_SIZE bytes). stolen = the first DG_STOLEN bytes of the guarded function, back = address of the
    //       function + DG_STOLEN. Counter is zeroed.
    void dg_build_stub(uint8_t* mem, const uint8_t* stolen, uint64_t back)
    {
        memset(mem, 0xCC, DG_STUB_SIZE);
        const uint8_t head[20] = {
            0x48, 0x85, 0xD2,                    // 0  test rdx,rdx
            0x74, 0x0F,                          // 3  je orig (+20)
            0x48, 0x83, 0x3A, 0x00,              // 5  cmp qword ptr [rdx],0
            0x75, 0x09,                          // 9  jne orig (+20)
            0xFF, 0x05, 0, 0, 0, 0,              // 11 inc dword ptr [rip+disp] (disp below)
            0x31, 0xC0,                          // 17 xor eax,eax
            0xC3 };                              // 19 ret
        memcpy(mem, head, sizeof head);
        const int32_t disp = static_cast<int32_t>(DG_COUNTER_OFF) - 17;   // rip after the inc = +17
        memcpy(mem + 13, &disp, 4);
        memcpy(mem + 20, stolen, DG_STOLEN);
        const uint8_t jmp[6] = { 0xFF, 0x25, 0, 0, 0, 0 };                // jmp [rip+0] -> the qword that follows
        memcpy(mem + 30, jmp, sizeof jmp);
        memcpy(mem + 36, &back, 8);
        memset(mem + DG_COUNTER_OFF, 0, 4);
    }
}
