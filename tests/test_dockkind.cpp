// FILE: tests/test_dockkind.cpp
// WHAT: native unit test for dock_kind_from_class (cpp/MSCore/msc_dockkind.hpp), the class-name rule behind MSCore_dock_kind. Pure C++, no game headers.
// RUN: scripts\test-native.ps1 (cl on Windows, g++ fallback). Exit 1 on any failed CHECK.
#include <cstdio>
#include "msc_dockkind.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

// one class name -> expected kind
static void cn(const wchar_t* name, int want)
{
    CHECK(dock_kind_from_class(name) == want);
}

int main()
{
    cn(L"BPO_IndustrialDock_C", 1);
    cn(L"BP_IndustrialDock_C", 1);
    cn(L"BPO_StationBlockDock_C", 0);
    cn(L"StationBlockDock", 0);
    cn(L"Foo", -1);
    cn(L"", -1);

    std::printf("test_dockkind: %d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
