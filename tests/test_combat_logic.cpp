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
    // enemy velocity: raw per frame -> m/s, shape fallback
    {
        const double raw[3] = {R, 0, -2 * R};   // 1 m/frame, 0, -2 m/frame
        double vf[3], vm[3];
        const double sp = cmb_vel_mps(raw, kCmbRawPerM, kSimFps, vf, vm);
        CHECK(std::fabs(vf[0] - 1.0) < 1e-9 && std::fabs(vf[2] + 2.0) < 1e-9);
        CHECK(std::fabs(vm[0] - 60.0) < 1e-9 && std::fabs(vm[2] + 120.0) < 1e-9);
        CHECK(std::fabs(sp - std::sqrt(60.0 * 60.0 + 120.0 * 120.0)) < 1e-9);
        CHECK(cmb_vel_mps(raw, 0, kSimFps, vf, vm) == -1);
        CHECK(cmb_vel_mps(raw, kCmbRawPerM, 0, vf, vm) == -1);
        CHECK(cmb_vel_shape(true, true) == 1 && cmb_vel_shape(false, true) == 2 && cmb_vel_shape(true, false) == 1 && cmb_vel_shape(false, false) == 0);
    }
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
    CHECK(cmb_cannon_reach(2) == kObservedCannonReachM && kObservedCannonReachM == 700);
    CHECK(cmb_cannon_reach(0) == -1);
    CHECK(cmb_enemy_flags(false, false, 0) == 0);
    CHECK(cmb_enemy_flags(true, false, 0) == 1);
    CHECK(cmb_enemy_flags(false, true, 0) == 2);
    CHECK(cmb_enemy_flags(true, true, 7) == (1 | 2 | (7 << 8)));
    CHECK(cmb_flag_alive(cmb_enemy_flags(true, false, 3)) && !cmb_flag_moved(cmb_enemy_flags(true, false, 3)));
    CHECK(!cmb_flag_alive(cmb_enemy_flags(false, true, 3)) && cmb_flag_moved(cmb_enemy_flags(false, true, 3)));
    CHECK(cmb_flag_task(cmb_enemy_flags(true, true, 0xFF)) == 0xFF);
    CHECK(cmb_flag_task(cmb_enemy_flags(true, true, 0x1AB)) == 0xAB);
    CHECK(cmb_class_big(L"BP_RelicDestroyerObject_C") && cmb_class_big(L"bpo_DESTROYER") && cmb_class_big(L"Destroyer"));
    CHECK(!cmb_class_big(L"BP_RelicDirk_C") && !cmb_class_big(L"Crescent") && !cmb_class_big(L"") && !cmb_class_big(L"Destroye"));
    CHECK(cmb_enemy_flags(false, false, 0, true) == 4 && cmb_flag_big(4) && !cmb_flag_big(1 | 2 | (0xFF << 8)));
    CHECK(cmb_enemy_flags(true, true, 0xFF, true) == (1 | 2 | 4 | (0xFF << 8)));
    CHECK(cmb_flag_alive(cmb_enemy_flags(true, false, 9, true)) && !cmb_flag_moved(cmb_enemy_flags(true, false, 9, true)) && cmb_flag_task(cmb_enemy_flags(true, false, 9, true)) == 9);

    CHECK(cmb_probe_class_line(7, L"BP_RelicDestroyerObject_C", true, 3000) == L"PROBE enemy_class id=7 class=BP_RelicDestroyerObject_C big=1 hull=3000");
    CHECK(cmb_probe_class_line(-1, L"", false, 0) == L"PROBE enemy_class id=-1 class= big=0 hull=0");
    {
        CmbLogRate lr;
        int ok = 0;
        for (int i = 0; i < 25; ++i) ok += cmb_log_allow(lr, 1000) ? 1 : 0;
        CHECK(ok == 20);                                   // burst capped at 20
        CHECK(!cmb_log_allow(lr, 10999));                  // still inside the 10 s window
        CHECK(cmb_log_allow(lr, 11000));                   // new window opens
    }

    std::printf("test_combat_logic: %d passed, %d failed\n", g_run - g_fail, g_fail);
    return g_fail ? 1 : 0;
}
