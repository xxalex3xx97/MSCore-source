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
    unsigned long long at = 0;         // ms of the last refresh while non-zero (watchdog)
    unsigned long long sent = 0;       // ms of the last command sent for this state (keep-alive)
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

// MAPPING TABLE (one place to swap): StrafeImpulse component slot (0 = X, 1 = Y, 2 = Z) of the logical right/left and up/down axes.
// Live test (command): right -> Y (+0x54), up -> X (+0x50). The player's ImpulseInput struct differs (Y right, Z up): see msc_strafe.hpp.
inline constexpr int kStrafeSlotRight = 1;
inline constexpr int kStrafeSlotUp = 0;
inline constexpr long long kStrafeRaw = 65536;                   // fixed-point 1.0
inline constexpr unsigned long long kStrafeKeepMs = 500;         // resend a held strafe at least this often (MSCore_strafe_tick)

inline int str_slot(int axis) { return axis == 0 ? kStrafeSlotRight : kStrafeSlotUp; }

// sign -1/0/+1 -> raw fixed-point value written into the command: exactly +-65536, 0 for 0
inline long long str_raw(int sign) { return sign > 0 ? kStrafeRaw : (sign < 0 ? -kStrafeRaw : 0); }

// raw values for the 3 StrafeImpulse components {X, Y, Z} from the logical right / up signs
inline void str_slots(int sright, int sup, long long out[3])
{
    out[0] = out[1] = out[2] = 0;
    out[kStrafeSlotRight] = str_raw(sright);
    out[kStrafeSlotUp] = str_raw(sup);
}

// non-zero state not sent for >= kStrafeKeepMs: resend
inline bool str_needs_resend(const StrafeSt& st, unsigned long long now, unsigned long long keep_ms = kStrafeKeepMs)
{
    return st.nz() && now - st.sent >= keep_ms;
}
