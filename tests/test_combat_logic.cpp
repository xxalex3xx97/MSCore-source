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
    // enemy motion slots
    {
        double m[14];
        cmb_motion_init(m);
        CHECK(m[0] == -1 && m[6] == -1 && m[10] == -1 && m[11] == -1 && m[12] == -1 && m[13] == -1 && m[7] == kCmbNoLoc && m[9] == kCmbNoLoc && cmb_motion_noloc(m));
        const double rel[3] = {1, -2, 3};
        cmb_motion_set_target(m, true, rel);
        CHECK(m[7] == 1 && m[8] == -2 && m[9] == 3 && !cmb_motion_noloc(m));
        cmb_motion_set_target(m, false, rel);
        CHECK(cmb_motion_noloc(m));
        CHECK(std::fabs(cmb_rate_per_s(true, 2.982, kSimFps) - 178.92) < 1e-9 && cmb_rate_per_s(false, 2.982, kSimFps) == -1 && cmb_rate_per_s(true, 1, 0) == -1);
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

    // probe line now prints health=<cur>/<max> (was hull=<h>): the live ghost finding needs cur and max side by side
    CHECK(cmb_probe_class_line(7, L"BP_RelicDestroyerObject_C", true, 3000, 3000) == L"PROBE enemy_class id=7 class=BP_RelicDestroyerObject_C big=1 health=3000/3000");
    CHECK(cmb_probe_class_line(-1, L"", false, 0) == L"PROBE enemy_class id=-1 class= big=0 health=0/?");
    CHECK(cmb_probe_class_line(5, L"C", false, -1, 50) == L"PROBE enemy_class id=5 class=C big=0 health=?/50");
    // alive = CURRENT Health > 0; MaxHealth never decides; unreadable = not alive + unknown bit
    CHECK(cmb_hp_state(true, 0) == CMB_HP_DEAD && cmb_hp_state(true, -5) == CMB_HP_DEAD);
    CHECK(cmb_hp_state(true, 1) == CMB_HP_ALIVE && cmb_hp_state(true, 50) == CMB_HP_ALIVE);
    CHECK(cmb_hp_state(false, 50) == CMB_HP_UNKNOWN && cmb_hp_state(false, 0) == CMB_HP_UNKNOWN);
    CHECK(cmb_hull_value(true, 0) == 0 && cmb_hull_value(true, 50) == 50 && cmb_hull_value(false, 50) == -1 && cmb_hull_value(true, -3) == 0 && cmb_hull_value(true, 1LL << 40) == 2147483647);
    {
        const int32_t unk = cmb_enemy_flags(cmb_hp_state(false, 0) == CMB_HP_ALIVE, false, 2, false, cmb_hp_state(false, 0) == CMB_HP_UNKNOWN);
        CHECK(!cmb_flag_alive(unk) && cmb_flag_hull_unknown(unk) && unk == (8 | (2 << 8)));
        const int32_t ok = cmb_enemy_flags(true, true, 4, true, false);
        CHECK(cmb_flag_alive(ok) && !cmb_flag_hull_unknown(ok) && cmb_flag_big(ok) && ok == (1 | 2 | 4 | (4 << 8)));
        CHECK(cmb_flag_big(cmb_enemy_flags(true, false, 0, true, true)) && cmb_flag_hull_unknown(cmb_enemy_flags(true, false, 0, true, true)));   // big independent of unknown
        CHECK(!cmb_flag_hull_unknown(cmb_enemy_flags(true, true, 0xFF, true)));
    }
    CHECK(cmb_dead_line(41312, L"BP_RelicCrescentObject_C", 0, 50) == L"PROBE enemy_dead id=41312 class=BP_RelicCrescentObject_C health=0 max=50");
    CHECK(cmb_task_line(41312, L"C", 2, 0) == L"PROBE enemy_task id=41312 class=C task=2 health=0");
    {
        std::set<std::pair<std::wstring, int>> s;
        CHECK(cmb_first_seen(s, L"A", 2) && !cmb_first_seen(s, L"A", 2) && cmb_first_seen(s, L"A", 4) && cmb_first_seen(s, L"B", 2));
    }
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
