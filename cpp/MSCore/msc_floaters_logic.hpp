// FILE: cpp/MSCore/msc_floaters_logic.hpp
// WHAT: pure helpers of the floaters module: the C API row (MsFloaterSec, 40 bytes), sector box test, point-to-sector binning. No game headers.
// USED BY: msc_floaters.hpp, tests/test_floaters_logic.cpp.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
    // C API row of MSCore_floaters, 40 bytes. One row per sector that holds at least one floater. ch = sector chunk location.
    struct MsFloaterSec { int32_t id; int32_t sys; int32_t count; int32_t pad; int64_t ch[3]; };
    static_assert(sizeof(MsFloaterSec) == 40, "MsFloaterSec layout is part of the C API");

    struct FlSecBox { int32_t id; int32_t sys; int64_t ch[3]; int64_t he[3]; };
    struct FlPt { int32_t sys; int64_t ch[3]; };

    inline bool fl_in_box(const FlSecBox& s, const FlPt& p)
    {
        if (s.sys != p.sys) return false;
        for (int i = 0; i < 3; ++i) { const int64_t d = p.ch[i] - s.ch[i]; if ((d < 0 ? -d : d) > s.he[i]) return false; }
        return true;
    }

    inline double fl_d2(const FlSecBox& s, const FlPt& p)
    {
        double a = 0;
        for (int i = 0; i < 3; ++i) { const double d = double(p.ch[i] - s.ch[i]); a += d * d; }
        return a;
    }

    // each point goes to the containing sector with the smallest squared chunk distance (ties: lowest id). Rows only for count > 0, count desc then id asc. unbound = points in no sector.
    inline std::vector<MsFloaterSec> fl_bin(const std::vector<FlSecBox>& secs, const std::vector<FlPt>& pts, int& unbound)
    {
        std::vector<int> cnt(secs.size(), 0);
        unbound = 0;
        for (const FlPt& p : pts)
        {
            int best = -1;
            double bd = 0;
            for (size_t i = 0; i < secs.size(); ++i)
            {
                if (!fl_in_box(secs[i], p)) continue;
                const double d = fl_d2(secs[i], p);
                if (best < 0 || d < bd || (d == bd && secs[i].id < secs[size_t(best)].id)) { best = int(i); bd = d; }
            }
            if (best < 0) ++unbound; else ++cnt[size_t(best)];
        }
        std::vector<MsFloaterSec> o;
        for (size_t i = 0; i < secs.size(); ++i)
        {
            if (cnt[i] <= 0) continue;
            MsFloaterSec r{};
            r.id = secs[i].id; r.sys = secs[i].sys; r.count = cnt[i];
            for (int k = 0; k < 3; ++k) r.ch[k] = secs[i].ch[k];
            o.push_back(r);
        }
        std::sort(o.begin(), o.end(), [](const MsFloaterSec& a, const MsFloaterSec& b) { return a.count > b.count || (a.count == b.count && a.id < b.id); });
        return o;
    }
}
