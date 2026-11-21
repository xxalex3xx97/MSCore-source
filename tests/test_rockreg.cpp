// FILE: tests/test_rockreg.cpp
// WHAT: native unit test for the rock registry logic (cpp/MSCore/msc_rockreg_logic.hpp), API 16. Pure C++, no game headers.
// RUN: scripts\test-native.ps1 (cl on Windows, g++ fallback). Exit 1 on any failed CHECK.
#include <limits>
#include <cstdio>
#include "msc_rockreg_logic.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))

// ore1 = one ore name index (-1 = none); rad < 0 = radius not read yet
static size_t rr_add(RrTable& g, int id, int sys, double x, double y, double z, int16_t ore1 = -1, double rad = -1.0)
{
    const double c[3] = {x, y, z};
    const int16_t ore[4] = {ore1, -1, -1, -1};
    const size_t i = g.add(id, sys, c, ore);
    if (rad >= 0.0) g.set_radius(i, rad);
    return i;
}

static void t_table()
{
    RrTable g;
    const uint64_t v0 = g.version;
    rr_add(g, 1, 7, 0, 0, 0);
    rr_add(g, 2, 7, 100, 0, 0);
    CHECK(g.version == v0 + 2);
    rr_add(g, 1, 7, 5, 0, 0);                    // known id: updated, not duplicated
    CHECK(g.rocks.size() == 2 && g.rocks[0].c[0] == 5.0);
    CHECK(g.version == v0 + 2);
    CHECK(g.is_alive(1) && g.is_alive(2) && !g.is_alive(3));
    CHECK(g.index_of(2) == 1 && g.index_of(3) == RrTable::npos && g.get(3) == nullptr);
    CHECK(g.kill(2, 10));
    CHECK(!g.kill(2, 11));                       // already dead
    CHECK(!g.is_alive(2));
    CHECK(g.get(2) != nullptr && g.get(2)->destroyed_at == 10);
    CHECK(g.version == v0 + 3);
    rr_add(g, 2, 7, 100, 0, 0);                  // re-add revives
    CHECK(g.is_alive(2) && g.version == v0 + 4);
    const double p[3] = {0, 0, 0};
    const size_t i = g.add(9, 7, p);             // no ore given: -1
    CHECK(g.rocks[i].ore[0] == -1 && g.rocks[i].ore[3] == -1);
    g.target_set(5, true);
    CHECK(g.targeted().count(5) == 1);
    g.target_set(5, false);
    CHECK(g.targeted().empty());
    CHECK(!rr_over_budget(0.9) && rr_over_budget(1.1));
}

static void t_world_track()
{
    RrWorldTrack t;
    CHECK(rr_world_step(t, 7, 4) == RWE_LOADED && t.ok);
    CHECK(rr_world_step(t, 7, 4) == RWE_NONE);
    for (int i = 0; i < 3; ++i) CHECK(rr_world_step(t, 0, 4) == RWE_NONE);
    CHECK(t.ok);
    CHECK(rr_world_step(t, 7, 4) == RWE_NONE && t.bad == 0);
    for (int i = 0; i < 3; ++i) rr_world_step(t, 0, 4);
    CHECK(rr_world_step(t, 0, 4) == RWE_LOST && !t.ok && t.id == 0);
    CHECK(rr_world_step(t, 0, 4) == RWE_NONE && !t.ok);
    CHECK(rr_world_step(t, 7, 4) == RWE_LOADED && t.ok);
    CHECK(rr_world_step(t, 9, 4) == RWE_LOADED && t.ok);
}

static void t_within_nearest()
{
    RrTable g;
    rr_add(g, 1, 7, 0, 0, 0, 0, 10);
    rr_add(g, 2, 7, 3000, 0, 0, 0, 10);      // another grid cell than rock 1
    rr_add(g, 3, 7, 1000, 0, 0, -1, 10);     // no ore
    rr_add(g, 4, 8, 500, 0, 0, 0, 10);       // other system
    const double o[3] = {0, 0, 0};
    std::vector<size_t> out;
    CHECK(g.within(7, o, 1500, out) == 2);   // rocks 1 and 3
    out.clear();
    CHECK(g.within(7, o, 5000, out) == 3);
    std::vector<std::pair<double, size_t>> nn;
    CHECK(g.nearest_n(7, 0, o, nullptr, 5000, 8, nn) == 2);                      // ore index 0 only
    CHECK(nn[0].first <= nn[1].first && g.rocks[nn[0].second].id == 1);
    CHECK(g.nearest_n(7, -1, o, nullptr, 5000, 8, nn) == 3);                     // any ore
    CHECK(g.nearest_n(7, 1, o, nullptr, 5000, 8, nn) == 0);                      // other ore index
    CHECK(g.nearest_n(7, 0, o, nullptr, 5000, 1, nn) == 1 && g.rocks[nn[0].second].id == 1);   // n caps
    std::set<int> skip = {1};
    CHECK(g.nearest_n(7, 0, o, &skip, 5000, 8, nn) == 1 && g.rocks[nn[0].second].id == 2);
    CHECK(g.nearest_n(7, 0, o, &skip, 2000, 8, nn) == 0);                        // max_m
    CHECK(g.nearest_n(9, 0, o, nullptr, 5000, 8, nn) == 0);                      // unknown system
    g.kill(1, 1);
    CHECK(g.nearest_n(7, 0, o, nullptr, 5000, 8, nn) == 1 && g.rocks[nn[0].second].id == 2);
    // up to 4 ore slots, matched by name index
    RrTable m;
    const double c[3] = {0, 0, 0};
    const int16_t multi[4] = {2, 5, 7, 9};
    m.add(1, 7, c, multi);
    CHECK(m.nearest_n(7, 9, o, nullptr, 100, 1, nn) == 1 && m.nearest_n(7, 5, o, nullptr, 100, 1, nn) == 1);
    CHECK(m.nearest_n(7, 3, o, nullptr, 100, 1, nn) == 0);
}

static void t_surface_radius()
{
    RrTable g;
    rr_add(g, 1, 7, 1000, 0, 0, -1, 900);    // big: surface 100
    rr_add(g, 2, 7, 500, 0, 0, -1, 0);       // surface 500
    const double o[3] = {0, 0, 0};
    double s = 0;
    size_t n = g.nearest_surface(7, o, 35.0, s);
    CHECK(n != RrTable::npos && g.rocks[n].id == 1 && std::fabs(s - 100.0) < 1e-9);
    n = g.nearest_surface(7, o, 35.0, s, 1);   // skip_id
    CHECK(n != RrTable::npos && g.rocks[n].id == 2 && std::fabs(s - 500.0) < 1e-9);
    RrTable u;
    rr_add(u, 1, 7, 1000, 0, 0);               // radius unknown: counts as unknown_rad
    n = u.nearest_surface(7, o, 35.0, s);
    CHECK(n != RrTable::npos && std::fabs(s - 965.0) < 1e-9);
    CHECK(u.nearest_surface(9, o, 35.0, s) == RrTable::npos);

    std::vector<std::array<double, 3>> refs = {{{0.0, 0.0, 0.0}}};
    std::vector<size_t> want;
    g.radius_wanted(7, refs, 5000, 10, want);
    CHECK(want.empty());                        // both radii known
    u.radius_wanted(7, refs, 5000, 10, want);
    CHECK(want.size() == 1);
    want.clear();
    const double a[3] = {0, 0, 0}, e[3] = {3000, 0, 0};
    u.radius_wanted_seg(7, a, e, 100, 10, want);
    CHECK(want.size() == 1);                    // unknown radius, on the segment
    want.clear();
    g.radius_wanted_seg(7, a, e, 100, 10, want);
    CHECK(want.empty());                        // known radii
}

static void t_within_seg()
{
    RrTable g;
    rr_add(g, 1, 7, 1000, 0, 0);               // on the segment
    rr_add(g, 2, 7, 1000, 400, 0);             // 400 m off
    rr_add(g, 3, 7, -300, 0, 0);               // beyond the a end: clamped distance 300
    rr_add(g, 4, 7, 1000, 80, 80);             // inside the box of margin 150, but 113 m off
    rr_add(g, 5, 8, 1000, 0, 0);               // other system
    rr_add(g, 6, 7, 1000, 0, 0);
    g.kill(6, 1);                              // dead
    const double a[3] = {0, 0, 0}, b[3] = {3000, 0, 0};
    std::vector<size_t> out;
    CHECK(g.within_seg(7, a, b, 150, out) == 2);   // rocks 1 and 4
    CHECK(g.rocks[out[0]].id == 1 || g.rocks[out[1]].id == 1);
    out.clear();
    CHECK(g.within_seg(7, a, b, 100, out) == 1 && g.rocks[out[0]].id == 1);   // 4 is 113 m off: box accepts, distance rejects
    out.clear();
    CHECK(g.within_seg(7, a, b, 350, out) == 3);   // 1, 3 (300 beyond the end), 4
    out.clear();
    CHECK(g.within_seg(7, a, b, 500, out) == 4);   // plus 2
    CHECK(rr_box_reject(a, b, g.rocks[1].c, 100) && !rr_box_reject(a, b, g.rocks[1].c, 500));
    NEAR(rr_seg_point_dist(a, b, g.rocks[2].c), 300.0, 1e-9);
}

static void t_abs_grid()
{
    const int64_t ch[3] = {1, 0, -1};
    const double loc[3] = {65536.0, 0.0, 131072.0};
    double c[3];
    rr_abs_m(ch, loc, 65536.0, c);
    NEAR(c[0], 2049.0, 1e-9);
    NEAR(c[1], 0.0, 1e-9);
    NEAR(c[2], -2046.0, 1e-9);
    CHECK(rr_cell_idx(-1.0) == -1 && rr_cell_idx(2047.9) == 0 && rr_cell_idx(2048.0) == 1);
    // a query over more than RR_GRID_MAX_CELLS cells scans the system list (same answer)
    RrTable g;
    rr_add(g, 1, 7, 0, 0, 0);
    rr_add(g, 2, 7, 1.0e7, 0, 0);
    const double o[3] = {0, 0, 0};
    std::vector<size_t> out;
    CHECK(g.within(7, o, 2.0e7, out) == 2);
}

static void t_kill_unseen()
{
    RrTable g;
    const double p[3] = {0, 0, 0};
    g.add(1, 7, p);
    g.add(2, 7, p);
    g.add(3, 7, p);
    const std::vector<uint64_t> seen = {5, 4, 5};   // pass 5: rock 2 not seen
    CHECK(rr_kill_unseen(g, seen, 5, 9) == 1);
    CHECK(g.is_alive(1) && !g.is_alive(2) && g.is_alive(3));
    CHECK(rr_kill_unseen(g, seen, 5, 10) == 0);
    CHECK(g.add(4, 7, p) == 3 && g.is_alive(4));
}

static void t_rings_zones()
{
    std::vector<RrSrc> src(1);
    src[0].sys = 7;
    const int64_t a0[3] = {0, 0, 0}, a3[3] = {3, 0, -1}, a13[3] = {13, 0, 0}, far[3] = {10000, 0, 0};
    CHECK(rr_ring_of(7, a0, src) == 0);
    CHECK(rr_ring_of(7, a3, src) == 3);
    CHECK(rr_ring_of(7, a13, src) == 13);
    CHECK(rr_ring_of(7, far, src) == RR_RING_CAP);
    CHECK(rr_ring_of(9, a0, src) == RR_RING_FAR);
    const std::vector<int> ring = {13, 0, RR_RING_FAR, 3, 20};
    std::vector<size_t> order;
    size_t near_n = 0;
    rr_order_rings(ring, RR_PRIORITY_CHUNKS, order, near_n);
    CHECK(order.size() == 5 && near_n == 2);
    CHECK(order[0] == 1 && order[1] == 3 && order[2] == 0 && order[3] == 4 && order[4] == 2);
    const double pm[3] = {2047.9, -0.5, 4096.0};
    int64_t cm[3] = {0, 0, 0};
    rr_chunk_of_m(pm, cm);
    CHECK(cm[0] == 0 && cm[1] == -1 && cm[2] == 2);

    RrTable g;
    g.zones_begin(1, src, RR_PRIORITY_CHUNKS);
    const int64_t c0[3] = {0, 0, 0}, c11[3] = {11, 0, 0}, c12[3] = {12, 0, 0}, cm12[3] = {-12, 0, 0};
    CHECK(!g.area_ready(7, c0, 0));
    g.zones_ready(1);
    CHECK(g.area_ready(7, c11, 1));
    CHECK(!g.area_ready(7, c12, 1));
    CHECK(g.area_ready(7, cm12, 0));
    CHECK(!g.area_ready(8, c0, 0));
    std::vector<RrSrc> src2(1);
    src2[0].sys = 9;
    g.zones_begin(2, src2, RR_PRIORITY_CHUNKS);   // drops not-ready zones, keeps the ready one
    CHECK(g.area_ready(7, c0, 0));
    CHECK(!g.area_ready(9, c0, 0));
}

static RrCacheData cache_sample()
{
    RrCacheData d;
    d.kinds = {"Iron", "Ice ore"};
    RrCacheRec a; a.id = 11; a.sys = 7; a.c[0] = 1.5; a.c[1] = -2048.25; a.c[2] = 4096.0; a.rad = 12.5f; a.ore[0] = 0;
    RrCacheRec b; b.id = 12; b.sys = 7; b.rad = -1.0f;
    RrCacheRec c; c.id = 13; c.sys = 8; c.c[0] = 3.0; c.c[1] = 4.0; c.c[2] = 5.0; c.rad = 0.0f; c.ore[0] = 1; c.ore[1] = 0;
    d.recs = {a, b, c};
    d.h.exe_size = 1234;
    d.h.save_n = 3;
    return d;
}

static void t_cache()
{
    CHECK(RR_CACHE_VER == 2 && RR_CACHE_REC == 48);
    const std::vector<uint8_t> img = rr_cache_encode(cache_sample());
    CHECK(img.size() == RR_CACHE_HDR + 3 * RR_CACHE_REC + 4 + (2 + 4) + (2 + 7));
    RrCacheData o;
    std::string why;
    CHECK(rr_cache_parse(img.data(), img.size(), 1234, o, why));
    CHECK(o.h.save_n == 3 && o.h.rock_n == 3 && o.h.exe_size == 1234);
    CHECK(o.kinds.size() == 2 && o.kinds[0] == "Iron" && o.kinds[1] == "Ice ore");
    CHECK(o.recs.size() == 3);
    CHECK(o.recs[0].id == 11 && o.recs[0].sys == 7 && o.recs[0].c[1] == -2048.25 && o.recs[0].rad == 12.5f && o.recs[0].ore[0] == 0 && o.recs[0].ore[1] == -1);
    CHECK(o.recs[1].rad == -1.0f && o.recs[1].ore[0] == -1);
    CHECK(o.recs[2].c[2] == 5.0 && o.recs[2].rad == 0.0f && o.recs[2].ore[0] == 1 && o.recs[2].ore[1] == 0 && o.recs[2].ore[2] == -1);

    const std::vector<uint8_t>& good = img;
    auto bad = [&](const std::vector<uint8_t>& im, uint64_t exe) { return !rr_cache_parse(im.data(), im.size(), exe, o, why); };
    CHECK(bad(good, 999));                                                   // exe size changed
    CHECK(bad(good, 0));                                                     // exe size unknown
    { auto x = good; x[0] ^= 0xFF; CHECK(bad(x, 1234)); }                    // magic
    { auto x = good; x[4] = 1; CHECK(bad(x, 1234) && why == "version"); }    // v1 file rejected
    { auto x = good; x[RR_CACHE_HDR + 9] ^= 0x10; CHECK(bad(x, 1234)); }     // checksum
    { auto x = good; x.pop_back(); CHECK(bad(x, 1234)); }                    // truncated
    { auto x = good; x.resize(20); CHECK(bad(x, 1234)); }                    // short
    { RrCacheData d = cache_sample(); d.recs[0].ore[0] = 5; CHECK(bad(rr_cache_encode(d), 1234)); }    // ore index out of range
    { RrCacheData d = cache_sample(); d.recs[0].ore[2] = 1; CHECK(bad(rr_cache_encode(d), 1234)); }    // not -1 terminated
    { RrCacheData d = cache_sample(); d.recs[0].ore[0] = -2; CHECK(bad(rr_cache_encode(d), 1234)); }
    { RrCacheData d = cache_sample(); d.recs[1].id = 0; CHECK(bad(rr_cache_encode(d), 1234)); }
    { RrCacheData d = cache_sample(); d.recs[0].c[0] = std::nan(""); CHECK(bad(rr_cache_encode(d), 1234)); }
    { RrCacheData d = cache_sample(); d.recs[0].rad = -5.0f; CHECK(bad(rr_cache_encode(d), 1234)); }
    CHECK(rr_cache_parse(good.data(), good.size(), 1234, o, why));
    RrCacheData e;
    e.h.exe_size = 5;
    const std::vector<uint8_t> eimg = rr_cache_encode(e);                   // empty table is valid
    CHECK(rr_cache_parse(eimg.data(), eimg.size(), 5, o, why) && o.recs.empty());

    CHECK(rr_cache_verdict(5, 5, 50, 0) == RCV_TRUST);
    CHECK(rr_cache_verdict(5, 6, 50, 0) == RCV_MERGE);
    CHECK(rr_cache_verdict(5, 5, 50, 1) == RCV_RESEED);
    CHECK(rr_cache_verdict(5, 5, 0, 0) == RCV_RESEED);
    const double a[3] = {1, 2, 3}, b[3] = {1.005, 2, 3}, c[3] = {1.02, 2, 3};
    CHECK(rr_same_pos(a, b) && !rr_same_pos(a, c));
}

// ore-name match works on rocks built from a parsed cache (fixes: ore was lost after a trusted cache load)
static void t_cache_loaded_ore()
{
    const std::vector<uint8_t> img = rr_cache_encode(cache_sample());
    RrCacheData o;
    std::string why;
    CHECK(rr_cache_parse(img.data(), img.size(), 1234, o, why));
    RrTable t;
    t.kinds = o.kinds;
    for (const RrCacheRec& c : o.recs)
    {
        const size_t i = t.add(c.id, c.sys, c.c, c.ore);
        if (c.rad >= 0.0f) t.set_radius(i, c.rad);
        t.rocks[i].unverified = true;
    }
    const double p[3] = {0, 0, 0};
    std::vector<std::pair<double, size_t>> nn;
    const int16_t iron = 0, ice = 1;
    CHECK(t.kinds[iron] == "Iron" && t.kinds[ice] == "Ice ore");
    CHECK(t.nearest_n(7, iron, p, nullptr, 1.0e7, 8, nn) == 1 && t.rocks[nn[0].second].id == 11);
    CHECK(t.nearest_n(8, ice, p, nullptr, 1.0e7, 8, nn) == 1 && t.rocks[nn[0].second].id == 13);
    CHECK(t.nearest_n(8, iron, p, nullptr, 1.0e7, 8, nn) == 1);   // rock 13 holds Ice ore then Iron
    CHECK(t.nearest_n(7, ice, p, nullptr, 1.0e7, 8, nn) == 0);
}

// NaN / infinite / huge / inverted bounds: fast, empty or bounded (they used to overflow the cell span and hang)
static void t_bad_bounds()
{
    RrTable t;
    for (int i = 0; i < 20; ++i) rr_add(t, 100 + i, 7, i * 3000.0, 0, 0);
    const double nan = std::nan(""), inf = std::numeric_limits<double>::infinity();
    const double o[3] = {0, 0, 0}, far[3] = {-1e30, 0, 0}, nanp[3] = {nan, 0, 0}, infp[3] = {inf, 0, 0};
    const double hi0[3] = {0, 0, 0}, big[3] = {1e30, 1e30, 1e30}, nbig[3] = {-1e30, -1e30, -1e30};
    int n = 0;
    t.for_box(7, far, hi0, [&](size_t) { ++n; });          // one huge end: bounded, no hang
    CHECK(n <= 20);
    n = 0;
    t.for_box(7, nbig, big, [&](size_t) { ++n; });          // whole system, each rock once
    CHECK(n == 20);
    n = 0;
    t.for_box(7, nanp, hi0, [&](size_t) { ++n; });
    t.for_box(7, o, nanp, [&](size_t) { ++n; });
    t.for_box(7, hi0, o, [&](size_t) { ++n; });             // lo == hi still visits its cell
    CHECK(n == 1);
    const double lo2[3] = {5, 0, 0}, hi2[3] = {-5, 0, 0};
    n = 0;
    t.for_box(7, lo2, hi2, [&](size_t) { ++n; });           // lo > hi: empty
    CHECK(n == 0);
    std::vector<size_t> v;
    CHECK(t.within(7, nanp, 100.0, v) == 0 && t.within(7, infp, 100.0, v) == 0 && t.within(7, o, nan, v) == 0 && t.within(7, o, -1.0, v) == 0);
    CHECK(t.within(7, o, 1e30, v) == 20);                   // "no limit" works
    CHECK(t.within(7, o, inf, v) == 20);
    v.clear();
    CHECK(t.within_seg(7, far, hi0, 10.0, v) <= 20);
    CHECK(t.within_seg(7, nanp, hi0, 10.0, v) == 0 && t.within_seg(7, o, infp, 10.0, v) == 0 && t.within_seg(7, o, hi0, nan, v) == 0);
    CHECK(t.within_seg(7, nbig, big, 1e30, v) == 20);
    std::vector<size_t> rw;
    t.radius_wanted_seg(7, nanp, hi0, 10.0, 5, rw);
    t.radius_wanted_seg(7, far, hi0, 1e30, 50, rw);
    CHECK(rw.size() <= 20);
    std::vector<std::pair<double, size_t>> nn;
    CHECK(t.nearest_n(7, -1, o, nullptr, 1e30, 5, nn) == 5 && t.nearest_n(7, -1, nanp, nullptr, 1e30, 5, nn) == 0);
    double sm = 0;
    CHECK(t.nearest_surface(7, o, 50.0, sm) != RrTable::npos);
    CHECK(t.nearest_surface(7, nanp, 50.0, sm) == RrTable::npos);
}

// review fixes: alive counter, clear, seed sanity, radius backoff, sort_unique
static void t_review()
{
    RrTable t;
    CHECK(t.alive_count() == 0);
    rr_add(t, 1, 7, 0, 0, 0);
    rr_add(t, 2, 7, 10, 0, 0);
    rr_add(t, 3, 7, 20, 0, 0);
    CHECK(t.alive_count() == 3);
    rr_add(t, 2, 7, 11, 0, 0);                        // same id again: no new row
    CHECK(t.alive_count() == 3 && t.rocks.size() == 3);
    CHECK(t.kill(2, 5) && t.alive_count() == 2);
    CHECK(!t.kill(2, 6) && t.alive_count() == 2);     // already dead
    rr_add(t, 2, 7, 11, 0, 0);                        // revive
    CHECK(t.alive_count() == 3);
    size_t n = 0;
    for (const RrRock& r : t.rocks) if (r.alive) ++n;
    CHECK(n == t.alive_count());
    t.target_set(9, true);
    const uint64_t v0 = t.version;
    t.kinds.push_back("Iron");
    t.zones_begin(1, std::vector<RrSrc>{RrSrc{}}, 12);
    t.clear();                                        // new world: nothing of the old one is reusable
    CHECK(t.alive_count() == 0 && t.rocks.empty() && t.kinds.empty() && t.zones.empty() && t.version > v0);
    CHECK(t.index_of(1) == RrTable::npos && t.get(2) == nullptr && !t.is_alive(3));
    const double o[3] = {0, 0, 0};
    std::vector<size_t> v;
    CHECK(t.within(7, o, 1e6, v) == 0);
    CHECK(t.targeted().count(9) == 1);
    rr_add(t, 1, 7, 5, 0, 0);                         // the same id can be added again, fresh
    CHECK(t.alive_count() == 1 && t.index_of(1) == 0 && t.within(7, o, 100.0, v) == 1);

    // seed sanity: unknown reference = sane; under half of the known rocks = incomplete
    CHECK(rr_seed_sane(0, 0) && rr_seed_sane(100, 0));
    CHECK(rr_seed_sane(50, 100) && rr_seed_sane(100, 100) && rr_seed_sane(500, 100));
    CHECK(!rr_seed_sane(49, 100) && !rr_seed_sane(0, 100) && !rr_seed_sane(0, 1));

    // radius retry backoff: 500 ms, doubling, capped at 60 s
    CHECK(rr_rad_backoff_ms(0) == 500 && rr_rad_backoff_ms(1) == 500 && rr_rad_backoff_ms(2) == 1000 && rr_rad_backoff_ms(3) == 2000);
    CHECK(rr_rad_backoff_ms(8) == 60000 && rr_rad_backoff_ms(250) == 60000);
    for (unsigned f = 1; f < 40; ++f) CHECK(rr_rad_backoff_ms(f + 1) >= rr_rad_backoff_ms(f));
    // an unread radius stays unknown: the rock is still wanted next time
    RrTable r2;
    const size_t i0 = rr_add(r2, 1, 7, 0, 0, 0);
    std::vector<size_t> w;
    r2.radius_wanted(7, {{0.0, 0.0, 0.0}}, 1000.0, 10, w);
    CHECK(w.size() == 1 && !r2.rocks[i0].rad_known);
    r2.set_radius(i0, 0.0);                           // a successful read of zero is stored as known
    w.clear();
    r2.radius_wanted(7, {{0.0, 0.0, 0.0}}, 1000.0, 10, w);
    CHECK(w.empty() && r2.rocks[i0].rad_known);

    // sort + unique
    std::vector<size_t> u = {5, 1, 5, 3, 1, 3, 9};
    rr_sort_unique(u);
    CHECK((u == std::vector<size_t>{1, 3, 5, 9}));
    std::vector<size_t> e;
    rr_sort_unique(e);
    CHECK(e.empty());
}

// round 3: a reseed that corrects a row moves it in the grid, pass verdict, kill gate, cache retry backoff
static void t_round3()
{
    RrTable t;
    const double o[3] = {0, 0, 0}, far[3] = {5000, 0, 0};
    rr_add(t, 1, 7, 0, 0, 0);
    rr_add(t, 2, 7, 100, 0, 0);
    std::vector<size_t> v;
    CHECK(t.within(7, o, 10.0, v) == 1);
    // stale cache row corrected by the save: other cell, same system
    rr_add(t, 1, 7, 5000, 0, 0);
    CHECK(t.rocks.size() == 2 && t.alive_count() == 2);
    v.clear();
    CHECK(t.within(7, o, 10.0, v) == 0);                       // not at the old place any more
    v.clear();
    CHECK(t.within(7, far, 10.0, v) == 1 && t.rocks[v[0]].id == 1);
    std::vector<std::pair<double, size_t>> nn;
    CHECK(t.nearest_n(7, -1, far, nullptr, 100.0, 5, nn) == 1 && t.rocks[nn[0].second].id == 1);
    double sm = 0;
    const size_t bs = t.nearest_surface(7, far, 0.0, sm);
    CHECK(bs != RrTable::npos && t.rocks[bs].id == 1);
    // other system
    rr_add(t, 1, 8, 5000, 0, 0);
    v.clear();
    CHECK(t.within(7, far, 10.0, v) == 0);
    CHECK(t.within(8, far, 10.0, v) == 1);
    const double lo[3] = {-1e30, -1e30, -1e30}, hi[3] = {1e30, 1e30, 1e30};
    size_t n7 = 0, n8 = 0;
    t.for_box(7, lo, hi, [&](size_t) { ++n7; });   // system list scan: only rock 2 is listed in system 7
    t.for_box(8, lo, hi, [&](size_t) { ++n8; });
    CHECK(n7 == 1 && n8 == 1);
    // moved inside the same cell: one entry only
    rr_add(t, 2, 7, 120, 0, 0);
    v.clear();
    CHECK(t.within(7, o, 1000.0, v) == 1 && t.rocks[v[0]].c[0] == 120.0);
    // version changes on a move
    const uint64_t v0 = t.version;
    rr_add(t, 2, 7, 4000, 0, 0);
    CHECK(t.version > v0);

    // pass verdict
    CHECK(rr_pass_verdict(100, 100, 0, 3) == RPV_OK && rr_pass_verdict(0, 0, 0, 3) == RPV_OK);
    CHECK(rr_pass_verdict(10, 100, 0, 3) == RPV_RETRY && rr_pass_verdict(10, 100, 2, 3) == RPV_RETRY);
    CHECK(rr_pass_verdict(10, 100, 3, 3) == RPV_INCOMPLETE);   // accepted, but no kill / no cache write (game side)
    CHECK(rr_pass_verdict(60, 100, 3, 3) == RPV_OK);

    // kill gate
    CHECK(!rr_kill_allowed(false, 100, 4) && !rr_kill_allowed(true, 3, 4) && rr_kill_allowed(true, 4, 4) && rr_kill_allowed(true, 99, 4));

    // cache write retry backoff: 5 min doubling, at most 1 h
    CHECK(rr_cache_retry_ms(0) == 0 && rr_cache_retry_ms(1) == 300000 && rr_cache_retry_ms(2) == 600000 && rr_cache_retry_ms(3) == 1200000);
    CHECK(rr_cache_retry_ms(5) == 3600000 && rr_cache_retry_ms(1000) == 3600000);
    for (unsigned f = 1; f < 30; ++f) CHECK(rr_cache_retry_ms(f + 1) >= rr_cache_retry_ms(f));
}

int main()
{
    t_round3();
    t_review();
    t_bad_bounds();
    t_table();
    t_world_track();
    t_within_nearest();
    t_surface_radius();
    t_within_seg();
    t_abs_grid();
    t_kill_unseen();
    t_rings_zones();
    t_cache();
    t_cache_loaded_ore();
    std::printf("test_rockreg: %d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
