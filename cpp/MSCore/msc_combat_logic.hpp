// FILE: cpp/MSCore/msc_combat_logic.hpp
// WHAT: pure combat helpers: chunk+raw delta in metres, enemy row filter (radius, sort, cap), fire-command edge trigger. No game headers.
// USED BY: msc_combat.hpp (later), tests/test_combat_logic.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace
{
    constexpr double kCmbChunkM = 2048.0;    // UNVERIFIED chunk edge (m)
    constexpr double kCmbRawPerM = 65536.0;  // raw per metre (verified in Fleet g_raw_per_m)
    // C API row of MSCore_enemies_near, 48 bytes. rel_m = enemy minus reference ship, metres, world axes (chunk axes). Fleet mirrors this struct.
    // flags (was pad): bit0 = hull>0 (alive), bit1 = moved >= 1 m since previous scan of this enemy (0 until 2 samples),
    //                  bit2 = BIG (class name contains "Destroyer", or MaxHealth >= 1500; e.g. RelicDestroyer: 3000 hp, attack 840 m, detect 1260 m),
    //                  bits 8-15 = CurrentTask enum byte (0xFF unreadable). Helpers cmb_enemy_flags / cmb_flag_*.
    struct MsEnemy { int32_t id; int32_t sys; int32_t hull; int32_t pad; double d_m; double rel_m[3]; };
    static_assert(sizeof(MsEnemy) == 48, "MsEnemy layout is part of the C API");
    // bit3 = Health unreadable (hull = -1, alive clear): fail closed for targeting, flagged so the cause is visible.
    inline int32_t cmb_enemy_flags(bool alive, bool moved, int task, bool big = false, bool hull_unknown = false) { return (alive ? 1 : 0) | (moved ? 2 : 0) | (big ? 4 : 0) | (hull_unknown ? 8 : 0) | ((task & 0xFF) << 8); }
    inline bool cmb_flag_hull_unknown(int32_t f) { return (f & 8) != 0; }
    // WHAT: enemy health state from the CURRENT Health read. Unreadable -> UNKNOWN (not targetable), <= 0 -> DEAD (corpse the game has not removed), > 0 -> ALIVE. MaxHealth never decides this.
    enum CmbHp { CMB_HP_DEAD = 0, CMB_HP_ALIVE = 1, CMB_HP_UNKNOWN = 2 };
    inline CmbHp cmb_hp_state(bool read_ok, long long hp) { return !read_ok ? CMB_HP_UNKNOWN : hp > 0 ? CMB_HP_ALIVE : CMB_HP_DEAD; }
    // hull as sent in MsEnemy: current Health clamped to int32, -1 when unknown.
    inline int32_t cmb_hull_value(bool read_ok, long long hp) { return !read_ok ? -1 : hp > 2147483647LL ? 2147483647 : hp < 0 ? 0 : static_cast<int32_t>(hp); }
    inline bool cmb_flag_big(int32_t f) { return (f & 4) != 0; }
    // WHAT: true when class name contains "destroyer" (case-insensitive).
    inline bool cmb_class_big(const std::wstring& n)
    {
        static const wchar_t k[] = L"destroyer";
        const size_t kl = sizeof(k) / sizeof(k[0]) - 1;
        for (size_t i = 0; i + kl <= n.size(); ++i)
        {
            size_t j = 0;
            while (j < kl && (n[i + j] | 0x20) == k[j]) ++j;
            if (j == kl) return true;
        }
        return false;
    }
    constexpr int32_t kCmbBigMaxHealth = 1500;
    // WHAT: `<cur>/<max>` text, "?" for a negative (unknown) value.
    inline std::wstring cmb_hp_text(long long cur, long long mx) { return (cur < 0 ? std::wstring(L"?") : std::to_wstring(cur)) + L"/" + (mx < 0 ? std::wstring(L"?") : std::to_wstring(mx)); }
    // WHAT: PROBE line for the first class read of an enemy id: `PROBE enemy_class id=<id> class=<name> big=<0/1> health=<cur>/<max>`.
    inline std::wstring cmb_probe_class_line(int id, const std::wstring& cls, bool big, long long hp, long long mx = -1)
    {
        return L"PROBE enemy_class id=" + std::to_wstring(id) + L" class=" + cls + L" big=" + (big ? L"1" : L"0") + L" health=" + cmb_hp_text(hp, mx);
    }
    // WHAT: `PROBE enemy_dead id= class= health= max=` (once per id, Health reached <= 0 while the game still lists the enemy).
    inline std::wstring cmb_dead_line(int id, const std::wstring& cls, long long hp, long long mx)
    {
        return L"PROBE enemy_dead id=" + std::to_wstring(id) + L" class=" + cls + L" health=" + std::to_wstring(hp) + L" max=" + (mx < 0 ? std::wstring(L"?") : std::to_wstring(mx));
    }
    // WHAT: `PROBE enemy_task id= class= task= health=` (once per (class, task) pair: builds the CurrentTask table).
    inline std::wstring cmb_task_line(int id, const std::wstring& cls, int task, long long hp)
    {
        return L"PROBE enemy_task id=" + std::to_wstring(id) + L" class=" + cls + L" task=" + std::to_wstring(task) + L" health=" + (hp < 0 ? std::wstring(L"?") : std::to_wstring(hp));
    }
    // WHAT: true the first time (cls, key) is seen; the set is cleared past cap entries. Caller locks.
    inline bool cmb_first_seen(std::set<std::pair<std::wstring, int>>& seen, const std::wstring& cls, int key, size_t cap = 4096)
    {
        if (seen.size() > cap) seen.clear();
        return seen.insert({cls, key}).second;
    }
    // WHAT: global limiter for PROBE lines: at most max lines per window ms (fixed window). Caller locks.
    struct CmbLogRate { uint64_t start = 0; int n = 0; };
    inline bool cmb_log_allow(CmbLogRate& r, uint64_t now, int max = 20, uint64_t window = 10000)
    {
        if (r.start == 0 || now - r.start >= window) { r.start = now ? now : 1; r.n = 0; }
        if (r.n >= max) return false;
        ++r.n;
        return true;
    }
    inline bool cmb_flag_alive(int32_t f) { return (f & 1) != 0; }
    inline bool cmb_flag_moved(int32_t f) { return (f & 2) != 0; }
    inline int cmb_flag_task(int32_t f) { return (f >> 8) & 0xFF; }
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
    // WHAT: weapon range = speed * lifetime / raw_per_m. Units unprobed, caller passes constants. Any arg <= 0 -> -1. ticks_per_s kept, unused.
    inline double cmb_range_m(double bullet_speed_raw, double lifetime_ticks, double ticks_per_s, double raw_per_m)
    {
        if (bullet_speed_raw <= 0 || lifetime_ticks <= 0 || ticks_per_s <= 0 || raw_per_m <= 0) return -1;
        return bullet_speed_raw * lifetime_ticks / raw_per_m;
    }
    // WHAT: raw range * safety factor; -1 if either <= 0.
    inline double cmb_engage_range(double raw_m, double safety) { return (raw_m <= 0 || safety <= 0) ? -1 : raw_m * safety; }
    // user-observed 2026-11-22, supersedes the unverified lifetime=ticks formula (cmb_range_m stays as informational raw_m)
    constexpr int kObservedCannonReachM = 700;
    // WHAT: cannon reach in metres for a ship with n_cannons Cannon-class weapons, else -1.
    inline int cmb_cannon_reach(int n_cannons) { return n_cannons > 0 ? kObservedCannonReachM : -1; }
    // Simulation frames per second, used to turn per-frame enemy velocity into m/s. UNVERIFIED (assumed 60); calibrate from the PROBE enemy_vel lines.
    // legacy, unused at runtime: conversions use sim_tps_conv()
    constexpr double kSimFps = 60.0;
    // WHAT: per-frame raw velocity -> per-frame metres (v_frame_m) and m/s (v_mps = per_frame_m * fps); returns |v_mps|. raw_per_m <= 0 or fps <= 0 -> -1.
    inline double cmb_vel_mps(const double raw_per_frame[3], double raw_per_m, double fps, double v_frame_m[3], double v_mps[3])
    {
        if (raw_per_m <= 0 || fps <= 0) return -1;
        double s = 0;
        for (int i = 0; i < 3; ++i) { v_frame_m[i] = raw_per_frame[i] / raw_per_m; v_mps[i] = v_frame_m[i] * fps; s += v_mps[i] * v_mps[i]; }
        return std::sqrt(s);
    }
    // WHAT: which vector shape to use: 1 = PNum axes (struct with Value), 2 = plain double axes, 0 = none. PNum wins when both read.
    inline int cmb_vel_shape(bool pnum_ok, bool double_ok) { return pnum_ok ? 1 : (double_ok ? 2 : 0); }
    // enemy motion (API 24): out[14] layout (12..13 = avoidance_distance_m, avoidance_speed_mps), see msc_combat.hpp enemy_motion. kCmbNoLoc = "target location unreadable" marker (all 3 axes).
    constexpr double kCmbNoLoc = 1e30;
    constexpr int kCmbMotionSlots = 14;
    // WHAT: every slot -1, target location slots 7..9 = kCmbNoLoc.
    inline void cmb_motion_init(double out[14])
    {
        for (int i = 0; i < kCmbMotionSlots; ++i) out[i] = -1;
        for (int i = 7; i < 10; ++i) out[i] = kCmbNoLoc;
    }
    // WHAT: per-frame value -> per-second (value * fps); -1 when not found or fps <= 0.
    inline double cmb_rate_per_s(bool found, double per_frame, double fps) { return (found && fps > 0) ? per_frame * fps : -1; }
    // WHAT: writes slots 7..9 from rel (ok) or the kCmbNoLoc marker.
    inline void cmb_motion_set_target(double out[14], bool ok, const double rel[3])
    {
        for (int i = 0; i < 3; ++i) out[7 + i] = ok ? rel[i] : kCmbNoLoc;
    }
    // WHAT: true when slots 7..9 hold the kCmbNoLoc marker.
    inline bool cmb_motion_noloc(const double out[14]) { return out[7] >= kCmbNoLoc && out[8] >= kCmbNoLoc && out[9] >= kCmbNoLoc; }
    // WHAT: smallest value > 0, else -1.
    inline double cmb_min_range(const std::vector<double>& v)
    {
        double m = -1;
        for (double x : v) if (x > 0 && (m < 0 || x < m)) m = x;
        return m;
    }
}
