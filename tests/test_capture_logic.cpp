// FILE: tests/test_capture_logic.cpp
// WHAT: native unit test for cpp/MSCore/msc_capture_logic.hpp (change detection, rate counter). Pure C++.
#include <cstdio>
#include "msc_capture_logic.hpp"
#include "msc_log_logic.hpp"

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { ++g_run; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    CapState s; long long dt = -1;
    long long a[3] = {1, 2, 3}, b[3] = {1, 2, 4};
    CHECK(cap_changed(s, a, 3, 100, dt) && dt == 0);        // first is a change
    CHECK(!cap_changed(s, a, 3, 150, dt) && dt == 50);      // same: no change, dt since last change
    CHECK(cap_changed(s, b, 3, 300, dt) && dt == 200);
    CHECK(!cap_changed(s, b, 3, 310, dt) && dt == 10);
    CHECK(!cap_changed(s, b, 9, 310, dt));                  // bad n ignored

    CapRate r; int n = -1;
    CHECK(!cap_rate_tick(r, 1000, n));                      // starts the window
    cap_rate_add(r); cap_rate_add(r); cap_rate_add(r);
    CHECK(!cap_rate_tick(r, 1999, n));
    CHECK(cap_rate_tick(r, 2000, n) && n == 3);
    CHECK(!cap_rate_tick(r, 2500, n));
    cap_rate_add(r);
    CHECK(cap_rate_tick(r, 3000, n) && n == 1);

    CHECK(!msc_log_pass(L"PROBE input_capture rate cmds_per_s=0", false));
    CHECK(msc_log_pass(L"PROBE input_capture rate cmds_per_s=0", true));
    CHECK(!msc_log_pass(L"SCAN walk ship", false));
    CHECK(msc_log_pass(L"PROBE input_capture fault, probe off", false));
    CHECK(msc_log_pass(L"selfcheck ok", false));
    CHECK(msc_log_pass(L"SCAN native fault: native scan OFF", false));

    std::printf("test_capture_logic: %d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
