// FILE: tests/test_floaters_logic.cpp
// WHAT: native unit test for cpp/MSCore/msc_floaters_logic.hpp (sector box test, point binning, row order). Pure C++, no game headers.
#include <cstdio>
#include "msc_floaters_logic.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static FlSecBox sec(int id, int sys, int64_t x, int64_t y, int64_t z)
{
    FlSecBox s{};
    s.id = id; s.sys = sys;
    s.ch[0] = x; s.ch[1] = y; s.ch[2] = z;
    s.he[0] = s.he[1] = s.he[2] = 2;
    return s;
}

static FlPt pt(int sys, int64_t x, int64_t y, int64_t z)
{
    FlPt p{};
    p.sys = sys; p.ch[0] = x; p.ch[1] = y; p.ch[2] = z;
    return p;
}

int main()
{
    const FlSecBox A = sec(1, 1, 0, 0, 0);
    const FlSecBox B = sec(2, 1, 3, 0, 0);
    const FlSecBox C = sec(3, 2, 0, 0, 0);

    CHECK(fl_in_box(A, pt(1, 2, 2, 2)));
    CHECK(!fl_in_box(A, pt(1, 3, 0, 0)));
    CHECK(!fl_in_box(A, pt(2, 0, 0, 0)));

    std::vector<FlSecBox> secs = { A, B, C };
    std::vector<FlPt> pts;
    for (int i = 0; i < 3; ++i) pts.push_back(pt(1, 0, 0, 0));   // A only
    pts.push_back(pt(1, 2, 0, 0));                               // A and B: B is nearer (distance 1 against 2)
    pts.push_back(pt(1, 9, 9, 9));                               // unbound
    for (int i = 0; i < 4; ++i) pts.push_back(pt(2, 1, 1, 1));   // C

    int unbound = -1;
    std::vector<MsFloaterSec> rows = fl_bin(secs, pts, unbound);
    CHECK(unbound == 1);
    CHECK(rows.size() == 3);
    if (rows.size() == 3)
    {
        CHECK(rows[0].id == 3 && rows[0].count == 4 && rows[0].sys == 2);
        CHECK(rows[1].id == 1 && rows[1].count == 3);
        CHECK(rows[2].id == 2 && rows[2].count == 1 && rows[2].ch[0] == 3 && rows[2].ch[1] == 0 && rows[2].ch[2] == 0);
    }

    // tie: a point at equal distance in two boxes goes to the lowest id
    std::vector<FlSecBox> tie = { sec(7, 1, -1, 0, 0), sec(5, 1, 1, 0, 0) };
    std::vector<FlPt> tp = { pt(1, 0, 0, 0) };
    std::vector<MsFloaterSec> tr = fl_bin(tie, tp, unbound);
    CHECK(tr.size() == 1 && tr[0].id == 5 && tr[0].count == 1);

    // empty points: no rows, nothing unbound
    std::vector<MsFloaterSec> er = fl_bin(secs, std::vector<FlPt>(), unbound);
    CHECK(er.empty() && unbound == 0);

    std::printf("test_floaters_logic: %d passed, %d failed\n", g_run - g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
