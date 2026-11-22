// FILE: tests/test_combat_logic.cpp
// WHAT: native unit test for cpp/MSCore/msc_combat_logic.hpp (chunk delta, enemy filter, fire edge trigger). Pure C++, no game headers.
// RUN: scripts\test-native.ps1 (cl on Windows, g++ fallback). Exit 1 on any failed CHECK.
#include <cmath>
#include <cstdio>
#include "msc_combat_logic.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static const double R = 65536.0;

static MsEnemy row(int32_t id, double d)
{
    MsEnemy e{};
    e.id = id;
    e.d_m = d;
    return e;
}

// true when v holds exactly these ids in this order
static bool ids_are(const std::vector<MsEnemy>& v, std::initializer_list<int32_t> want)
{
    if (v.size() != want.size()) return false;
    size_t i = 0;
    for (int32_t w : want) { if (v[i].id != w) return false; ++i; }
    return true;
}

int main()
{
    // delta: ca/la = origin, cb/lb = target
    {
        const int64_t c0[3] = {0, 0, 0};
        const double l0[3] = {0, 0, 0};
        double out[3] = {0, 0, 0};

        // (1) 300 m along x in raw
        const double l1[3] = {300 * R, 0, 0};
        double len = cmb_delta_m(c0, l0, c0, l1, out);
        CHECK(std::fabs(len - 300.0) < 1e-9);
        CHECK(std::fabs(out[0] - 300.0) < 1e-9 && out[1] == 0 && out[2] == 0);

        // (2) one chunk along x
        const int64_t c1[3] = {1, 0, 0};
        len = cmb_delta_m(c0, l0, c1, l0, out);
        CHECK(std::fabs(len - 2048.0) < 1e-9);

        // (3) one chunk, origin moved 1000 m back in x
        const double l2[3] = {1000 * R, 0, 0};
        len = cmb_delta_m(c0, l2, c1, l0, out);
        CHECK(std::fabs(len - 1048.0) < 1e-9);

        // (4) 3 m / 4 m -> 5 m
        const double l3[3] = {3 * R, 4 * R, 0};
        len = cmb_delta_m(c0, l0, c0, l3, out);
        CHECK(std::fabs(len - 5.0) < 1e-9);

        // (5) -3 m in x, -1 chunk in z
        const double l4[3] = {-3 * R, 0, 0};
        const int64_t c2[3] = {0, 0, -1};
        len = cmb_delta_m(c0, l0, c2, l4, out);
        CHECK(std::fabs(len - std::sqrt(9.0 + 2048.0 * 2048.0)) < 1e-9);
        CHECK(std::fabs(out[2] - (-2048.0)) < 1e-9);
    }

    // filter
    {
        const std::vector<MsEnemy> rows = {row(5, 900), row(2, 300), row(9, 3000), row(3, 300)};

        // (6) radius 1000, max 10 -> 2,3,5 (sorted by d then id)
        CHECK(ids_are(cmb_filter(rows, 1000, 10), {2, 3, 5}));
        // (7) max 2 -> 2,3
        CHECK(ids_are(cmb_filter(rows, 1000, 2), {2, 3}));
        // (8) radius 900 includes id 5 (d == radius)
        CHECK(ids_are(cmb_filter(rows, 900, 10), {2, 3, 5}));
        // (9) radius 100 -> empty
        CHECK(cmb_filter(rows, 100, 10).empty());
        // (10) max 0 -> empty
        CHECK(cmb_filter(rows, 1000, 0).empty());
    }

    // fire edge trigger
    CHECK(cmb_fire_send(-1, true) == true);
    CHECK(cmb_fire_send(-1, false) == true);
    CHECK(cmb_fire_send(1, true) == false);
    CHECK(cmb_fire_send(1, false) == true);
    CHECK(cmb_fire_send(0, false) == false);
    CHECK(cmb_fire_send(0, true) == true);

    CHECK(std::fabs(cmb_range_m(65536.0, 10, 60, 65536.0) - 10.0) < 1e-9);
    CHECK(cmb_range_m(0, 10, 60, 65536.0) == -1);
    CHECK(cmb_range_m(100, -1, 60, 65536.0) == -1);
    CHECK(cmb_range_m(100, 10, 60, 0) == -1);
    CHECK(std::fabs(cmb_engage_range(600, 0.7) - 420.0) < 1e-9);
    CHECK(cmb_engage_range(-1, 0.7) == -1);
    CHECK(cmb_engage_range(600, 0) == -1);
    CHECK(cmb_engage_range(0, 0.7) == -1);
    CHECK(cmb_min_range({}) == -1);
    CHECK(cmb_min_range({-1, 0}) == -1);
    CHECK(cmb_min_range({500, -1, 200, 900}) == 200);
    CHECK(sizeof(MsEnemy) == 48);

    std::printf("test_combat_logic: %d passed, %d failed\n", g_run - g_fail, g_fail);
    return g_fail ? 1 : 0;
}
