// FILE: tests/test_wclass.cpp
// WHAT: native unit test for weapon_class_bits (cpp/MSCore/msc_wclass.hpp), the class-name rules behind MSCore_ship_caps. Pure C++, no game headers.
// RUN: scripts\test-native.ps1 (cl on Windows, g++ fallback). Exit 1 on any failed CHECK.
#include <cstdio>
#include "msc_wclass.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

// one class name -> expected bits
static void cn(const wchar_t* name, int want)
{
    CHECK(weapon_class_bits(name) == want);
}

int main()
{
    cn(L"BPO_MiningLaser_C", 1);
    cn(L"BPO_ItemScooper", 2);
    cn(L"BPO_BasicCannon", 4);
    cn(L"BPO_BasicLance", 4);
    cn(L"BPO_MissileLauncher_01", 4);
    cn(L"BPO_LaserTurret", 8);
    cn(L"BPO_StationTurret_01", 8);
    cn(L"BP_CannonTurret", 8);
    cn(L"BPO_TractorBeam", 16);
    cn(L"BPO_Laser", 0);
    cn(L"", 0);

    std::printf("test_wclass: %d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
