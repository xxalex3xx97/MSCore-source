// FILE: cpp/MSCore/msc_simrate.hpp
// WHAT: shared sim-rate state: BalRate g_bal (filled by msc_ballistics.hpp) and sim_tps_conv(), the measured sim ticks per second (fallback kBalFallbackTps).
// WHY: msc_combat.hpp converts per-frame values to per-second. It cannot include msc_ballistics.hpp (that one needs msc_combat.hpp helpers), so the state lives here.
// SAFETY: no game access. g_bal is read and written only under g_bal_mx.
// USED BY: msc_combat.hpp (sim_tps_conv), msc_ballistics.hpp (g_bal, g_bal_mx, bal_now_s).
#pragma once
#include <chrono>
#include <mutex>
#include "msc_ballistics_logic.hpp"

namespace
{
    BalRate g_bal;        // sim-rate estimator, samples from ship_guns_read (game thread)
    std::mutex g_bal_mx;

    // WHAT: wall clock in seconds (steady clock), the time base of g_bal.
    inline double bal_now_s()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // WHAT: sim ticks per wall second: measured median when fresh, else kBalFallbackTps. Pure read.
    inline double sim_tps_conv()
    {
        const double now = bal_now_s();
        std::lock_guard<std::mutex> lk(g_bal_mx);
        return bal_tps_conv(g_bal, now);
    }
}
