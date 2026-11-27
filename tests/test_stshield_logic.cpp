// FILE: tests/test_stshield_logic.cpp
// WHAT: native unit test for cpp/MSCore/msc_stshield_logic.hpp (station zone row filter). Pure C++, no game headers.
#include <cstdio>
#include <limits>
#include "msc_stshield_logic.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static MsStationZone row(int id, int sys, double x, double ext, double rng)
{
    MsStationZone r{};
    r.id = id; r.sys = sys; r.pos[0] = x; r.pos[1] = 0; r.pos[2] = 0; r.extent_m = ext; r.range_m = rng;
    return r;
}

int main()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<MsStationZone> v = { row(9, 1, 5, 100, -1), row(3, 1, 1, -1, 700), row(0, 1, 2, 1, 1), row(4, 0, 2, 1, 1), row(5, 1, nan, 1, 1), row(6, 2, 7, 0, nan) };
    const std::vector<MsStationZone> f = sz_filter(v, 10);
    CHECK(f.size() == 3);                                   // id 0, sys 0 and NaN pos dropped
    CHECK(f[0].id == 3 && f[1].id == 6 && f[2].id == 9);    // ordered by id
    CHECK(f[0].extent_m == -1 && f[0].range_m == 700);
    CHECK(f[1].extent_m == -1 && f[1].range_m == -1);       // 0 / NaN become unknown
    CHECK(f[2].extent_m == 100 && f[2].range_m == -1);
    CHECK(sz_filter(v, 2).size() == 2);
    CHECK(sz_filter(v, 0).empty());
    CHECK(sz_filter({}, 10).empty());
    CHECK(sz_is_player_station(L"BP_StationObject_C"));
    CHECK(!sz_is_player_station(L"BP_AbandonedStationObject_01_C"));
    CHECK(!sz_is_player_station(L"BPO_TradingOutpost_C"));
    CHECK(!sz_is_player_station(L""));
    std::printf("test_stshield_logic: %d passed, %d failed\n", g_run - g_fail, g_fail);
    return g_fail ? 1 : 0;
}
