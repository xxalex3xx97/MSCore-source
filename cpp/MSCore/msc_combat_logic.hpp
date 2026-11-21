// FILE: cpp/MSCore/msc_combat_logic.hpp
// WHAT: pure combat helpers: chunk+raw delta in metres, enemy row filter (radius, sort, cap), fire-command edge trigger. No game headers.
// USED BY: msc_combat.hpp (later), tests/test_combat_logic.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace
{
    constexpr double kCmbChunkM = 2048.0;    // UNVERIFIED chunk edge (m)
    constexpr double kCmbRawPerM = 65536.0;  // raw per metre (verified in Fleet g_raw_per_m)
    // C API row of MSCore_enemies_near, 48 bytes. rel_m = enemy minus reference ship, metres, world axes (chunk axes). Fleet mirrors this struct.
    struct MsEnemy { int32_t id; int32_t sys; int32_t hull; int32_t pad; double d_m; double rel_m[3]; };
    static_assert(sizeof(MsEnemy) == 48, "MsEnemy layout is part of the C API");
    // WHAT: delta b minus a in metres from chunk + chunk-relative raw loc; returns its length.
    inline double cmb_delta_m(const int64_t ca[3], const double la[3], const int64_t cb[3], const double lb[3], double out[3])
    {
        double s = 0;
        for (int i = 0; i < 3; ++i) { out[i] = static_cast<double>(cb[i] - ca[i]) * kCmbChunkM + (lb[i] - la[i]) / kCmbRawPerM; s += out[i] * out[i]; }
        return std::sqrt(s);
    }
    // WHAT: rows with d_m <= radius_m, sorted by d_m then id, cut to max. max <= 0 -> empty.
    inline std::vector<MsEnemy> cmb_filter(std::vector<MsEnemy> rows, double radius_m, int max)
    {
        std::vector<MsEnemy> o;
        for (const MsEnemy& r : rows) if (r.d_m <= radius_m) o.push_back(r);
        std::sort(o.begin(), o.end(), [](const MsEnemy& a, const MsEnemy& b) { return a.d_m < b.d_m || (a.d_m == b.d_m && a.id < b.id); });
        if (max <= 0) o.clear(); else if (static_cast<int>(o.size()) > max) o.resize(static_cast<size_t>(max));
        return o;
    }
    // WHAT: edge trigger for fire commands. last: -1 never sent, 0 off sent, 1 on sent. true = a command must be sent.
    inline bool cmb_fire_send(int last, bool on) { return last != (on ? 1 : 0); }
}
