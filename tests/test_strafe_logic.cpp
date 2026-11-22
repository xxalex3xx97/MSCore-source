// FILE: tests/test_strafe_logic.cpp
// WHAT: native unit test for cpp/MSCore/msc_strafe_logic.hpp (sign mapping, held state, watchdog expiry). Pure C++, no game headers.
#include <cstdio>
#include "msc_strafe_logic.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

int main()
{
    CHECK(str_sign(1.0) == 1 && str_sign(0.31) == 1 && str_sign(0.3) == 0 && str_sign(0.0) == 0);
    CHECK(str_sign(-0.3) == 0 && str_sign(-0.31) == -1 && str_sign(-1.0) == -1);

    StrafeSt st;
    CHECK(!str_update(st, 0, 0, 100));                      // zero stays zero: no send
    CHECK(str_update(st, 0, 1, 100) && st.s[0] == 1 && st.at == 100);
    CHECK(str_update(st, 0, 1, 200) && st.at == 200);       // unchanged non-zero: refresh, still sent
    CHECK(str_update(st, 1, -1, 300) && st.s[0] == 1 && st.s[1] == -1);   // other axis kept
    CHECK(str_update(st, 0, 0, 400) && st.nz() && st.s[1] == -1);        // x to 0 is sent, y still held
    CHECK(str_update(st, 1, 0, 500) && !st.nz());           // last axis to 0: explicit zero is sent
    CHECK(!str_update(st, 1, 0, 600));
    CHECK(!str_update(st, 2, 1, 600) && !st.nz());          // bad axis ignored

    st = StrafeSt();
    str_update(st, 0, 1, 1000);
    CHECK(!str_expired(st, 1000 + kStrafeHoldMs));          // exactly 10 s: not yet
    CHECK(str_expired(st, 1000 + kStrafeHoldMs + 1));
    str_update(st, 0, 1, 5000);                             // refresh moves the deadline
    CHECK(!str_expired(st, 5000 + kStrafeHoldMs));
    CHECK(str_zero(st) && !st.nz() && !str_zero(st));
    CHECK(!str_expired(st, 999999));                        // zero never expires

    std::printf("test_strafe_logic: %d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
