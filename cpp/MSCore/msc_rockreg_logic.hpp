// FILE: cpp/MSCore/msc_rockreg_logic.hpp
// WHAT: pure shared rock registry. Table of rocks (seeded from save data: centre in metres, radius in metres, up to 4 ore name indexes, alive
//       flag), a chunk-cell grid index per system, local queries (within, within_seg, nearest_n, nearest_surface, radius_wanted*), targeted-rock
//       set, seed ordering by chunk rings (nearest first), seeded areas (zones), kill of rocks a pass did not see, world tracker, and the disk
//       cache format v2 (encode, parse, verdict of a sample check).
// WHY separate: no UE / Windows calls, so tests/test_rockreg.cpp runs it alone. The game side (the rock registry module of MSCore, API 16)
//       fills and updates it; mods only read it through the API.
// DEPENDS ON (exact): std headers only.
// UNITS: all positions and radii in METRES, absolute frame = chunk * RR_CHUNK_M + loc_raw / raw_per_m (RR_CHUNK_M = 2048 m = 134217728 raw
//        per chunk; raw_per_m = 65536 by default).
// GRID: cell = RR_CHUNK_M cube per (system, cell x, y, z). A query visits only the cells its AABB overlaps; a query whose cell count
//        exceeds RR_GRID_MAX_CELLS scans the system list instead (correct, slower).
// ORE: RrRock::ore[4] = indexes into RrTable::kinds (ore names), -1 terminated / -1 = none. Matching is by name index, so rocks loaded from the
//        cache keep their ore.
// SEED ORDER: a save's ring = chunk distance (Chebyshev, chunks) to the nearest ship of its system at pass start; no ship in the system = far
//        (last). Phase A = rings <= RR_PRIORITY_CHUNKS (read first). Zones record the seeded areas (area_ready).
// CACHE FILE (v2): header 48 B (magic, version 2, exe size, save count, rock count, kinds offset, FNV-1a checksum of everything after the
//        header), rock_n records of 48 B (int32 id, int32 sys, double x y z m, float radius m or -1, int16 ore[4] (-1 none), 4 reserved bytes),
//        then the ore name table (u32 count, per name u16 length + UTF-8 bytes). Little endian (x86). A v1 file is rejected (full reseed).
// USED BY: dllmain.cpp (rock registry module, API 16). Native unit test: tests/test_rockreg.cpp.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    // WHAT: save chunk edge (m). 134217728 raw per chunk at 65536 raw per m.
    constexpr double RR_CHUNK_M = 2048.0;
    // WHAT: a grid query over more cells than this scans the system list instead.
    constexpr size_t RR_GRID_MAX_CELLS = 4096;
    // WHAT: first search radius of the expanding nearest search (m); it grows x4 up to RR_SEARCH_MAX_M.
    constexpr double RR_SEARCH_START_M = 4096.0;
    constexpr double RR_SEARCH_MAX_M = 1.0e7;
    // WHAT: largest rock radius assumed by nearest_surface (m). Rocks seen so far: 8 m and 70 m. Bigger rocks would be found late.
    constexpr double RR_RAD_MAX_M = 1000.0;
    // WHAT: registry work budget per tick (ms) and the per-tick caps of each stage.
    constexpr double RR_BUDGET_MS = 1.0;
    constexpr double RR_SEED_A_BUDGET_MS = 3.0;   // seed budget while phase A runs (first RR_PHASE_A_MS of a pass)
    constexpr uint64_t RR_PHASE_A_MS = 60000;     // phase A window from the pass start
    constexpr double RR_CACHE_WRITE_MS = 1.5;     // cache write budget per tick (writing ticks run the writer only)
    constexpr size_t RR_RAD_TICK = 10;            // GetRadius reads per tick
    constexpr size_t RR_ALIVE_TICK = 64;          // round-robin alive checks per tick
    constexpr size_t RR_K2_TICK = 50;             // K2_GetActorLocation / ore reads per tick
    constexpr size_t RR_VALID_TICK = 10;          // save validations (2 km of a ship) per tick
    constexpr double RR_NEAR_M = 2000.0;          // a ship this close validates the save object of a rock (game side)
    constexpr double RR_RAD_NEAR_M = 5000.0;      // radius reads only for rocks this close to a ship or to a targeted rock

    // WHAT: seed order and seeded areas. Phase A = saves within RR_PRIORITY_CHUNKS chunks (Chebyshev) of a ship of their system.
    //       Rings above RR_RING_CAP count as RR_RING_CAP; a save in a system with no ship is RR_RING_FAR (last).
    constexpr int RR_PRIORITY_CHUNKS = 12;
    constexpr int RR_RING_CAP = 4096;
    constexpr int RR_RING_FAR = RR_RING_CAP + 1;
    constexpr size_t RR_ZONE_KEEP = 32;           // ready zones kept (oldest dropped)
    // WHAT: cache check: tolerance of a sampled position (m, 1 cm), sample size per part (nearest + random), and the rewrite threshold.
    constexpr double RR_CACHE_TOL_M = 0.01;
    constexpr size_t RR_CACHE_SAMPLE_N = 50;
    constexpr double RR_CACHE_CHANGE_FRAC = 0.005;   // a seed that changed more than 0.5% of the rocks rewrites the cache

    // WHAT: largest |coordinate| (m) a query bound is clamped to. Cell indexes then stay below ~5e8, so spans never overflow int64.
    constexpr double RR_BOUND_MAX_M = 1.0e12;
    // WHAT: true when all 3 components are finite (NaN / infinity are rejected by the queries: empty result).
    inline bool rr_finite3(const double v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
    // WHAT: v clamped to +-RR_BOUND_MAX_M (finite input; +-infinity clamps too).
    inline double rr_clampb(double v) { return std::min(std::max(v, -RR_BOUND_MAX_M), RR_BOUND_MAX_M); }

    // WHAT: distance from point c to the SEGMENT a->b (clamped to the ends).
    inline double rr_seg_point_dist(const double a[3], const double b[3], const double c[3])
    {
        const double d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const double w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const double l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        const double t = std::clamp(l2 < 1e-12 ? 0.0 : (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / l2, 0.0, 1.0);
        const double p[3] = {a[0] + d[0] * t, a[1] + d[1] * t, a[2] + d[2] * t};
        return std::sqrt((c[0] - p[0]) * (c[0] - p[0]) + (c[1] - p[1]) * (c[1] - p[1]) + (c[2] - p[2]) * (c[2] - p[2]));
    }

    // WHAT: true when centre c lies outside the box of segment a->b grown by rmax on every side (cannot be within rmax of the segment).
    // WHY: cheap first test; the distance test only runs on the rocks that pass it.
    inline bool rr_box_reject(const double a[3], const double b[3], const double c[3], double rmax)
    {
        for (int i = 0; i < 3; ++i)
        {
            const double lo = std::min(a[i], b[i]) - rmax, hi = std::max(a[i], b[i]) + rmax;
            if (c[i] < lo || c[i] > hi) return true;
        }
        return false;
    }

    // WHAT: one rock of the table. id = save object id (ObjectId). c = centre (m, absolute). rad_m valid when rad_known (0 = read and zero).
    // ore: up to 4 indexes into RrTable::kinds (ore name), -1 terminated (-1 in slot 0 = no ore known).
    // unverified: loaded from the cache and not yet checked by a pass.
    struct RrRock
    {
        int id = 0;
        int sys = 0;
        double c[3] = {0, 0, 0};
        double rad_m = 0.0;
        bool rad_known = false;
        bool alive = true;
        uint64_t destroyed_at = 0;   // tick of the kill (0 = never)
        int16_t ore[4] = {-1, -1, -1, -1};
        bool unverified = false;
    };

    // WHAT: grid cell key: system and cell index (floor(metres / RR_CHUNK_M)) per axis.
    struct RrCell
    {
        int sys; int64_t x, y, z;
        bool operator==(const RrCell& o) const { return sys == o.sys && x == o.x && y == o.y && z == o.z; }
    };
    struct RrCellHash
    {
        size_t operator()(const RrCell& c) const
        {
            uint64_t h = static_cast<uint64_t>(c.sys) * 0x9E3779B97F4A7C15ull;
            h ^= static_cast<uint64_t>(c.x) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
            h ^= static_cast<uint64_t>(c.y) * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
            h ^= static_cast<uint64_t>(c.z) * 0x27D4EB2F165667C5ull + (h << 6) + (h >> 2);
            return static_cast<size_t>(h);
        }
    };

    // WHAT: cell index of a metre position on one axis.
    inline int64_t rr_cell_idx(double v) { return static_cast<int64_t>(std::floor(v / RR_CHUNK_M)); }

    // WHAT: absolute metres of a save object position: chunk (3 ints) * RR_CHUNK_M + loc_raw / raw_per_m.
    // WHY: save Location is chunk-relative; this is the only conversion the table uses.
    inline void rr_abs_m(const int64_t ch[3], const double loc_raw[3], double raw_per_m, double out[3])
    {
        for (int i = 0; i < 3; ++i) out[i] = static_cast<double>(ch[i]) * RR_CHUNK_M + loc_raw[i] / raw_per_m;
    }

    // WHAT: chunk of a metre position (floor(m / RR_CHUNK_M) per axis).
    inline void rr_chunk_of_m(const double p[3], int64_t c[3])
    {
        for (int i = 0; i < 3; ++i) c[i] = static_cast<int64_t>(std::floor(p[i] / RR_CHUNK_M));
    }

    // WHAT: Chebyshev distance (chunks) of two chunk triples.
    inline int64_t rr_cheb(const int64_t a[3], const int64_t b[3])
    {
        int64_t m = 0;
        for (int i = 0; i < 3; ++i)
        {
            int64_t d = a[i] - b[i];
            if (d < 0) d = -d;
            if (d > m) m = d;
        }
        return m;
    }

    // WHAT: a ship of the running seed pass: system and chunk.
    struct RrSrc { int sys = 0; int64_t c[3] = {0, 0, 0}; };

    // WHAT: seed ring of a save (system sys, chunk ch): Chebyshev chunk distance to the nearest ship of the same system (capped at
    //       RR_RING_CAP). RR_RING_FAR when no ship is in the system.
    inline int rr_ring_of(int sys, const int64_t ch[3], const std::vector<RrSrc>& src)
    {
        int64_t best = -1;
        for (const RrSrc& s : src)
        {
            if (s.sys != sys) continue;
            const int64_t d = rr_cheb(ch, s.c);
            if (best < 0 || d < best) best = d;
        }
        if (best < 0) return RR_RING_FAR;
        return static_cast<int>(std::min<int64_t>(best, RR_RING_CAP));
    }

    // WHAT: order of the items by ring (counting sort; array order inside a ring). order = item indices, nearest ring first.
    //       near_n = number of items with ring <= near_r (they are the first near_n of order).
    inline void rr_order_rings(const std::vector<int>& ring, int near_r, std::vector<size_t>& order, size_t& near_n)
    {
        std::vector<size_t> cnt(static_cast<size_t>(RR_RING_FAR) + 1, 0);
        for (int r : ring) ++cnt[static_cast<size_t>(r)];
        std::vector<size_t> start(cnt.size(), 0);
        for (size_t k = 1; k < cnt.size(); ++k) start[k] = start[k - 1] + cnt[k - 1];
        order.assign(ring.size(), 0);
        for (size_t i = 0; i < ring.size(); ++i) order[start[static_cast<size_t>(ring[i])]++] = i;
        near_n = 0;
        for (int r = 0; r <= near_r && static_cast<size_t>(r) < cnt.size(); ++r) near_n += cnt[static_cast<size_t>(r)];
    }

    // WHAT: true when the registry work of this tick is over budget (elapsed_ms > budget_ms). Stages stop and resume next tick.
    inline bool rr_over_budget(double elapsed_ms, double budget_ms = RR_BUDGET_MS) { return elapsed_ms > budget_ms; }

    // WHAT: a seeded area: the chunks within r chunks (Chebyshev) of chunk c in system sys. ready = every save in it has been read (phase A done).
    struct RrZone { int sys = 0; int64_t c[3] = {0, 0, 0}; int64_t r = 0; uint64_t pass = 0; bool ready = false; };

    // WHAT: the rock table with its indexes. One instance for the game; the tests make their own.
    class RrTable
    {
    public:
        uint64_t version = 0;      // bumped on every add, remove (kill) and revive
        std::vector<RrRock> rocks;
        std::vector<std::string> kinds;   // ore names (RrRock::ore indexes this)
        std::vector<RrZone> zones;

        // WHAT: adds a rock (id > 0), or updates the centre (and ore, when given) of a known id. Returns its stable index.
        // ore: 4 slots (-1 terminated) or nullptr = none.
        // WHY: the seed reads save objects once; a second add of the same id only refreshes it and never duplicates the grid entry.
        size_t add(int id, int sys, const double c[3], const int16_t* ore = nullptr)
        {
            auto it = by_id_.find(id);
            if (it != by_id_.end())
            {
                RrRock& r = rocks[it->second];
                if (!r.alive) { r.alive = true; ++version; }
                std::copy(c, c + 3, r.c);
                if (ore) std::copy(ore, ore + 4, r.ore);
                return it->second;
            }
            const size_t i = rocks.size();
            RrRock r;
            r.id = id; r.sys = sys;
            if (ore) std::copy(ore, ore + 4, r.ore);
            std::copy(c, c + 3, r.c);
            rocks.push_back(r);
            by_id_[id] = i;
            grid_[cell_key(sys, c)].push_back(i);
            sys_list_[sys].push_back(i);
            ++version;
            return i;
        }

        // WHAT: index of a rock by id, or npos. A dead rock is found too (check alive).
        static constexpr size_t npos = static_cast<size_t>(-1);
        size_t index_of(int id) const { auto it = by_id_.find(id); return it == by_id_.end() ? npos : it->second; }
        // WHAT: true when rock id is in the table and alive.
        bool is_alive(int id) const { const size_t i = index_of(id); return i != npos && rocks[i].alive; }
        // WHAT: the rock by id, or nullptr when unknown. Check alive yourself.
        const RrRock* get(int id) const { const size_t i = index_of(id); return i == npos ? nullptr : &rocks[i]; }

        // WHAT: marks a rock destroyed at tick: alive = false, version bumped. false when unknown or already dead.
        // WHY: the single place a rock dies, so every reader sees it through is_alive.
        bool kill(int id, uint64_t tick)
        {
            const size_t i = index_of(id);
            if (i == npos || !rocks[i].alive) return false;
            RrRock& r = rocks[i];
            r.alive = false;
            r.destroyed_at = tick;
            ++version;
            return true;
        }

        // WHAT: sets the radius of a rock (m), marks it known (0 is a valid cached value).
        void set_radius(size_t i, double rad_m) { if (i < rocks.size()) { rocks[i].rad_m = rad_m; rocks[i].rad_known = true; } }

        // ---- seeded areas ----
        // WHAT: starts a pass: drops the zones that are not ready (aborted or unfinished passes), adds one zone of radius r per ship (not ready yet).
        void zones_begin(uint64_t pass, const std::vector<RrSrc>& src, int64_t r)
        {
            zones.erase(std::remove_if(zones.begin(), zones.end(), [](const RrZone& z) { return !z.ready; }), zones.end());
            for (const RrSrc& s : src)
            {
                RrZone z;
                z.sys = s.sys;
                std::copy(s.c, s.c + 3, z.c);
                z.r = r;
                z.pass = pass;
                zones.push_back(z);
            }
            while (zones.size() > RR_ZONE_KEEP) zones.erase(zones.begin());
        }
        // WHAT: marks the zones of pass ready (phase A of the pass is done: every save within their radius is read).
        void zones_ready(uint64_t pass) { for (RrZone& z : zones) if (z.pass == pass) z.ready = true; }
        // WHAT: true when the area of chunk c in system sys, r chunks around it (Chebyshev), lies inside a ready zone.
        //       False = unknown: the saves there may not be in the table yet.
        bool area_ready(int sys, const int64_t c[3], int64_t r) const
        {
            for (const RrZone& z : zones) if (z.ready && z.sys == sys && rr_cheb(z.c, c) + r <= z.r) return true;
            return false;
        }

        // ---- targeted rocks (every ship's m.rock / move target) ----
        // WHAT: registers (on = true) or drops a rock id from the targeted set. The game side alive-checks these every tick.
        void target_set(int id, bool on)
        {
            if (on) targeted_.insert(id); else targeted_.erase(id);
        }
        const std::set<int>& targeted() const { return targeted_; }

        // ---- queries (pure reads; no game calls) ----
        // WHAT: visits the rocks of system sys whose cells overlap the AABB lo..hi (metres). f(index) is called once per rock; f checks bounds itself.
        template <class F>
        void for_box(int sys, const double lo_in[3], const double hi_in[3], F f) const
        {
            // WHY: NaN bounds or lo > hi = empty box; huge / infinite bounds are clamped so the cell span cannot overflow (a hang otherwise)
            for (int k = 0; k < 3; ++k) if (std::isnan(lo_in[k]) || std::isnan(hi_in[k]) || lo_in[k] > hi_in[k]) return;
            const double lo[3] = {rr_clampb(lo_in[0]), rr_clampb(lo_in[1]), rr_clampb(lo_in[2])};
            const double hi[3] = {rr_clampb(hi_in[0]), rr_clampb(hi_in[1]), rr_clampb(hi_in[2])};
            const int64_t x0 = rr_cell_idx(lo[0]), x1 = rr_cell_idx(hi[0]);
            const int64_t y0 = rr_cell_idx(lo[1]), y1 = rr_cell_idx(hi[1]);
            const int64_t z0 = rr_cell_idx(lo[2]), z1 = rr_cell_idx(hi[2]);
            const double ncell = static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1) * static_cast<double>(z1 - z0 + 1);
            if (ncell > static_cast<double>(RR_GRID_MAX_CELLS))
            {
                auto it = sys_list_.find(sys);
                if (it != sys_list_.end()) for (size_t i : it->second) f(i);
                return;
            }
            for (int64_t x = x0; x <= x1; ++x)
                for (int64_t y = y0; y <= y1; ++y)
                    for (int64_t z = z0; z <= z1; ++z)
                    {
                        auto it = grid_.find(RrCell{sys, x, y, z});
                        if (it != grid_.end()) for (size_t i : it->second) f(i);
                    }
        }

        // WHAT: rocks alive within r_m of p (centre distance), appended to out (indices).
        size_t within(int sys, const double p[3], double r_m, std::vector<size_t>& out) const
        {
            const size_t n0 = out.size();
            if (!rr_finite3(p) || !(r_m >= 0.0)) return 0;   // NaN, infinite position or negative / NaN radius: nothing
            const double lo[3] = {p[0] - r_m, p[1] - r_m, p[2] - r_m}, hi[3] = {p[0] + r_m, p[1] + r_m, p[2] + r_m};
            const double r2 = r_m * r_m;
            for_box(sys, lo, hi, [&](size_t i)
            {
                const RrRock& r = rocks[i];
                if (!r.alive || r.sys != sys) return;
                const double d[3] = {r.c[0] - p[0], r.c[1] - p[1], r.c[2] - p[2]};
                if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= r2) out.push_back(i);
            });
            return out.size() - n0;
        }

        // WHAT: alive rocks of sys whose centre lies within margin (m) of the SEGMENT a->b (clamped to the ends), appended to out (indices).
        size_t within_seg(int sys, const double a[3], const double b[3], double margin, std::vector<size_t>& out) const
        {
            const size_t n0 = out.size();
            if (!rr_finite3(a) || !rr_finite3(b) || !(margin >= 0.0)) return 0;
            const double lo[3] = {std::min(a[0], b[0]) - margin, std::min(a[1], b[1]) - margin, std::min(a[2], b[2]) - margin};
            const double hi[3] = {std::max(a[0], b[0]) + margin, std::max(a[1], b[1]) + margin, std::max(a[2], b[2]) + margin};
            for_box(sys, lo, hi, [&](size_t i)
            {
                const RrRock& rk = rocks[i];
                if (!rk.alive || rk.sys != sys) return;
                if (rr_box_reject(a, b, rk.c, margin) || rr_seg_point_dist(a, b, rk.c) > margin) return;
                out.push_back(i);
            });
            return out.size() - n0;
        }

        // WHAT: true when rock r passes the filters of the nearest queries: alive, holds ore name index want_ore (-1 = any), id not skipped.
        static bool passes(const RrRock& r, int16_t want_ore, const std::set<int>* skip_ids)
        {
            if (!r.alive) return false;
            if (want_ore >= 0)
            {
                bool hit = false;
                for (int k = 0; k < 4 && r.ore[k] >= 0; ++k) if (r.ore[k] == want_ore) { hit = true; break; }
                if (!hit) return false;
            }
            if (skip_ids && skip_ids->count(r.id)) return false;
            return true;
        }

        // WHAT: up to n rocks of the filters, nearest first, within max_m (max_m > 0 required). out = (distance m, index).
        size_t nearest_n(int sys, int16_t want_ore, const double p[3], const std::set<int>* skip_ids, double max_m, size_t n,
                         std::vector<std::pair<double, size_t>>& out) const
        {
            out.clear();
            std::vector<size_t> cand;
            within(sys, p, max_m, cand);
            for (size_t i : cand)
            {
                const RrRock& rk = rocks[i];
                if (!passes(rk, want_ore, skip_ids)) continue;
                const double d[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                out.push_back({std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), i});
            }
            const size_t k = std::min(n, out.size());
            std::partial_sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(k), out.end(),
                              [](const std::pair<double, size_t>& a, const std::pair<double, size_t>& b) { return a.first < b.first; });
            out.resize(k);
            return k;
        }

        // WHAT: nearest rock by SURFACE distance (centre distance - radius, m) of system sys. An unread radius counts as unknown_rad (m).
        //       surf_m = that distance; skip_id (> 0) is left out. npos when none.
        //       The search is final once the best surface distance + RR_RAD_MAX_M lies inside the searched radius.
        size_t nearest_surface(int sys, const double p[3], double unknown_rad, double& surf_m, int skip_id = 0) const
        {
            std::vector<size_t> cand;
            for (double r = RR_SEARCH_START_M;; r = std::min(r * 4.0, RR_SEARCH_MAX_M))
            {
                cand.clear();
                within(sys, p, r, cand);
                size_t best = npos;
                double bs = 1e300;
                for (size_t i : cand)
                {
                    const RrRock& rk = rocks[i];
                    if (skip_id && rk.id == skip_id) continue;
                    const double d[3] = {rk.c[0] - p[0], rk.c[1] - p[1], rk.c[2] - p[2]};
                    const double rad = rk.rad_known ? std::max(0.0, rk.rad_m) : unknown_rad;
                    const double s = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - rad;
                    if (s < bs) { bs = s; best = i; }
                }
                if (best != npos && bs + RR_RAD_MAX_M <= r) { surf_m = bs; return best; }
                if (r >= RR_SEARCH_MAX_M) { if (best != npos) { surf_m = bs; return best; } return npos; }
            }
        }

        // WHAT: up to max_n alive rocks of sys with radius not known yet whose cells lie within r_m of the segment a->b (metres). Appended to out (indices, no duplicates).
        // WHY: the approach segment's rocks get their radius first (the path check uses it).
        void radius_wanted_seg(int sys, const double a[3], const double b[3], double r_m, size_t max_n, std::vector<size_t>& out) const
        {
            if (!rr_finite3(a) || !rr_finite3(b) || !(r_m >= 0.0)) return;
            const double lo[3] = {std::min(a[0], b[0]) - r_m, std::min(a[1], b[1]) - r_m, std::min(a[2], b[2]) - r_m};
            const double hi[3] = {std::max(a[0], b[0]) + r_m, std::max(a[1], b[1]) + r_m, std::max(a[2], b[2]) + r_m};
            for_box(sys, lo, hi, [&](size_t i)
            {
                const RrRock& rk = rocks[i];
                if (out.size() >= max_n || !rk.alive || rk.rad_known || rk.sys != sys) return;
                if (rr_box_reject(a, b, rk.c, r_m) || rr_seg_point_dist(a, b, rk.c) > r_m) return;
                if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
            });
        }

        // WHAT: up to max_n alive rocks of sys with radius not known yet, within r_m of any point in refs (metres). Appended to out (indices, no duplicates).
        // WHY: the radius is read only for rocks near a ship or a targeted rock.
        void radius_wanted(int sys, const std::vector<std::array<double, 3>>& refs, double r_m, size_t max_n, std::vector<size_t>& out) const
        {
            std::vector<size_t> cand;
            for (const auto& p : refs)
            {
                cand.clear();
                within(sys, p.data(), r_m, cand);
                for (size_t i : cand)
                {
                    if (out.size() >= max_n) return;
                    if (rocks[i].rad_known || std::find(out.begin(), out.end(), i) != out.end()) continue;
                    out.push_back(i);
                }
            }
        }

    private:
        std::unordered_map<int, size_t> by_id_;
        std::unordered_map<RrCell, std::vector<size_t>, RrCellHash> grid_;
        std::unordered_map<int, std::vector<size_t>> sys_list_;
        std::set<int> targeted_;

        static RrCell cell_key(int sys, const double c[3]) { return RrCell{sys, rr_cell_idx(c[0]), rr_cell_idx(c[1]), rr_cell_idx(c[2])}; }
    };

    // WHAT: kills the alive rocks that a pass did not see (seen[i] != pass). Returns the count. Called when a pass finishes.
    // WHY: a rock whose save object is gone (or not alive) is dropped only after a full pass, never on a partial one.
    inline size_t rr_kill_unseen(RrTable& t, const std::vector<uint64_t>& seen, uint64_t pass, uint64_t tick)
    {
        size_t gone = 0;
        for (size_t i = 0; i < t.rocks.size(); ++i)
        {
            if (!t.rocks[i].alive) continue;
            if (i < seen.size() && seen[i] == pass) continue;
            if (t.kill(t.rocks[i].id, tick)) ++gone;
        }
        return gone;
    }

    // WHAT: world tracker. id = identity of the loaded world (0 = unreadable), read each tick. LOADED = a different readable world than before
    //       (menu -> game, save load): drop the object pointers. LOST = unreadable for bad_max ticks in a row (debounce): same drop, world not ok.
    //       Same id, other system (a warp) = NONE.
    struct RrWorldTrack { uint64_t id = 0; int bad = 0; bool ok = false; };
    enum RrWorldEv { RWE_NONE = 0, RWE_LOADED = 1, RWE_LOST = 2 };
    inline RrWorldEv rr_world_step(RrWorldTrack& t, uint64_t id, int bad_max)
    {
        if (id != 0)
        {
            t.bad = 0;
            t.ok = true;
            if (id == t.id) return RWE_NONE;
            t.id = id;
            return RWE_LOADED;
        }
        if (t.bad < bad_max) ++t.bad;
        if (t.bad == bad_max && t.ok) { t.ok = false; t.id = 0; return RWE_LOST; }
        return RWE_NONE;
    }

    // ---- disk cache: format and checks (no file I/O here; the game side reads and writes the bytes) ----
    constexpr uint32_t RR_CACHE_MAGIC = 0x52474352;   // bytes 'R' 'C' 'G' 'R' (little endian)
    constexpr uint32_t RR_CACHE_VER = 2;
    constexpr size_t RR_CACHE_HDR = 48;               // header bytes
    constexpr size_t RR_CACHE_REC = 48;               // record bytes
    constexpr uint64_t RR_CACHE_FNV0 = 1469598103934665603ull;   // FNV-1a offset basis

    // WHAT: header fields. exe_size = size of the game exe the table was seeded with; save_n = live save objects at seed; rock_n = records;
    //       kinds_off = byte offset of the ore name table; sum = FNV-1a of every byte after the header.
    struct RrCacheHdr { uint64_t exe_size = 0, save_n = 0, rock_n = 0, kinds_off = 0, sum = 0; };
    // WHAT: one record: rock id, system, centre (m, absolute), radius (m, -1 = not read), ore name indexes (-1 terminated, -1 = none).
    struct RrCacheRec { int32_t id = 0, sys = 0; double c[3] = {0, 0, 0}; float rad = -1.0f; int16_t ore[4] = {-1, -1, -1, -1}; };
    struct RrCacheData { RrCacheHdr h; std::vector<RrCacheRec> recs; std::vector<std::string> kinds; };

    // WHAT: FNV-1a over n bytes, continuing hash h.
    inline uint64_t rr_cache_fnv(uint64_t h, const uint8_t* p, size_t n)
    {
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }
    template <class T> inline void rr_put(uint8_t* p, T v) { std::memcpy(p, &v, sizeof(T)); }
    template <class T> inline T rr_get(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof(T)); return v; }

    // WHAT: writes the 48 header bytes to p (magic, version, fields).
    inline void rr_cache_put_hdr(uint8_t* p, const RrCacheHdr& h)
    {
        rr_put<uint32_t>(p, RR_CACHE_MAGIC);
        rr_put<uint32_t>(p + 4, RR_CACHE_VER);
        rr_put<uint64_t>(p + 8, h.exe_size);
        rr_put<uint64_t>(p + 16, h.save_n);
        rr_put<uint64_t>(p + 24, h.rock_n);
        rr_put<uint64_t>(p + 32, h.kinds_off);
        rr_put<uint64_t>(p + 40, h.sum);
    }
    // WHAT: writes one 48-byte record to p (bytes 44..47 reserved, zero).
    inline void rr_cache_put_rec(uint8_t* p, const RrCacheRec& r)
    {
        rr_put<int32_t>(p, r.id);
        rr_put<int32_t>(p + 4, r.sys);
        rr_put<double>(p + 8, r.c[0]);
        rr_put<double>(p + 16, r.c[1]);
        rr_put<double>(p + 24, r.c[2]);
        rr_put<float>(p + 32, r.rad);
        for (int k = 0; k < 4; ++k) rr_put<int16_t>(p + 36 + 2 * k, r.ore[k]);
        rr_put<int32_t>(p + 44, 0);
    }
    // WHAT: appends the ore name table: u32 count, then per name u16 length and the bytes (names cut at 65535 bytes).
    inline void rr_cache_put_kinds(std::vector<uint8_t>& out, const std::vector<std::string>& kinds)
    {
        const size_t at = out.size();
        out.resize(at + 4);
        rr_put<uint32_t>(&out[at], static_cast<uint32_t>(kinds.size()));
        for (const std::string& k : kinds)
        {
            const size_t n = std::min<size_t>(k.size(), 65535);
            const size_t b = out.size();
            out.resize(b + 2 + n);
            rr_put<uint16_t>(&out[b], static_cast<uint16_t>(n));
            if (n) std::memcpy(&out[b + 2], k.data(), n);
        }
    }

    // WHAT: a whole cache image in memory (tests; the game side streams the same bytes). d.h.exe_size and d.h.save_n are taken as given.
    inline std::vector<uint8_t> rr_cache_encode(const RrCacheData& d)
    {
        std::vector<uint8_t> out(RR_CACHE_HDR + d.recs.size() * RR_CACHE_REC);
        for (size_t i = 0; i < d.recs.size(); ++i) rr_cache_put_rec(&out[RR_CACHE_HDR + i * RR_CACHE_REC], d.recs[i]);
        const size_t kofs = out.size();
        rr_cache_put_kinds(out, d.kinds);
        RrCacheHdr h = d.h;
        h.rock_n = d.recs.size();
        h.kinds_off = kofs;
        h.sum = rr_cache_fnv(RR_CACHE_FNV0, out.data() + RR_CACHE_HDR, out.size() - RR_CACHE_HDR);
        rr_cache_put_hdr(out.data(), h);
        return out;
    }

    // WHAT: parses a cache image. false + why when it must be ignored: short, magic, version (v1 included), exe size (or 0), layout, checksum,
    //       name table, or a record that is not sane (id/sys <= 0, non-finite centre, bad radius, ore index out of range or not -1 terminated).
    inline bool rr_cache_parse(const uint8_t* p, size_t n, uint64_t exe_size, RrCacheData& out, std::string& why)
    {
        out = RrCacheData();
        if (n < RR_CACHE_HDR) { why = "short file"; return false; }
        if (rr_get<uint32_t>(p) != RR_CACHE_MAGIC) { why = "magic"; return false; }
        if (rr_get<uint32_t>(p + 4) != RR_CACHE_VER) { why = "version"; return false; }
        RrCacheHdr& h = out.h;
        h.exe_size = rr_get<uint64_t>(p + 8);
        h.save_n = rr_get<uint64_t>(p + 16);
        h.rock_n = rr_get<uint64_t>(p + 24);
        h.kinds_off = rr_get<uint64_t>(p + 32);
        h.sum = rr_get<uint64_t>(p + 40);
        if (exe_size == 0 || h.exe_size != exe_size) { why = "exe size changed"; return false; }
        if (h.rock_n > (n - RR_CACHE_HDR) / RR_CACHE_REC) { why = "rock count"; return false; }
        if (h.kinds_off != RR_CACHE_HDR + h.rock_n * RR_CACHE_REC || h.kinds_off > n) { why = "layout"; return false; }
        if (rr_cache_fnv(RR_CACHE_FNV0, p + RR_CACHE_HDR, n - RR_CACHE_HDR) != h.sum) { why = "checksum"; return false; }
        size_t q = static_cast<size_t>(h.kinds_off);
        if (q + 4 > n) { why = "name table"; return false; }
        const uint32_t nk = rr_get<uint32_t>(p + q);
        q += 4;
        for (uint32_t k = 0; k < nk; ++k)
        {
            if (q + 2 > n) { why = "name table"; return false; }
            const size_t len = rr_get<uint16_t>(p + q);
            q += 2;
            if (q + len > n) { why = "name table"; return false; }
            out.kinds.emplace_back(reinterpret_cast<const char*>(p + q), len);
            q += len;
        }
        if (q != n) { why = "trailing bytes"; return false; }
        out.recs.resize(static_cast<size_t>(h.rock_n));
        for (size_t i = 0; i < out.recs.size(); ++i)
        {
            const uint8_t* r = p + RR_CACHE_HDR + i * RR_CACHE_REC;
            RrCacheRec& c = out.recs[i];
            c.id = rr_get<int32_t>(r);
            c.sys = rr_get<int32_t>(r + 4);
            c.c[0] = rr_get<double>(r + 8);
            c.c[1] = rr_get<double>(r + 16);
            c.c[2] = rr_get<double>(r + 24);
            c.rad = rr_get<float>(r + 32);
            bool ore_ok = true;
            bool ended = false;
            for (int k = 0; k < 4; ++k)
            {
                c.ore[k] = rr_get<int16_t>(r + 36 + 2 * k);
                if (c.ore[k] == -1) ended = true;
                else if (ended || c.ore[k] < 0 || c.ore[k] >= static_cast<int32_t>(nk)) ore_ok = false;
            }
            const bool fin = std::isfinite(c.c[0]) && std::isfinite(c.c[1]) && std::isfinite(c.c[2]);
            const bool rad_ok = c.rad == -1.0f || (std::isfinite(c.rad) && c.rad >= 0.0f);
            if (c.id <= 0 || c.sys <= 0 || !fin || !rad_ok || !ore_ok) { why = "record"; return false; }
        }
        return true;
    }

    // WHAT: verdict of a loaded cache after its sample check (RCV_TRUST: keep all, skip the seed; RCV_MERGE: keep the matching rocks, read the
    //       new ones; RCV_RESEED: read every cached rock again). save_cache = save count of the cache, save_live = live saves now,
    //       sample_n = rocks compared, sample_bad = rocks that differ (position > RR_CACHE_TOL_M or ore name) or could not be read.
    enum RrCacheVerdict { RCV_TRUST = 0, RCV_MERGE = 1, RCV_RESEED = 2 };
    inline RrCacheVerdict rr_cache_verdict(uint64_t save_cache, uint64_t save_live, size_t sample_n, size_t sample_bad)
    {
        if (sample_n == 0 || sample_bad > 0) return RCV_RESEED;
        return save_cache == save_live ? RCV_TRUST : RCV_MERGE;
    }
    // WHAT: true when two centres (m) are within RR_CACHE_TOL_M on every axis.
    inline bool rr_same_pos(const double a[3], const double b[3])
    {
        return std::fabs(a[0] - b[0]) <= RR_CACHE_TOL_M && std::fabs(a[1] - b[1]) <= RR_CACHE_TOL_M && std::fabs(a[2] - b[2]) <= RR_CACHE_TOL_M;
    }
}
