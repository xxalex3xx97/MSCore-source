// FILE: cpp/MSCore/msc_capture_logic.hpp
// WHAT: pure helpers of the input capture probe: change detection with elapsed ms, per-second command counter. No game headers.
// USED BY: msc_capture.hpp, tests/test_capture_logic.cpp.
#pragma once
#include <cstring>

struct CapState
{
    bool has = false;
    long long v[8] = {};
    unsigned long long last = 0;   // ms of the last change
};

// true when v[0..n) differs from the stored values (or there are none yet); dt = ms since the previous change (0 for the first). Stores v on change.
inline bool cap_changed(CapState& s, const long long* v, int n, unsigned long long now, long long& dt)
{
    if (n < 0 || n > 8) return false;
    const bool ch = !s.has || std::memcmp(s.v, v, sizeof(long long) * n) != 0;
    dt = s.has ? static_cast<long long>(now - s.last) : 0;
    if (ch) { std::memcpy(s.v, v, sizeof(long long) * n); s.has = true; s.last = now; }
    return ch;
}

struct CapRate
{
    unsigned long long start = 0;
    int n = 0;
};

inline void cap_rate_add(CapRate& r) { ++r.n; }

// true once per >= 1000 ms: n = commands counted since the last report. The first call only starts the window.
inline bool cap_rate_tick(CapRate& r, unsigned long long now, int& n)
{
    if (r.start == 0) { r.start = now ? now : 1; return false; }
    if (now - r.start < 1000) return false;
    n = r.n; r.n = 0; r.start = now;
    return true;
}
