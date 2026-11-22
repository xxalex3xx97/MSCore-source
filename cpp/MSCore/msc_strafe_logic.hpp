// FILE: cpp/MSCore/msc_strafe_logic.hpp
// WHAT: pure helpers of the strafe module: value -> sign, per-ship held state, watchdog expiry. No game headers.
// USED BY: msc_cmd.hpp, msc_strafe.hpp, tests/test_strafe_logic.cpp.
#pragma once

inline constexpr double kStrafeThreshold = 0.3;                  // |value| above this counts as a push (the game ignores magnitude)
inline constexpr unsigned long long kStrafeHoldMs = 10000;       // held longer than this without a refresh -> auto zero

// value -1..1 -> -1, 0, +1
inline int str_sign(double v) { return v > kStrafeThreshold ? 1 : (v < -kStrafeThreshold ? -1 : 0); }

struct StrafeSt
{
    int s[2] = {0, 0};                 // [0] right(+)/left(-) = game Y slot, [1] up(+)/down(-) = game X slot
    unsigned long long at = 0;         // ms of the last refresh while non-zero
    bool nz() const { return s[0] != 0 || s[1] != 0; }
};

// sets axis to v (-1/0/+1). Returns true when a command must be sent: the state changed, or it is non-zero (a refresh).
inline bool str_update(StrafeSt& st, int axis, int v, unsigned long long now)
{
    if (axis < 0 || axis > 1) return false;
    const bool changed = st.s[axis] != v;
    st.s[axis] = v;
    if (st.nz()) st.at = now;
    return changed || st.nz();
}

// both axes 0. Returns true when either was non-zero (a zero command must be sent).
inline bool str_zero(StrafeSt& st)
{
    const bool was = st.nz();
    st.s[0] = st.s[1] = 0;
    return was;
}

inline bool str_expired(const StrafeSt& st, unsigned long long now, unsigned long long hold_ms = kStrafeHoldMs)
{
    return st.nz() && now - st.at > hold_ms;
}

// logical axis (0 right, 1 up) -> StrafeImpulse component (0 = X, 1 = Y). Live-verified: right is Y, up is X.
inline int str_slot(int axis) { return axis == 0 ? 1 : 0; }

// fills the game components {X, Y} from the held state: X = up, Y = right
inline void str_components(const StrafeSt& st, int out[2]) { out[str_slot(0)] = st.s[0]; out[str_slot(1)] = st.s[1]; }
