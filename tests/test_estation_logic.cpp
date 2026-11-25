// FILE: tests/test_estation_logic.cpp
// WHAT: native unit test for cpp/MSCore/msc_estation_logic.hpp (row flags, build percent, filter). Pure C++, no game headers.
#include <cstdio>
#include "msc_estation_logic.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static MsEnemyStation row(int id, double d, int hull, int flags)
{
    MsEnemyStation r{};
    r.id = id; r.sys = 1; r.hull = hull; r.max_hull = 20000; r.flags = flags; r.build_pct = 100; r.d_m = d; r.radius_m = -1;
    return r;
}

int main()
{
    CHECK(est_flags(true, false, false) == 1); CHECK(est_flags(false, true, false) == 2); CHECK(est_flags(true, false, true) == 5);
    CHECK(est_build_pct(50, 200) == 25); CHECK(est_build_pct(200, 200) == 100); CHECK(est_build_pct(300, 200) == 100);
    CHECK(est_build_pct(-1, 200) == -1); CHECK(est_build_pct(5, 0) == -1);
    CHECK(est_hull_value(true, 5000) == 5000); CHECK(est_hull_value(false, 5000) == -1); CHECK(est_hull_value(true, 9999999999LL) == 2147483647); CHECK(est_hull_value(true, -3) == 0);

    std::vector<MsEnemyStation> v = { row(7, 900, 100, 1), row(3, 300, 100, 1), row(5, 300, 100, 1), row(9, 100, 0, 0), row(11, 200, -1, 2), row(13, 5000, 100, 1) };
    const std::vector<MsEnemyStation> f = est_filter(v, 1000.0, 10);
    CHECK(f.size() == 4);                       // 9 dead dropped, 13 beyond radius
    CHECK(f[0].id == 11);                        // nearest, hull unreadable is kept (fail closed in the consumer)
    CHECK(f[1].id == 3 && f[2].id == 5);         // tie on distance: lowest id first
    CHECK(f[3].id == 7);
    CHECK(est_filter(v, 1000.0, 2).size() == 2);
    CHECK(est_filter(v, 50.0, 10).empty());
    CHECK(est_filter({}, 1000.0, 10).empty());

    std::printf("test_estation_logic: %d passed, %d failed\n", g_run - g_fail, g_fail);
    return g_fail ? 1 : 0;
}
