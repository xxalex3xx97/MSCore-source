// FILE: tests/test_ballistics_logic.cpp
// WHAT: native unit test for cpp/MSCore/msc_ballistics_logic.hpp (sim-rate estimator and aim gun pick). Pure C++, no game headers.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "msc_ballistics_logic.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

// steady 22.4 ticks/s: stamp = 5.6 per 0.25 s
static void steady(BalRate& r, int upto)
{
    bal_sample(r, 0.0, 0, false);
    for (int k = 1; k <= upto; ++k) bal_sample(r, 0.25 * k, static_cast<int64_t>(std::llround(5.6 * k)), false);
}

int main()
{
    // Steady
    {
        BalRate r;
        steady(r, 40);
        int n = 0;
        const double t = bal_tps(r, &n);
        CHECK(std::fabs(t - 22.4) < 0.3);
        CHECK(n >= 3);
        CHECK(bal_age(r, 10.0) <= 0.3);
        CHECK(std::fabs(bal_tps_conv(r, 10.0) - 22.4) < 0.3);

        // Outlier: one sample far off is rejected, ring unchanged
        const size_t before = r.ring.size();
        bal_sample(r, 10.25, static_cast<int64_t>(std::llround(5.6 * 41)) + 100, false);
        CHECK(std::fabs(bal_tps(r) - 22.4) < 0.3);
        CHECK(r.ring.size() == before);

        // Call gap: restart series, ring kept
        const size_t ring_before = r.ring.size();
        bal_sample(r, 12.0, 400, false);
        CHECK(r.ring.size() == ring_before);
        CHECK(r.ev.size() == 0);

        // Frame decrease resets the rate
        BalRate d;
        steady(d, 40);
        bal_sample(d, 10.25, 10, false);
        CHECK(bal_tps(d) == 0.0);

        // Pause resets everything
        BalRate p;
        steady(p, 40);
        bal_sample(p, 10.5, 99999, true);
        int pn = -1;
        CHECK(bal_tps(p, &pn) == 0.0);
        CHECK(pn == 0);
        CHECK(bal_age(p, 11.0) > 1e8);
    }

    // Low rate (about 2 ticks/s) is rejected
    {
        BalRate r;
        for (int k = 0; k <= 40; ++k) bal_sample(r, 0.25 * k, static_cast<int64_t>(std::llround(0.5 * k)), false);
        CHECK(bal_tps(r) == 0.0);
    }

    // bal_tps_conv: empty -> fallback; stale -> fallback
    {
        BalRate e;
        CHECK(bal_tps_conv(e, 0.0) == 22.4);
        BalRate s;
        steady(s, 40);
        CHECK(std::fabs(bal_tps_conv(s, 10.0) - 22.4) < 0.3);
        CHECK(bal_tps_conv(s, 50.0) == 22.4);
    }

    // bal_pick
    {
        std::vector<BalGun> g5;
        for (int i = 0; i < 5; ++i) g5.push_back({ 6.0, 100.0, 5.0, true });
        const BalPick p = bal_pick(g5);
        CHECK(p.ok);
        CHECK(p.n == 5);
        CHECK(p.distinct == 1);
        CHECK(std::fabs(p.range_m - 600.0) < 1e-9);
        CHECK(std::fabs(p.tbs_frames - 5.0) < 1e-9);
    }
    {
        std::vector<BalGun> g;
        for (int i = 0; i < 3; ++i) g.push_back({ 6.0, 100.0, 5.0, true });
        for (int i = 0; i < 2; ++i) g.push_back({ 8.0, 100.0, 5.0, true });
        const BalPick p = bal_pick(g);
        CHECK(p.ok);
        CHECK(std::fabs(p.speed_mpf - 6.0) < 1e-9);
        CHECK(p.n == 3);
        CHECK(p.distinct == 2);
        CHECK(std::fabs(p.smin - 6.0) < 1e-9);
        CHECK(std::fabs(p.smax - 8.0) < 1e-9);
    }
    {
        // 2v2 tie -> lower speed
        std::vector<BalGun> g = { { 6.0, 100.0, 5.0, true }, { 6.0, 100.0, 5.0, true }, { 8.0, 100.0, 5.0, true }, { 8.0, 100.0, 5.0, true } };
        const BalPick p = bal_pick(g);
        CHECK(p.ok && std::fabs(p.speed_mpf - 6.0) < 1e-9);
    }
    {
        // non-primary guns are ignored
        std::vector<BalGun> g = { { 6.0, 100.0, 5.0, true }, { 6.0, 100.0, 5.0, true },
                                  { 8.0, 100.0, 5.0, false }, { 8.0, 100.0, 5.0, false }, { 8.0, 100.0, 5.0, false } };
        const BalPick p = bal_pick(g);
        CHECK(p.ok && std::fabs(p.speed_mpf - 6.0) < 1e-9 && p.n == 2 && p.distinct == 1);
    }
    {
        std::vector<BalGun> g = { { 6.0, 100.0, 5.0, false }, { 8.0, 100.0, 5.0, false } };
        CHECK(!bal_pick(g).ok);
    }
    {
        CHECK(!bal_pick(std::vector<BalGun>()).ok);
        CHECK(!bal_pick(std::vector<BalGun>{ { 0.0, 100.0, 5.0, true } }).ok);
        CHECK(!bal_pick(std::vector<BalGun>{ { 6.0, 0.0, 5.0, true } }).ok);
    }

    std::printf("test_ballistics_logic: %d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
