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

    // logical right -> game Y slot, logical up -> game X slot; a throttle send carries both (same state, both components)
    StrafeSt m; long long comp[3];
    str_update(m, 0, 1, 1);
    str_slots(m.s[0], m.s[1], comp);
    CHECK(comp[1] == 65536 && comp[0] == 0);
    str_update(m, 1, -1, 2);
    str_slots(m.s[0], m.s[1], comp);
    CHECK(comp[1] == 65536 && comp[0] == -65536);
    CHECK(str_slot(0) == 1 && str_slot(1) == 0);

    // raw value written for +-1 is exactly +-65536, 0 for 0; right goes to the right slot, up to the up slot
    CHECK(str_raw(1) == 65536 && str_raw(-1) == -65536 && str_raw(0) == 0);
    long long rw[3];
    str_slots(1, -1, rw);
    CHECK(rw[kStrafeSlotRight] == 65536 && rw[kStrafeSlotUp] == -65536 && rw[2] == 0);
    str_slots(0, 0, rw);
    CHECK(rw[0] == 0 && rw[1] == 0 && rw[2] == 0);
    CHECK(kStrafeSlotRight != kStrafeSlotUp && str_slot(0) == kStrafeSlotRight && str_slot(1) == kStrafeSlotUp);

    // keep-alive: resend a non-zero state after 500 ms without a send
    StrafeSt k;
    CHECK(!str_needs_resend(k, 9999));                       // zero: never
    str_update(k, 0, 1, 1000); k.sent = 1000;
    CHECK(!str_needs_resend(k, 1499) && str_needs_resend(k, 1500));

    std::printf("test_strafe_logic: %d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
