// FILE: cpp/MSCore/msc_stshield_logic.hpp
// WHAT: pure helpers of the stshield module: the C API row (MsStationZone, 48 bytes) and the row filter. No game headers.
// USED BY: msc_stshield.hpp, tests/test_stshield_logic.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    // C API row of MSCore_station_zones, 48 bytes. pos = station centre, absolute metres (same frame as rockreg: chunk * RR_CHUNK_M + loc / 65536).
    // extent_m = station extent in metres, -1 unknown. range_m = station shield range in metres, -1 unknown (not readable until the probe shows the field).
    struct MsStationZone { int32_t id; int32_t sys; double pos[3]; double extent_m; double range_m; };
    static_assert(sizeof(MsStationZone) == 48, "MsStationZone layout is part of the C API");

    // WHAT: rows with id > 0, sys > 0 and a finite position, ordered by id, at most max rows (max < 0 = all). Unknown extent/range are normalised to -1.
    inline std::vector<MsStationZone> sz_filter(const std::vector<MsStationZone>& rows, int max)
    {
        std::vector<MsStationZone> o;
        for (MsStationZone r : rows)
        {
            if (r.id <= 0 || r.sys <= 0) continue;
            if (!std::isfinite(r.pos[0]) || !std::isfinite(r.pos[1]) || !std::isfinite(r.pos[2])) continue;
            if (!std::isfinite(r.extent_m) || r.extent_m <= 0) r.extent_m = -1;
            if (!std::isfinite(r.range_m) || r.range_m <= 0) r.range_m = -1;
            o.push_back(r);
        }
        std::sort(o.begin(), o.end(), [](const MsStationZone& a, const MsStationZone& b) { return a.id < b.id; });
        if (max >= 0 && static_cast<int>(o.size()) > max) o.resize(static_cast<size_t>(max));
        return o;
    }
}
