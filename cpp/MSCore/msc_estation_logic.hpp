// FILE: cpp/MSCore/msc_estation_logic.hpp
// WHAT: pure helpers of the enemy-station module: the C API row (MsEnemyStation, 64 bytes), row flags, construction percent, row filter. No game headers.
// USED BY: msc_estation.hpp, tests/test_estation_logic.cpp.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
    // C API row of MSCore_enemy_stations, 64 bytes. rel_m = station minus reference ship, metres, world axes (same as MsEnemy.rel_m). Fleet mirrors this struct.
    // flags: bit0 alive (hull > 0), bit1 hull unreadable (hull = -1, not targetable), bit2 radius known (else radius_m = -1). build_pct = 100 * ConstructionProgress / MaxConstructionProgress, -1 unknown.
    struct MsEnemyStation { int32_t id; int32_t sys; int32_t hull; int32_t max_hull; int32_t flags; int32_t build_pct; double d_m; double rel_m[3]; double radius_m; };
    static_assert(sizeof(MsEnemyStation) == 64, "MsEnemyStation layout is part of the C API");

    constexpr int32_t EST_F_ALIVE = 1, EST_F_HULL_UNREAD = 2, EST_F_RADIUS = 4;

    inline int32_t est_flags(bool alive, bool hull_unreadable, bool radius_known)
    {
        return (alive ? EST_F_ALIVE : 0) | (hull_unreadable ? EST_F_HULL_UNREAD : 0) | (radius_known ? EST_F_RADIUS : 0);
    }

    // WHAT: construction percent 0..100 from the two raw numbers; -1 when either is unknown or max <= 0.
    inline int32_t est_build_pct(int64_t cur, int64_t mx)
    {
        if (cur < 0 || mx <= 0) return -1;
        const int64_t p = cur * 100 / mx;
        return static_cast<int32_t>(p > 100 ? 100 : p);
    }

    // WHAT: hull as sent: Health clamped to int32, -1 when unreadable.
    inline int32_t est_hull_value(bool readable, int64_t hp)
    {
        if (!readable) return -1;
        if (hp > 2147483647LL) return 2147483647;
        if (hp < 0) return 0;
        return static_cast<int32_t>(hp);
    }

    // WHAT: rows within radius_m, nearest first (ties lowest id), at most max rows. Dead rows (hull read and <= 0) are dropped.
    inline std::vector<MsEnemyStation> est_filter(std::vector<MsEnemyStation> rows, double radius_m, int max)
    {
        std::vector<MsEnemyStation> o;
        for (const MsEnemyStation& r : rows)
        {
            if (r.d_m > radius_m) continue;
            if (!(r.flags & EST_F_HULL_UNREAD) && r.hull <= 0) continue;
            o.push_back(r);
        }
        std::sort(o.begin(), o.end(), [](const MsEnemyStation& a, const MsEnemyStation& b) { return a.d_m < b.d_m || (a.d_m == b.d_m && a.id < b.id); });
        if (max >= 0 && static_cast<int>(o.size()) > max) o.resize(static_cast<size_t>(max));
        return o;
    }
}
