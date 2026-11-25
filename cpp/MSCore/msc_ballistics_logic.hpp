// FILE: cpp/MSCore/msc_ballistics_logic.hpp
// WHAT: pure ballistics helpers: sim-rate estimator (median of ticks per wall second from a sim-frame stamp) and aim gun group pick. No game headers.
// WHY: Fleet needs bullet flight time and the sim tick rate; both are learned here so the game names stay in MSCore.
// USED BY: msc_simrate.hpp, msc_ballistics.hpp, tests/test_ballistics_logic.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>
namespace
{
    constexpr double kBalMinSpanS = 3.0;        // shortest sample span used for one rate
    constexpr double kBalMaxCallGapS = 0.5;     // longer gap between calls = stale series, restart
    constexpr double kBalTpsMin = 5.0;          // plausible sim ticks per second
    constexpr double kBalTpsMax = 90.0;
    constexpr double kBalOutlierFrac = 0.35;    // reject a rate further than this fraction from the ring median
    constexpr size_t kBalRing = 8;              // accepted rates kept
    constexpr double kBalEvKeepS = 12.0;        // stamp events kept this long
    constexpr double kBalPauseCheckS = 0.5;    // pause state re-read at most this often (caller)
    constexpr double kBalFreshS = 30.0;         // measured rate older than this is not used
    constexpr double kBalFallbackTps = 22.4;    // measured 2026-11-23 (cannon LastShotTime rate)

    // WHAT: sim-rate estimator state. ev = (wall s, stamp) series, ring = accepted rates (ticks/s), last_f = last stamp seen.
    struct BalRate
    {
        std::deque<std::pair<double, int64_t>> ev;
        std::vector<double> ring;
        double last_call = -1e9;
        double last_update = -1e9;
        int64_t last_f = -1;
    };

    // WHAT: forget the series and the rates (not last_call).
    inline void bal_reset(BalRate& r)
    {
        r.ev.clear();
        r.ring.clear();
        r.last_f = -1;
        r.last_update = -1e9;
    }

    // WHAT: one sample (now_s wall seconds, stamp = sim frame count from LastShotTime, paused = game paused).
    // Pause resets. Stamp < 0 is ignored. A gap between calls over kBalMaxCallGapS restarts the series (ring kept).
    // A stamp decrease (new save / reload) resets. Equal stamp = nothing new. Rate = frames over the newest span >= kBalMinSpanS.
    // Rate outside [kBalTpsMin, kBalTpsMax] or more than kBalOutlierFrac off the ring median is rejected.
    inline void bal_sample(BalRate& r, double now_s, int64_t stamp, bool paused)
    {
        if (paused) { bal_reset(r); r.last_call = now_s; return; }
        if (stamp < 0) return;
        const double gap = now_s - r.last_call;
        r.last_call = now_s;
        if (r.last_f < 0 || gap > kBalMaxCallGapS) { r.last_f = stamp; r.ev.clear(); return; }
        if (stamp < r.last_f) { bal_reset(r); r.last_f = stamp; r.last_call = now_s; return; }
        if (stamp == r.last_f) return;
        r.last_f = stamp;
        r.ev.emplace_back(now_s, stamp);
        while (!r.ev.empty() && now_s - r.ev.front().first > kBalEvKeepS) r.ev.pop_front();
        const size_t newest = r.ev.size() - 1;
        size_t j = 0;
        bool found = false;
        for (size_t k = newest; k-- > 0;)
        {
            if (now_s - r.ev[k].first >= kBalMinSpanS) { j = k; found = true; break; }
        }
        if (!found) return;
        const double span = now_s - r.ev[j].first;
        const double rate = static_cast<double>(stamp - r.ev[j].second) / span;
        if (rate < kBalTpsMin || rate > kBalTpsMax) return;
        if (r.ring.size() >= 3)
        {
            const double med = [&] {
                std::vector<double> s = r.ring;
                std::sort(s.begin(), s.end());
                const size_t m = s.size() / 2;
                return s.size() % 2 ? s[m] : 0.5 * (s[m - 1] + s[m]);
            }();
            if (std::fabs(rate - med) > kBalOutlierFrac * med) return;
        }
        r.ring.push_back(rate);
        if (r.ring.size() > kBalRing) r.ring.erase(r.ring.begin());
        r.last_update = now_s;
    }

    // WHAT: median of the accepted rates (ticks per second), 0 when none. n = number of rates used.
    inline double bal_tps(const BalRate& r, int* n = nullptr)
    {
        if (n) *n = static_cast<int>(r.ring.size());
        if (r.ring.empty()) return 0.0;
        std::vector<double> s = r.ring;
        std::sort(s.begin(), s.end());
        const size_t m = s.size() / 2;
        return s.size() % 2 ? s[m] : 0.5 * (s[m - 1] + s[m]);
    }

    // WHAT: seconds since the last accepted rate, 1e9 when never.
    inline double bal_age(const BalRate& r, double now_s)
    {
        if (r.last_update <= -1e8) return 1e9;
        return now_s - r.last_update;
    }

    // WHAT: rate for conversions: measured median when at least 3 rates and fresh (<= kBalFreshS), else kBalFallbackTps.
    inline double bal_tps_conv(const BalRate& r, double now_s)
    {
        return (r.ring.size() >= 3 && bal_age(r, now_s) <= kBalFreshS) ? bal_tps(r) : kBalFallbackTps;
    }

    // WHAT: one weapon for the aim group. speed_mpf = bullet speed metres per frame, life_frames = BulletLifetime frames,
    //       tbs_frames = TimeBetweenShots frames (0 unknown), primary = bAddToPrimaryGroup.
    struct BalGun { double speed_mpf; double life_frames; double tbs_frames; bool primary; };

    // WHAT: aim group. ok = at least one kept gun. Kept = primary and speed > 0 and life > 0.
    //       Groups by exact speed (|a-b| < 1e-6); winner = most guns, tie -> lower speed. speed/life/tbs/n from the winner.
    //       range_m = speed * life (metres, tick independent). distinct = number of speed groups. smin/smax over all kept guns.
    struct BalPick { bool ok = false; double speed_mpf = 0, life_frames = 0, tbs_frames = 0, range_m = 0, smin = 0, smax = 0; int n = 0, distinct = 0; };

    inline BalPick bal_pick(const std::vector<BalGun>& guns)
    {
        BalPick p;
        struct Grp { double speed; int count; double life; double tbs; };
        std::vector<Grp> groups;
        for (const BalGun& g : guns)
        {
            if (!g.primary || g.speed_mpf <= 0 || g.life_frames <= 0) continue;
            if (!p.ok) { p.smin = p.smax = g.speed_mpf; }
            p.smin = std::min(p.smin, g.speed_mpf);
            p.smax = std::max(p.smax, g.speed_mpf);
            p.ok = true;
            bool found = false;
            for (Grp& gr : groups)
            {
                if (std::fabs(gr.speed - g.speed_mpf) < 1e-6) { ++gr.count; found = true; break; }
            }
            if (!found) groups.push_back({ g.speed_mpf, 1, g.life_frames, g.tbs_frames });
        }
        if (!p.ok) return p;
        const Grp* best = nullptr;
        for (const Grp& gr : groups)
        {
            if (!best || gr.count > best->count || (gr.count == best->count && gr.speed < best->speed)) best = &gr;
        }
        p.speed_mpf = best->speed;
        p.life_frames = best->life;
        p.tbs_frames = best->tbs;
        p.n = best->count;
        p.distinct = static_cast<int>(groups.size());
        p.range_m = p.speed_mpf * p.life_frames;
        return p;
    }
}
