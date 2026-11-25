// FILE: tests/test_threat_logic.cpp
// WHAT: native unit test for cpp/MSCore/msc_threat_logic.hpp (entry classification and counting). Pure C++, no game headers.
#include <cstdio>
#include "msc_threat_logic.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

int main()
{
    CHECK(thr_kind(true, false, true, 100, 500) == THR_HOSTILE);
    CHECK(thr_kind(true, false, true, 500, 500) == THR_HOSTILE);   // edge counts near
    CHECK(thr_kind(true, false, true, 501, 500) == THR_FAR);
    CHECK(thr_kind(true, false, true, -1, 500) == THR_HOSTILE);    // unreadable distance: conservative
    CHECK(thr_kind(true, true, true, 100, 500) == THR_NONE);       // same owner/faction
    CHECK(thr_kind(false, false, true, -1, 500) == THR_FAR);       // unresolved
    CHECK(thr_kind(true, false, false, 100, 500) == THR_NONE);     // not an object array

    ThrCount c;
    CHECK(c.hostile == 0 && c.far_n == 0 && c.nearest_m < 0);
    thr_add(c, THR_NONE, 5);
    CHECK(c.hostile == 0 && c.far_n == 0);
    thr_add(c, THR_HOSTILE, 300);
    thr_add(c, THR_HOSTILE, 120);
    thr_add(c, THR_HOSTILE, -1);
    thr_add(c, THR_FAR, 900);
    CHECK(c.hostile == 3 && c.far_n == 1 && c.nearest_m == 120);   // far and unreadable do not set nearest

    std::printf("test_threat_logic: %d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
