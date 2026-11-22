// FILE: cpp/MSCore/msc_threat_logic.hpp
// WHAT: pure threat helpers: classify one TargettedBy entry (none / hostile / far) and add it to the running counts. No game headers.
// USED BY: msc_threat.hpp, tests/test_threat_logic.cpp.
#pragma once
namespace
{
    enum ThrKind { THR_NONE = 0, THR_HOSTILE = 1, THR_FAR = 2 };
    struct ThrCount { int hostile = 0; int far_n = 0; double nearest_m = -1.0; };
    // WHAT: one entry. live = resolved to a living object, same_side = shares the ship's owner or faction, object_type = array element is an object pointer,
    //       dist_m < 0 = distance unreadable. Not an object array -> NONE. Dead/unresolved -> FAR. Same side -> NONE.
    //       Live with unreadable distance counts HOSTILE (conservative, as Fleet did). Else HOSTILE when dist_m <= range_m, FAR beyond.
    inline ThrKind thr_kind(bool live, bool same_side, bool object_type, double dist_m, double range_m)
    {
        if (!object_type) return THR_NONE;
        if (!live) return THR_FAR;
        if (same_side) return THR_NONE;
        return (dist_m < 0.0 || dist_m <= range_m) ? THR_HOSTILE : THR_FAR;
    }
    // WHAT: add an entry of kind k at dist_m to c. nearest_m = smallest readable distance of a HOSTILE entry, -1 when none.
    inline void thr_add(ThrCount& c, ThrKind k, double dist_m)
    {
        if (k == THR_FAR) ++c.far_n;
        else if (k == THR_HOSTILE)
        {
            ++c.hostile;
            if (dist_m >= 0.0 && (c.nearest_m < 0.0 || dist_m < c.nearest_m)) c.nearest_m = dist_m;
        }
    }
}
