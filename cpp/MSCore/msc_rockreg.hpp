// FILE: cpp/MSCore/msc_rockreg.hpp
// WHAT: game side of the shared rock registry (native API 16). Seeds the RrTable (msc_rockreg_logic.hpp) from the save objects of class
//       kClsAsteroid, nearest first; caches the table to disk; reads radius lazily; validates save objects near ships; self-check (registry
//       position vs actor K2_GetActorLocation); stats; dump / query / check reports.
// WHY: rocks never move or shrink, so position, ore and radius are read once per rock; the save data holds every rock, loaded or not.
//       One registry in MSCore serves every mod (MiningBenchMod standalone, Fleet) through the MSCore_rockreg_* exports.
// SAVE FIELDS USED: sys_id(save) = system, get_chunk(save) + sec_axes(save, Location) = chunk-relative position (abs m = chunk * 2048 + loc / 65536),
//       obj_id(save) = rock id (= actor ObjectId), bAlive, ResourceItems (ores, read by fill_ores of msc_scan.hpp), GetRadius() on the save (raw / 65536 = m).
// DELETES: ONE FUObjectDeleteListener (heap, never freed, released in ~MSCoreMod and OnUObjectArrayShutdown) queues every deleted UObject pointer.
//       rr_tick drains it first: a dead pointer equal to a bound save[i] clears save[i] and kills the rock (the save object is gone). Only the
//       pointers that hit a bound save count: when one drain hits more than RR_MASS_DEAD saves (a world unload), or the queue overflowed, or the
//       world tracker reports LOADED / LOST, it is a WORLD CHANGE: every save pointer is dropped, nothing is killed, the seed waits and reseeds.
//       (An ordinary garbage collection frees far more than 500 unrelated UObjects; counting those would unbind everything at every GC.)
// PASS (one FindAllOf of kClsAsteroid, then slices): 1 bind (id + bAlive per save), 2 sample (only after a cache load), 3 classify (ring from
//       the nearest ship), 4 read ('need' saves in ring order; phase A = rings <= RR_PRIORITY_CHUNKS makes the zones ready), 5 finish (unseen rocks
//       killed; cache written when the table changed by > RR_CACHE_CHANGE_FRAC or no valid cache).
// CACHE: rockreg\RockRegCache.bin next to the Scripts folder of MSCore (rr_dir), format v2 in msc_rockreg_logic.hpp, loaded once (first pass),
//       written in chunks over ticks (tmp file + rename). The old per-mod MiningRegistryCache.bin files are not used (orphaned, safe to delete).
// DRIVER: MSCore has no frame hook for this. Each mod calls MSCore_rockreg_feed(owner, ships, targets) every ~0.25 s; the feeds younger than
//       RR_FEED_MAX_MS are merged (ships by sid, targets as a set) and rr_tick runs when >= RR_TICK_MS passed since the last tick.
// SAFETY: game thread only. Every export body runs in seh_run (rr_run); there is no in-tick fault recovery, a fault disables the module. A fault or exception turns the module 'rockreg' off for the session
//       (caps row, module_disable); every export then returns -2 (-1 on exception). rr_selftest turns it off when a save object lacks the fields.
// UNITS: metres (see msc_rockreg_logic.hpp). Ship positions come from the callers (actor location / 100), not from a game call here.
// DEPENDS ON: msc_rockreg_logic.hpp, msc_api.hpp (MsReg*), msc_cmd.hpp (alive, seh_run), msc_caps.hpp, msc_ue.hpp (obj_id, get_bool_prop, get_obj_prop,
//       actor_vec_call, obj_display, msc_log), msc_warp.hpp (get_chunk, sec_axes, sys_id), msc_chunks.hpp (ch_player, ch_ctrl), msc_flight.hpp (call_pnum),
//       msc_scan.hpp (alive_seh, fill_ores, SCAN_ORES, MsScanRow).
// USED BY: dllmain.cpp (MSCore_rockreg_* exports, check "rockreg", rr_resolve, rr_listener_release).
// OUTPUT: log lines 'RR seed start/near done/done', 'RR cache none|ignored|load|trusted|merged|reseed|written', 'RR selfcheck SUMMARY', 'RR stats', 'RR slow',
//       'RR world loaded|lost'; files rockreg\RockRegDump.txt (every 60 s and by report kind 0), RockRegQuery.txt (kind 1), RockRegCheck.txt (kind 2),
//       RockRegCache.bin.
#pragma once

#include <Unreal/UObjectArray.hpp>

#include "msc_rockreg_logic.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace
{
    // WHAT: raw units per metre of a save position / GetRadius (PNum: 65536 raw = 1 m). Same constant as the chunk size of the logic header.
    constexpr double RR_RAW_PER_M = 65536.0;
    // WHAT: actor K2_GetActorLocation unit (uu) per metre.
    constexpr double RR_UU_PER_M = 100.0;

    // WHAT: periods and caps of the registry (ms / count).
    constexpr ULONGLONG RR_TICK_MS = 200;               // rr_tick runs when this long passed since the last one
    constexpr ULONGLONG RR_FEED_MAX_MS = 2000;          // a feed older than this is ignored by the merge
    constexpr ULONGLONG RR_SEED_REFRESH_MS = 600000;    // safety full pass every 10 min
    constexpr ULONGLONG RR_SELF_MS = 600000;            // self-check period
    constexpr ULONGLONG RR_STATS_MS = 60000;            // stats line and RockRegDump.txt period
    constexpr ULONGLONG RR_SLOW_LOG_MS = 30000;         // 'RR slow' at most this often
    constexpr ULONGLONG RR_SYS_MS = 2000;               // ship system check period
    constexpr ULONGLONG RR_TRIG_MS = 1000;              // seed trigger check
    constexpr ULONGLONG RR_EMPTY_RETRY_MS = 5000;       // seed retry while the table holds no alive rock / after a world change
    constexpr int RR_NOWORLD_TICKS = 4;                 // failed player reads in a row before the world counts as gone
    constexpr size_t RR_SELF_N = 50;                    // rocks compared in one self-check
    constexpr size_t RR_DUMP_N = 200;                   // rows of RockRegDump.txt
    constexpr double RR_DUMP_M = 20000.0;               // dump: rocks within this distance of the first ship
    constexpr size_t RR_LIST_N = 50;                    // rows of a query report
    constexpr double RR_MISSING_M = 3000.0;             // compare: a loaded actor this close with no registry entry counts as missing
    constexpr double RR_RAD_SEG_M = 1500.0;             // radius reads first for rocks this close to an approach segment
    constexpr size_t RR_RAD_SEG_TICK = 40;              // GetRadius reads per tick for approach segment rocks
    constexpr size_t RR_MASS_DEAD = 500;                // bound saves deleted in one drain = world change
    constexpr size_t RR_DEAD_CAP = 65536;               // delete queue cap; above it the queue is cleared and flagged (world change)
    constexpr size_t RR_CHECK_MAX_N = 64;               // check report rows (max)
    constexpr int RR_NEAREST_MAX_N = 64;                // MSCore_rockreg_nearest_n: rocks returned (max)
    constexpr int RR_SKIP_MAX = 256;                    // MSCore_rockreg_nearest_n: skip ids used (max; the rest is ignored)
    constexpr int RR_MAX_FEED_N = 100000;               // sanity cap on a feed array
    constexpr double RR_COORD_MAX = 1.0e12;             // largest |coordinate| or length (m) accepted from a caller (cell indexes stay far inside int64)

    // WHAT: wide -> UTF-8 (ore names are compared and stored as UTF-8).
    std::string rr_u8(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return std::string();
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
        return s;
    }
    // WHAT: UTF-8 -> wide (text of reports and logs).
    std::wstring rr_w(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (n <= 0) return std::wstring();
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
        return w;
    }

    // WHAT: ms since a QueryPerformanceCounter reading.
    double rr_perf_ms(const LARGE_INTEGER& t0)
    {
        LARGE_INTEGER now, hz;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&hz);
        return static_cast<double>(now.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(hz.QuadPart);
    }

    // WHAT: true when every component of v is finite and |v| <= RR_COORD_MAX.
    // WHY: the logic header turns coordinates into int64 cell indexes; a NaN, infinite or huge value from a caller would overflow the cell span and hang the game thread.
    bool rr_ok3(const double v[3])
    {
        for (int k = 0; k < 3; ++k) if (!std::isfinite(v[k]) || std::fabs(v[k]) > RR_COORD_MAX) return false;
        return true;
    }
    // WHAT: v as a length in 0..RR_COORD_MAX (huge values are clamped, "no limit" = a big number works). false for NaN.
    bool rr_len(double v, double& out)
    {
        if (std::isnan(v)) return false;
        out = std::min(std::max(v, 0.0), RR_COORD_MAX);
        return true;
    }

    double rr_norm3(const double v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

    // WHAT: folder of the registry files: <MSCore mod folder>\rockreg (the DLL sits in <mod>\dlls). Created on first use.
    const std::filesystem::path& rr_dir()
    {
        static std::filesystem::path p;
        static bool done = false;
        if (done) return p;
        done = true;
        HMODULE hm = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&rr_dir), &hm))
        {
            std::vector<wchar_t> buf(32768);
            const DWORD n = GetModuleFileNameW(hm, buf.data(), static_cast<DWORD>(buf.size()));
            if (n > 0 && n < buf.size()) p = std::filesystem::path(std::wstring(buf.data(), n)).parent_path().parent_path() / L"rockreg";
        }
        if (p.empty()) p = std::filesystem::path(L"rockreg");
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p;
    }

    // WHAT: writes txt to dst through dst.tmp and a rename. false on any failure.
    bool rr_write_atomic(const std::filesystem::path& dst, const std::string& txt)
    {
        std::filesystem::path tmp = dst;
        tmp += L".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f) return false;
            f.write(txt.data(), static_cast<std::streamsize>(txt.size()));
            if (!f) return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, dst, ec);
        return !ec;
    }

    // WHAT: the cache writer state (one file at a time). Records are streamed to <file>.tmp in chunks; the header is written last.
    struct RrCacheWrite
    {
        bool on = false;
        FILE* f = nullptr;
        std::filesystem::path tmp, dst;
        size_t next = 0;                   // next rock index to write
        uint64_t save_n = 0, rock_n = 0;
        uint64_t sum = RR_CACHE_FNV0;      // FNV-1a of the bytes after the header so far
        std::vector<uint8_t> buf;          // bytes not yet written
        ULONGLONG t_begin = 0;
    };

    // WHAT: one owner's last feed (owner 1 = standalone mining, 2 = Fleet).
    struct RrFeed { ULONGLONG ms = 0; std::vector<MsRegShip> ships; std::vector<int> targets; };

    // WHAT: all registry state (heap, never freed: the engine may call the delete listener at shutdown).
    struct RrState
    {
        RrTable tab;
        std::vector<UObject*> save;        // per rock index: save object of the running pass (alive_seh before each use), nullptr = not bound
        std::vector<uint64_t> seen;        // per rock index: seed pass that last saw it
        std::unordered_map<const void*, size_t> save_at;   // save pointer -> rock index (kept in step with save[]); the delete drain looks up here
        std::unordered_map<const void*, int16_t> ikind;    // ore item pointer -> kind index (one name read per item); cleared on unbind / pass start
        std::vector<UObject*> seed_list;   // FindAllOf result of the running pass
        std::vector<UObject*> need;        // saves of the running pass that need a full read
        std::vector<int> ring;             // per need item: seed ring
        std::vector<size_t> order;         // need indices, nearest ring first
        std::vector<RrSrc> src;            // ships of the running pass (system, chunk)
        size_t bind_cur = 0, cls_cur = 0, ord_cur = 0, near_n = 0;
        size_t live_n = 0;                 // live saves seen by the running pass
        size_t changed = 0;                // rocks added or revived by the running pass
        bool seeding = false;
        bool ordered = false, near_done = false, sample_done = true;
        bool cache_tried = false, cache_pending = false, cache_ok = false;
        uint64_t cache_save_n = 0;
        uint64_t exe_size = 0;
        bool notready_logged = false;
        RrCacheWrite cw;
        uint64_t pass = 0;
        std::set<int> sys_done;            // systems a pass has covered (started with a ship there)
        int world_sys = 0;
        bool world_ok = false;
        bool world_wait = false;           // world changed: the next seed waits RR_EMPTY_RETRY_MS (freed saves of the old world)
        RrWorldTrack wt;
        bool seed_due = false;
        int cache_verdict = -1;            // verdict of the cache sample (RrCacheVerdict), -1 = not decided; kept until a pass finishes, so an aborted pass re-applies it
        size_t prev_alive = 0;             // alive rocks known when the running pass began (table + cache): the sanity reference
        int insane_n = 0;                  // consecutive passes rejected by rr_seed_sane
        int rd_ok_ticks = 0;               // consecutive ticks whose world probe read the player; kills need RR_NOWORLD_TICKS of them
        unsigned cache_fail_n = 0;         // cache write failures in a row
        ULONGLONG cache_retry_at = 0;      // no cache write before this GetTickCount64 (backoff after a failure)
        std::set<std::string> cache_fail_logged;   // failure kinds already logged
        std::vector<uint8_t> rad_fail;     // per rock index: failed GetRadius reads in a row
        std::vector<ULONGLONG> rad_next;   // per rock index: earliest next GetRadius try (GetTickCount64)
        ULONGLONG seed_ms = 0, self_ms = 0, stats_ms = 0, slow_ms = 0, sys_ms = 0, trig_ms = 0, last_tick_ms = 0;
        std::string self_summary = "SUMMARY n/a (no selfcheck yet)";
        bool self_due = false;
        size_t valid_cur = 0;
        uint64_t ticks = 0;
        double ms_sum = 0.0, ms_worst = 0.0;
        uint64_t stat_ticks = 0;
        std::vector<MsRegShip> ships;      // merged ships of this tick
        std::vector<int> targets;
        std::map<int, RrFeed> feeds;       // owner -> last feed
    };
    RrState& rr_st() { static RrState* s = new RrState; return *s; }

    // ---------------- delete listener ----------------
    struct RrDeadQ { std::mutex mx; std::vector<const void*> q; bool overflow = false; };
    RrDeadQ& rr_dead() { static RrDeadQ* s = new RrDeadQ; return *s; }
    struct RrListener;
    // WHY atomic: the release (game thread, ~MSCoreMod) and OnUObjectArrayShutdown (engine thread) can run together; exchange / compare_exchange make sure only one removes.
    std::atomic<RrListener*> g_rr_listener{nullptr};
    std::atomic<bool> g_rr_listener_gone{false};   // released: never registered again this session
    struct RrListener : FUObjectDeleteListener
    {
        void NotifyUObjectDeleted(const UObjectBase* o, int32_t) override
        {
            RrDeadQ& d = rr_dead();
            std::lock_guard<std::mutex> l(d.mx);
            if (d.overflow) return;
            if (d.q.size() >= RR_DEAD_CAP) { d.q.clear(); d.overflow = true; return; }
            d.q.push_back(o);
        }
        // WHY: the engine asserts at shutdown that every delete listener removed itself inside this callback.
        void OnUObjectArrayShutdown() override
        {
            RrListener* self = this;
            g_rr_listener_gone = true;
            if (g_rr_listener.compare_exchange_strong(self, nullptr)) UObjectArray::RemoveUObjectDeleteListener(this);
        }
    };
    // WHAT: unregisters the delete listener (mod destructor: the listener code lives in this DLL, which is unloaded at game exit). Not deleted.
    void rr_listener_release()
    {
        g_rr_listener_gone = true;
        RrListener* l = g_rr_listener.exchange(nullptr);
        if (l) UObjectArray::RemoveUObjectDeleteListener(l);
    }

    // WHAT: grows the per-rock side arrays to cover index i.
    void rr_side(RrState& s, size_t i)
    {
        if (s.save.size() <= i) { s.save.resize(i + 1, nullptr); s.seen.resize(i + 1, 0); s.rad_fail.resize(i + 1, 0); s.rad_next.resize(i + 1, 0); }
    }
    // WHAT: sets the save pointer of rock i (nullptr clears it) and keeps save_at in step.
    void rr_bind(RrState& s, size_t i, UObject* o)
    {
        rr_side(s, i);
        if (s.save[i]) s.save_at.erase(s.save[i]);
        s.save[i] = o;
        if (o) s.save_at[o] = i;
    }

    // WHAT: the executable size (cache key). 0 when unreadable (the cache is then not used).
    uint64_t rr_exe_size()
    {
        wchar_t b[1024] = {0};
        if (!GetModuleFileNameW(nullptr, b, 1024)) return 0;
        std::error_code ec;
        const auto n = std::filesystem::file_size(b, ec);
        return ec ? 0 : static_cast<uint64_t>(n);
    }

    // WHAT: display name of an ore item (UTF-8), "" when the item is dead or the read faults.
    std::string rr_item_name(UObject* item)
    {
        if (!alive_seh(item)) return std::string();
        std::wstring w;
        if (!seh_run([&] { w = obj_display(item); if (w.empty()) w = item->GetName(); })) return std::string();
        return rr_u8(w);
    }

    // WHAT: kind index of an ore name in tab.kinds (appended when new, -1 for "" and when the table is full).
    int16_t rr_kind_idx(RrTable& t, const std::string& name, bool add)
    {
        if (name.empty()) return -1;
        for (size_t k = 0; k < t.kinds.size(); ++k) if (t.kinds[k] == name) return static_cast<int16_t>(k);
        if (!add || t.kinds.size() >= 30000) return -1;
        t.kinds.push_back(name);
        return static_cast<int16_t>(t.kinds.size() - 1);
    }

    // WHAT: kind index of an ore item, one name read per item pointer per pass.
    int16_t rr_item_kind(RrState& s, UObject* item)
    {
        auto it = s.ikind.find(item);
        if (it != s.ikind.end()) return it->second;
        const int16_t k = rr_kind_idx(s.tab, rr_item_name(item), true);
        s.ikind[item] = k;
        return k;
    }

    // WHAT: ore name indexes (up to 4, -1 terminated) of a save object's ResourceItems (fill_ores of msc_scan.hpp: struct-with-Item or bare Item array).
    void rr_ore_of(RrState& s, UObject* o, int16_t ore[4])
    {
        for (int k = 0; k < 4; ++k) ore[k] = -1;
        MsScanRow r{};
        int32_t ids[SCAN_ORES] = {};
        fill_ores(r, o, ids);
        int n = 0;
        for (int i = 0; i < r.nores && n < 4; ++i)
        {
            const int16_t k = rr_item_kind(s, static_cast<UObject*>(r.ores[i]));
            if (k < 0) continue;
            bool dup = false;
            for (int j = 0; j < n; ++j) if (ore[j] == k) dup = true;
            if (!dup) ore[n++] = k;
        }
    }

    // WHAT: ore name of a rock (its first ore), "?" when none.
    std::string rr_ore_name(const RrTable& t, const RrRock& r)
    {
        if (r.ore[0] >= 0 && static_cast<size_t>(r.ore[0]) < t.kinds.size()) return t.kinds[static_cast<size_t>(r.ore[0])];
        return "?";
    }

    // WHAT: index of an Item object's name in tab.kinds, -1 when the item is dead or no rock has that ore name.
    int rr_ore_idx(const RrState& s, UObject* item)
    {
        const std::string n = rr_item_name(item);
        if (n.empty()) return -1;
        for (size_t k = 0; k < s.tab.kinds.size(); ++k) if (s.tab.kinds[k] == n) return static_cast<int>(k);
        return -1;
    }

    // WHAT: reads the full data of one save object into the table (skipped when dead, not alive, no system, no chunk, no id). true when added or revived.
    bool rr_seed_one(RrState& s, UObject* o)
    {
        if (!alive_seh(o)) return false;
        bool al = true;
        get_bool_prop(o, STR("bAlive"), al);
        if (!al) return false;
        const int sys = sys_id(o);
        int64_t ch[3] = {0, 0, 0}, raw[3] = {0, 0, 0};
        if (sys <= 0 || !get_chunk(o, ch) || !sec_axes(o, STR("Location"), raw)) return false;
        const int id = obj_id(o);
        if (id <= 0) return false;
        int16_t ore[4];
        rr_ore_of(s, o, ore);
        const double loc[3] = {static_cast<double>(raw[0]), static_cast<double>(raw[1]), static_cast<double>(raw[2])};
        double c[3] = {0, 0, 0};
        rr_abs_m(ch, loc, RR_RAW_PER_M, c);
        // changed = new, revived, or a known row the save corrects (system, position, ore): a corrected cache must be rewritten
        const size_t old = s.tab.index_of(id);
        bool fresh = old == RrTable::npos || !s.tab.rocks[old].alive;
        if (!fresh)
        {
            const RrRock& r = s.tab.rocks[old];
            fresh = r.sys != sys || !rr_same_pos(r.c, c) || std::memcmp(r.ore, ore, sizeof ore) != 0;
        }
        const size_t i = s.tab.add(id, sys, c, ore);
        rr_side(s, i);
        s.tab.rocks[i].unverified = false;
        rr_bind(s, i, o);
        s.seen[i] = s.pass;
        return fresh;
    }

    // WHAT: rocks alive in the table (running counter of the table).
    size_t rr_alive_count(const RrState& s) { return s.tab.alive_count(); }

    // WHAT: the player's system and a world identity (controller + PlayerObject); run under seh_run. sys = 0 when unreadable.
    struct RrWorldCtx { int sys = 0; uint64_t id = 0; };
    void rr_world_probe(RrWorldCtx& x)
    {
        int64_t pch[3] = {0, 0, 0};
        int ps = 0;
        if (ch_player(ps, pch)) x.sys = ps;
        if (x.sys > 0)
        {
            UObject* c = ch_ctrl();
            UObject* po = get_obj_prop(c, STR("PlayerObject"));
            x.id = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(c)) * 1000003ull) ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(po));
            if (x.id == 0) x.id = 1;
        }
    }

    // WHAT: frees the lists of the running pass (after a finish or an abort).
    void rr_seed_release(RrState& s)
    {
        std::vector<UObject*>().swap(s.seed_list);
        std::vector<UObject*>().swap(s.need);
        std::vector<int>().swap(s.ring);
        std::vector<size_t>().swap(s.order);
        s.src.clear();
        s.bind_cur = s.cls_cur = s.ord_cur = s.near_n = 0;
        s.ordered = false;
    }

    // WHAT: loads the cache file into the table: one read, one parse. The entries are unverified until a pass binds them (and samples them).
    void rr_cache_load(RrState& s)
    {
        if (s.exe_size == 0) return;
        const auto path = rr_dir() / L"RockRegCache.bin";
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) { msc_log(STR("RR cache none (the first full seed writes it)")); return; }
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        try
        {
            std::vector<uint8_t> img;
            {
                std::ifstream f(path, std::ios::binary);
                if (!f) throw std::runtime_error("open");
                f.seekg(0, std::ios::end);
                const auto sz = static_cast<size_t>(f.tellg());
                f.seekg(0, std::ios::beg);
                img.resize(sz);
                if (sz && !f.read(reinterpret_cast<char*>(img.data()), static_cast<std::streamsize>(sz))) throw std::runtime_error("read");
            }
            RrCacheData d;
            std::string why;
            if (!rr_cache_parse(img.data(), img.size(), s.exe_size, d, why))
            {
                msc_log(STR("RR cache ignored: ") + rr_w(why));
                return;
            }
            std::vector<uint8_t>().swap(img);
            s.tab.kinds = d.kinds;
            for (const RrCacheRec& c : d.recs)
            {
                const size_t i = s.tab.add(c.id, c.sys, c.c, c.ore);
                rr_side(s, i);
                s.tab.rocks[i].unverified = true;
                if (c.rad >= 0.0f) s.tab.set_radius(i, static_cast<double>(c.rad));
            }
            s.cache_pending = true;
            s.cache_save_n = d.h.save_n;
            s.cache_ok = true;
            msc_log(STR("RR cache load n=") + std::to_wstring(d.recs.size()) + STR(" saves=") + std::to_wstring(d.h.save_n) + STR(" ms=") +
                    std::to_wstring(rr_perf_ms(t0)) + STR(" (from cache, unverified)"));
        }
        catch (...)
        {
            msc_log(STR("RR cache load fault: file ignored"));
        }
    }

    // WHAT: starts a pass: the cache is loaded (first pass only), one FindAllOf of kClsAsteroid (all systems), the zones of the ships are set up.
    void rr_seed_begin(RrState& s, const std::vector<MsRegShip>& ships)
    {
        s.seed_ms = GetTickCount64();   // set first: a faulted find is retried at the next trigger, not every tick
        if (!s.cache_tried)
        {
            s.cache_tried = true;
            s.exe_size = rr_exe_size();
            rr_cache_load(s);
        }
        for (const MsRegShip& sh : ships) if (sh.sys > 0) s.sys_done.insert(sh.sys);
        LARGE_INTEGER a;
        QueryPerformanceCounter(&a);
        std::vector<UObject*> v;
        const std::wstring acls = widen(kClsAsteroid);
        UObjectGlobals::FindAllOf(acls.c_str(), v);
        const double find_ms = rr_perf_ms(a);
        s.seed_list.swap(v);
        s.src.clear();
        for (const MsRegShip& sh : ships)
        {
            if (sh.sys <= 0) continue;
            RrSrc x;
            x.sys = sh.sys;
            rr_chunk_of_m(sh.p, x.c);
            s.src.push_back(x);
        }
        ++s.pass;
        s.ikind.clear();
        s.tab.zones_begin(s.pass, s.src, RR_PRIORITY_CHUNKS);
        s.bind_cur = s.cls_cur = s.ord_cur = s.near_n = 0;
        s.live_n = 0;
        s.changed = 0;
        s.need.clear();
        s.ring.clear();
        s.order.clear();
        s.sample_done = !s.cache_pending;
        s.prev_alive = std::max<size_t>(s.tab.alive_count(), s.cache_pending ? static_cast<size_t>(s.cache_save_n) : 0);
        s.ordered = false;
        s.near_done = false;
        s.notready_logged = false;
        s.seeding = true;
        msc_log(STR("RR seed start n=") + std::to_wstring(s.seed_list.size()) + STR(" pass=") + std::to_wstring(s.pass) + STR(" find_ms=") +
                std::to_wstring(find_ms) + STR(" ships=") + std::to_wstring(s.src.size()));
    }

    // WHAT: ends the running pass without finishing it (a ship in a new system). The table is kept; the next pass binds again.
    void rr_seed_abort(RrState& s)
    {
        s.seeding = false;
        rr_seed_release(s);
    }

    // WHAT: drops every game object pointer of the registry (save objects of the table, lists of the running pass, item pointers). The table rows stay.
    // WHY: after a world load the save objects are freed; a later read through them faults.
    void rr_seed_unbind(RrState& s)
    {
        std::fill(s.save.begin(), s.save.end(), nullptr);
        s.save_at.clear();
        s.ikind.clear();
        rr_seed_release(s);
    }

    // WHAT: bind of one save: alive and bAlive, obj_id. A known alive rock is bound (save pointer, seen); any other save goes to 'need'.
    void rr_bind_one(RrState& s, UObject* o)
    {
        if (!alive_seh(o)) return;
        bool al = true;
        get_bool_prop(o, STR("bAlive"), al);
        if (!al) return;
        const int id = obj_id(o);
        if (id <= 0) return;
        ++s.live_n;
        const size_t i = s.tab.index_of(id);
        if (i != RrTable::npos && s.tab.rocks[i].alive)
        {
            rr_bind(s, i, o);
            s.seen[i] = s.pass;
            return;
        }
        s.need.push_back(o);
    }

    // WHAT: applies the cache verdict to the unverified bound rocks cand: reseed = their saves are read again (queued in 'need'); trust / merge = verified.
    void rr_sample_apply(RrState& s, const std::vector<size_t>& cand, RrCacheVerdict v, size_t checked, size_t bad)
    {
        if (v == RCV_RESEED)
        {
            for (size_t i : cand) if (s.save[i]) s.need.push_back(s.save[i]);
            msc_log(STR("RR cache reseed: sample checked=") + std::to_wstring(checked) + STR(" bad=") + std::to_wstring(bad) +
                    STR(" cached rocks read again=") + std::to_wstring(cand.size()));
            return;
        }
        for (size_t i : cand) s.tab.rocks[i].unverified = false;
        msc_log(std::wstring(v == RCV_TRUST ? STR("RR cache trusted") : STR("RR cache merged")) + STR(" saves=") + std::to_wstring(s.live_n) +
                STR(" cache_saves=") + std::to_wstring(s.cache_save_n) + STR(" checked=") + std::to_wstring(checked) + STR(" bad=0 bound=") +
                std::to_wstring(cand.size()));
    }

    // WHAT: one-shot sample check of a loaded cache: the unverified rocks nearest to the ships and as many random ones are read (position, ore
    //       names) and compared with the table. rr_cache_verdict decides: trust, merge (new reads only) or reseed (all cache rocks re-read).
    // ponytail: one call scans the rock table once (cand) and reads at most 2 * RR_CACHE_SAMPLE_N = 100 saves; it runs once per session, so it is not
    //           resumed across ticks. Ceiling: a table of ~1M rocks makes that one scan a visible hitch; then split the cand scan over ticks.
    void rr_sample_stage(RrState& s)
    {
        s.sample_done = true;   // cache_pending stays set until a pass FINISHES: an aborted pass runs this stage again
        std::vector<size_t> cand;
        for (size_t i = 0; i < s.tab.rocks.size(); ++i)
        {
            const RrRock& r = s.tab.rocks[i];
            if (r.unverified && r.alive && i < s.save.size() && s.save[i]) cand.push_back(i);
        }
        if (s.cache_verdict >= 0) { rr_sample_apply(s, cand, static_cast<RrCacheVerdict>(s.cache_verdict), 0, 0); return; }   // restarted pass: same verdict
        std::vector<std::pair<double, size_t>> by_dist;
        by_dist.reserve(cand.size());
        for (size_t i : cand)
        {
            const RrRock& r = s.tab.rocks[i];
            double best = 1e300;
            for (const MsRegShip& sh : s.ships)
            {
                if (sh.sys != r.sys) continue;
                const double d[3] = {r.c[0] - sh.p[0], r.c[1] - sh.p[1], r.c[2] - sh.p[2]};
                best = std::min(best, rr_norm3(d));
            }
            by_dist.push_back({best, i});
        }
        const size_t kn = std::min(RR_CACHE_SAMPLE_N, by_dist.size());
        std::partial_sort(by_dist.begin(), by_dist.begin() + static_cast<std::ptrdiff_t>(kn), by_dist.end());
        std::set<size_t> pick;
        for (size_t k = 0; k < kn; ++k) if (by_dist[k].first < 1e300) pick.insert(by_dist[k].second);
        std::mt19937_64 rng(static_cast<uint64_t>(GetTickCount64()));
        for (size_t t = 0; !cand.empty() && pick.size() < 2 * RR_CACHE_SAMPLE_N && t < 8 * RR_CACHE_SAMPLE_N; ++t) pick.insert(cand[rng() % cand.size()]);

        size_t checked = 0, bad = 0;
        const std::vector<size_t> idx(pick.begin(), pick.end());
        const bool ok = seh_run([&]
        {
            for (size_t i : idx)
            {
                const RrRock& r = s.tab.rocks[i];
                UObject* o = s.save[i];
                ++checked;
                int64_t ch[3] = {0, 0, 0}, raw[3] = {0, 0, 0};
                if (!alive(o) || !get_chunk(o, ch) || !sec_axes(o, STR("Location"), raw)) { ++bad; continue; }
                const double loc[3] = {static_cast<double>(raw[0]), static_cast<double>(raw[1]), static_cast<double>(raw[2])};
                double am[3] = {0, 0, 0};
                rr_abs_m(ch, loc, RR_RAW_PER_M, am);
                int16_t ore[4];
                rr_ore_of(s, o, ore);
                if (!rr_same_pos(am, r.c) || std::memcmp(ore, r.ore, sizeof ore) != 0) ++bad;
            }
        });
        if (!ok) ++bad;
        const RrCacheVerdict v = rr_cache_verdict(s.cache_save_n, s.live_n, checked, bad);
        s.cache_verdict = static_cast<int>(v);
        rr_sample_apply(s, cand, v, checked, bad);
    }

    // WHAT: ring of one 'need' save: system and chunk (cheap reads). A save that cannot be read gets RR_RING_FAR.
    void rr_classify_one(RrState& s, size_t i)
    {
        UObject* o = s.need[i];
        int ring = RR_RING_FAR;
        int64_t ch[3] = {0, 0, 0};
        if (alive_seh(o))
        {
            const int sys = sys_id(o);
            if (sys > 0 && get_chunk(o, ch)) ring = rr_ring_of(sys, ch, s.src);
        }
        s.ring.push_back(ring);
    }

    // WHAT: phase A check: when every save of rings <= RR_PRIORITY_CHUNKS is read, the zones of the pass are ready and the line is logged once.
    void rr_near_check(RrState& s)
    {
        if (s.near_done || !s.ordered || s.ord_cur < s.near_n) return;
        s.near_done = true;
        s.tab.zones_ready(s.pass);
        msc_log(STR("RR seed near done n=") + std::to_wstring(s.near_n) + STR(" ms=") + std::to_wstring(GetTickCount64() - s.seed_ms) +
                STR(" pass=") + std::to_wstring(s.pass));
    }

    void rr_cache_fail(RrState& s, const char* kind);   // defined with the writer below

    // WHAT: ends a pass: rocks it did not see are killed; the self-check is due; the cache is written when it is missing or the table changed a lot.
    void rr_seed_finish(RrState& s)
    {
        s.seeding = false;
        // sanity (rr_pass_verdict): a pass that saw fewer than half of the rocks known before it (world not fully loaded) is incomplete: the partial 'seen'
        // marks are discarded, nothing is killed, the cache is not touched, the pass is retried after RR_EMPTY_RETRY_MS. After 3 incomplete tries the pass
        // is accepted as INCOMPLETE: it still kills NOTHING and writes NO cache (absent rocks are not confirmed absent; the delete listener and the 2 km
        // validation remove the dead ones over time).
        const RrPassVerdict pv = rr_pass_verdict(s.live_n, s.prev_alive, s.insane_n, 3);
        if (pv == RPV_RETRY)
        {
            ++s.insane_n;
            msc_log(STR("RR seed incomplete saves=") + std::to_wstring(s.live_n) + STR(" known=") + std::to_wstring(s.prev_alive) + STR(" try=") +
                    std::to_wstring(s.insane_n) + STR(" (nothing killed, no cache write, retry)"));
            std::fill(s.seen.begin(), s.seen.end(), 0);
            s.world_wait = true;
            s.seed_ms = GetTickCount64();
            rr_seed_release(s);
            return;
        }
        const bool incomplete = pv == RPV_INCOMPLETE;
        if (incomplete) msc_log(STR("RR seed accepted as incomplete saves=") + std::to_wstring(s.live_n) + STR(" known=") + std::to_wstring(s.prev_alive) + STR(" (nothing killed, no cache write)"));
        const bool force_write = s.cache_verdict == RCV_RESEED;   // a reseed replaced the cached rows: the file is rewritten
        s.cache_verdict = -1;
        s.insane_n = 0;
        s.cache_pending = false;
        const size_t gone = (s.live_n && !incomplete) ? rr_kill_unseen(s.tab, s.seen, s.pass, s.ticks) : 0;
        s.tab.zones_ready(s.pass);
        s.self_due = true;
        const size_t alive_n = rr_alive_count(s);
        const size_t changed = s.changed + gone;
        msc_log(STR("RR seed done n=") + std::to_wstring(alive_n) + STR(" saves=") + std::to_wstring(s.live_n) + STR(" new=") +
                std::to_wstring(s.changed) + STR(" gone=") + std::to_wstring(gone) + STR(" pass=") + std::to_wstring(s.pass) + STR(" ms=") +
                std::to_wstring(GetTickCount64() - s.seed_ms));
        if (alive_n > 0 && !incomplete && GetTickCount64() >= s.cache_retry_at &&
            (!s.cache_ok || force_write || static_cast<double>(changed) > RR_CACHE_CHANGE_FRAC * static_cast<double>(alive_n)))
        {
            // the writer runs over the next ticks (rr_cache_write_step): tmp file, placeholder header, records
            RrCacheWrite& w = s.cw;
            w.dst = rr_dir() / L"RockRegCache.bin";
            w.tmp = w.dst;
            w.tmp += L".tmp";
            if (_wfopen_s(&w.f, w.tmp.c_str(), L"wb") != 0) w.f = nullptr;
            if (!w.f) rr_cache_fail(s, "open");
            else
            {
                uint8_t z[RR_CACHE_HDR] = {0};
                std::fwrite(z, 1, sizeof z, w.f);
                w.on = true;
                w.save_n = s.live_n;
                w.next = 0;
                w.rock_n = 0;
                w.sum = RR_CACHE_FNV0;
                w.buf.clear();
                w.t_begin = GetTickCount64();
            }
        }
        rr_seed_release(s);
    }

    // WHAT: writer: flushes the pending bytes to the temp file.
    void rr_cache_flush(RrCacheWrite& w)
    {
        if (w.buf.empty()) return;
        if (std::fwrite(w.buf.data(), 1, w.buf.size(), w.f) != w.buf.size()) throw std::runtime_error("write");
        w.sum = rr_cache_fnv(w.sum, w.buf.data(), w.buf.size());
        w.buf.clear();
    }

    // WHAT: a cache write failed (kind = open | I/O): backoff before the next try (rr_cache_retry_ms), the kind is logged once per session.
    void rr_cache_fail(RrState& s, const char* kind)
    {
        ++s.cache_fail_n;
        s.cache_retry_at = GetTickCount64() + rr_cache_retry_ms(s.cache_fail_n);
        if (s.cache_fail_logged.insert(kind).second)
            msc_log(STR("RR cache write failed: ") + rr_w(kind) + STR(" (next try in ") + std::to_wstring(rr_cache_retry_ms(s.cache_fail_n) / 1000) + STR(" s, doubling up to 1 h)"));
    }

    // WHAT: writer abort: closes and removes the temp file; the old cache (if any) stays. fail = false for an abort that is not a failure (world changed).
    void rr_cache_write_abort(RrState& s, const char* why, bool fail = true)
    {
        RrCacheWrite& w = s.cw;
        if (w.f) std::fclose(w.f);
        w.f = nullptr;
        w.on = false;
        std::error_code ec;
        std::filesystem::remove(w.tmp, ec);
        if (fail) rr_cache_fail(s, why);
    }

    // WHAT: writer: the tail (ore names, header with the checksum, close, rename). Run when every rock has been written.
    // WHY: records hold indexes of tab.kinds (append only), so the table's name list is the name table of the file.
    void rr_cache_write_finish(RrState& s)
    {
        RrCacheWrite& w = s.cw;
        const uint64_t kofs = RR_CACHE_HDR + w.rock_n * RR_CACHE_REC;
        rr_cache_put_kinds(w.buf, s.tab.kinds);
        rr_cache_flush(w);
        RrCacheHdr h;
        h.exe_size = s.exe_size;
        h.save_n = w.save_n;
        h.rock_n = w.rock_n;
        h.kinds_off = kofs;
        h.sum = w.sum;
        uint8_t hb[RR_CACHE_HDR];
        rr_cache_put_hdr(hb, h);
        if (std::fseek(w.f, 0, SEEK_SET) != 0 || std::fwrite(hb, 1, sizeof hb, w.f) != sizeof hb) throw std::runtime_error("header");
        const bool closed = std::fclose(w.f) == 0;
        w.f = nullptr;
        w.on = false;
        if (!closed) throw std::runtime_error("close");
        std::filesystem::rename(w.tmp, w.dst);
        s.cache_ok = true;
        s.cache_fail_n = 0;
        s.cache_retry_at = 0;
        msc_log(STR("RR cache written rocks=") + std::to_wstring(w.rock_n) + STR(" saves=") + std::to_wstring(w.save_n) + STR(" names=") +
                std::to_wstring(s.tab.kinds.size()) + STR(" ms=") + std::to_wstring(GetTickCount64() - w.t_begin));
    }

    // WHAT: one writer tick: up to budget_ms of records (encoded into buf, flushed in 64 KB pieces). Finishes when every rock is written.
    void rr_cache_write_step(RrState& s, double budget_ms)
    {
        RrCacheWrite& w = s.cw;
        if (!w.on) return;
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        try
        {
            while (w.next < s.tab.rocks.size() && !rr_over_budget(rr_perf_ms(t0), budget_ms))
            {
                const RrRock& r = s.tab.rocks[w.next++];
                if (!r.alive) continue;
                RrCacheRec c;
                c.id = r.id;
                c.sys = r.sys;
                std::copy(r.c, r.c + 3, c.c);
                c.rad = r.rad_known ? static_cast<float>(r.rad_m) : -1.0f;
                std::copy(r.ore, r.ore + 4, c.ore);
                uint8_t rec[RR_CACHE_REC];
                rr_cache_put_rec(rec, c);
                w.buf.insert(w.buf.end(), rec, rec + RR_CACHE_REC);
                ++w.rock_n;
                if (w.buf.size() >= 65536) rr_cache_flush(w);
            }
            if (w.next >= s.tab.rocks.size()) rr_cache_write_finish(s);
        }
        catch (...)
        {
            rr_cache_write_abort(s, "I/O");
        }
    }

    // WHAT: seed budget of this tick: RR_SEED_A_BUDGET_MS during phase A of the first RR_PHASE_A_MS, else RR_BUDGET_MS.
    double rr_seed_budget(const RrState& s, ULONGLONG now)
    {
        return (!s.near_done && now - s.seed_ms < RR_PHASE_A_MS) ? RR_SEED_A_BUDGET_MS : RR_BUDGET_MS;
    }

    // WHAT: the seed slice of this tick: steps through the pass (bind, sample, classify, order, read) until the budget is spent.
    void rr_seed_slice(RrState& s, double budget)
    {
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        while (s.seeding && !rr_over_budget(rr_perf_ms(t0), budget))
        {
            if (s.bind_cur < s.seed_list.size()) { rr_bind_one(s, s.seed_list[s.bind_cur++]); continue; }
            if (!s.sample_done) { rr_sample_stage(s); continue; }
            if (s.cls_cur < s.need.size()) { rr_classify_one(s, s.cls_cur++); continue; }
            if (!s.ordered)
            {
                rr_order_rings(s.ring, RR_PRIORITY_CHUNKS, s.order, s.near_n);
                s.ordered = true;
                rr_near_check(s);
                continue;
            }
            if (s.ord_cur < s.order.size())
            {
                if (rr_seed_one(s, s.need[s.order[s.ord_cur++]])) ++s.changed;
                rr_near_check(s);
                continue;
            }
            rr_seed_finish(s);
        }
    }

    // WHAT: targeted rocks: the merged targets of every feed become the registry's targeted set; a targeted rock whose save object is gone is killed.
    void rr_target_stage(RrState& s, bool may_kill)
    {
        std::set<int> want(s.targets.begin(), s.targets.end());
        std::vector<int> drop;
        for (int id : s.tab.targeted()) if (!want.count(id)) drop.push_back(id);
        for (int id : drop) s.tab.target_set(id, false);
        for (int id : want) s.tab.target_set(id, true);
        for (int id : want)
        {
            const size_t i = s.tab.index_of(id);
            if (i == RrTable::npos || !s.tab.rocks[i].alive || i >= s.save.size() || !s.save[i]) continue;
            if (!alive_seh(s.save[i])) { if (may_kill) s.tab.kill(id, s.ticks); rr_bind(s, i, nullptr); }
        }
    }

    // WHAT: GetRadius of the rocks near a ship, near an approach segment or targeted: at most RR_RAD_SEG_TICK + RR_RAD_TICK reads per tick for ALL ships
    //       together, and the loop stops when the tick budget is spent. A read that fails is NOT stored (the radius stays unknown); that rock is tried
    //       again after rr_rad_backoff_ms. ponytail: rocks in backoff still occupy candidate slots (4 x the read cap), so 200 permanently failing rocks near a ship could starve others.
    void rr_radius_stage(RrState& s, const std::vector<MsRegShip>& ships, ULONGLONG now, bool may_kill)
    {
        constexpr size_t CAP = RR_RAD_SEG_TICK + RR_RAD_TICK;
        constexpr size_t GATHER = CAP * 4;
        std::vector<size_t> want;
        for (const MsRegShip& sh : ships) if (sh.has_seg) s.tab.radius_wanted_seg(sh.sys, sh.p, sh.q, RR_RAD_SEG_M, GATHER, want);
        for (const MsRegShip& sh : ships) s.tab.radius_wanted(sh.sys, {{sh.p[0], sh.p[1], sh.p[2]}}, RR_RAD_NEAR_M, GATHER, want);
        for (int id : s.tab.targeted())
        {
            const size_t i = s.tab.index_of(id);
            if (i != RrTable::npos && !s.tab.rocks[i].rad_known && want.size() < GATHER && std::find(want.begin(), want.end(), i) == want.end()) want.push_back(i);
        }
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        size_t reads = 0;
        for (size_t i : want)
        {
            if (reads >= CAP || (reads > 0 && rr_over_budget(rr_perf_ms(t0)))) break;
            if (i >= s.save.size() || !s.tab.rocks[i].alive || s.tab.rocks[i].rad_known) continue;
            UObject* sv = s.save[i];
            if (!sv) continue;   // not bound yet
            if (now < s.rad_next[i]) continue;   // in backoff
            if (!alive_seh(sv)) { if (may_kill) s.tab.kill(s.tab.rocks[i].id, s.ticks); rr_bind(s, i, nullptr); continue; }
            ++reads;
            int64_t rad = 0;
            if (call_pnum(sv, STR("GetRadius"), rad))
            {
                s.tab.set_radius(i, static_cast<double>(rad) / RR_RAW_PER_M);
                s.rad_fail[i] = 0;
            }
            else
            {
                if (s.rad_fail[i] < 250) ++s.rad_fail[i];
                s.rad_next[i] = now + rr_rad_backoff_ms(s.rad_fail[i]);
            }
        }
    }

    // WHAT: validation of the save objects of the rocks within RR_NEAR_M of a ship, RR_VALID_TICK per tick (rotating).
    // may_kill = the world read has been OK for RR_NOWORLD_TICKS ticks in a row (rr_kill_allowed); otherwise a failed check only clears the pointer.
    void rr_validate_stage(RrState& s, const std::vector<MsRegShip>& ships, bool may_kill)
    {
        std::vector<size_t> cand;
        for (const MsRegShip& sh : ships) s.tab.within(sh.sys, sh.p, RR_NEAR_M, cand);
        rr_sort_unique(cand);
        size_t n = 0, last = 0;
        bool limited = false;
        for (size_t i : cand)
        {
            if (i < s.valid_cur) continue;
            if (n >= RR_VALID_TICK) { limited = true; break; }
            if (!s.tab.rocks[i].alive) continue;
            ++n;
            last = i + 1;
            if (i < s.save.size() && s.save[i] && !alive_seh(s.save[i])) { if (may_kill) s.tab.kill(s.tab.rocks[i].id, s.ticks); rr_bind(s, i, nullptr); }
        }
        s.valid_cur = limited ? last : 0;
    }

    // WHAT: one row of a compare: a registry rock and its loaded actor. Positions in m; rad_live = GetRadius of the save object (-1 = not read).
    struct RrCmpRow
    {
        int id = 0;
        size_t idx = 0;
        double reg[3] = {0, 0, 0}, act[3] = {0, 0, 0};
        double err = 0.0, dist = 0.0, rad_reg = 0.0, rad_live = -1.0;
    };
    struct RrSum { size_t n = 0; double max_e = 0.0, mean_e = 0.0; int worst_id = 0; };
    RrSum rr_sum(const std::vector<RrCmpRow>& rows)
    {
        RrSum sm;
        double sum = 0.0;
        for (const RrCmpRow& r : rows)
        {
            sum += r.err;
            ++sm.n;
            if (r.err > sm.max_e) { sm.max_e = r.err; sm.worst_id = r.id; }
        }
        sm.mean_e = sm.n ? sum / static_cast<double>(sm.n) : 0.0;
        return sm;
    }
    // WHAT: "SUMMARY n=.. max_err_m=.. mean_err_m=.. missing=..". missing < 0 = not checked (n/a).
    std::string rr_summary_line(size_t n, double max_e, double mean_e, long long missing)
    {
        char b[192];
        if (missing < 0) std::snprintf(b, sizeof(b), "SUMMARY n=%zu max_err_m=%.3f mean_err_m=%.3f missing=n/a", n, max_e, mean_e);
        else std::snprintf(b, sizeof(b), "SUMMARY n=%zu max_err_m=%.3f mean_err_m=%.3f missing=%lld", n, max_e, mean_e, missing);
        return b;
    }

    // WHAT: every loaded rock actor (FindAllOf kClsAsteroidActor, ObjectId -> actor) against the registry. Rows: the n_max nearest to c (any order
    //       when no c), with the actor position (K2_GetActorLocation / 100), the error, the registry radius and GetRadius of the save object.
    //       missing (when c is known) = loaded actors within RR_MISSING_M of c with no registry entry (coverage check). One loop serves both.
    void rr_cmp_fn(RrState& s, bool has_c, const double c[3], size_t n_max, std::vector<RrCmpRow>& rows, long long& missing_out)
    {
        std::map<int, UObject*> actor_by_id;
        std::vector<UObject*> acts;
        UObjectGlobals::FindAllOf(widen(kClsAsteroidActor).c_str(), acts);
        for (UObject* a : acts)
        {
            int32_t id = 0;
            if (alive_seh(a) && get_int_prop(a, STR("ObjectId"), id) && id > 0) actor_by_id[id] = a;
        }
        std::vector<RrCmpRow> cand;
        long long missing = 0;
        for (const auto& kv : actor_by_id)
        {
            UObject* a = kv.second;
            double ap[3] = {0, 0, 0};
            std::wstring why;
            if (!actor_vec_call(a, STR("K2_GetActorLocation"), ap, why)) continue;
            const double am[3] = {ap[0] / RR_UU_PER_M, ap[1] / RR_UU_PER_M, ap[2] / RR_UU_PER_M};
            const double dv[3] = {am[0] - (has_c ? c[0] : 0.0), am[1] - (has_c ? c[1] : 0.0), am[2] - (has_c ? c[2] : 0.0)};
            const double dist = has_c ? rr_norm3(dv) : 0.0;
            const size_t idx = s.tab.index_of(kv.first);
            if (idx == RrTable::npos || !s.tab.rocks[idx].alive)
            {
                if (has_c && dist <= RR_MISSING_M) ++missing;
                continue;
            }
            RrCmpRow r;
            r.id = kv.first;
            r.idx = idx;
            std::copy(s.tab.rocks[idx].c, s.tab.rocks[idx].c + 3, r.reg);
            std::copy(am, am + 3, r.act);
            r.dist = dist;
            cand.push_back(r);
        }
        if (has_c) std::sort(cand.begin(), cand.end(), [](const RrCmpRow& a, const RrCmpRow& b) { return a.dist < b.dist; });
        missing_out = has_c ? missing : -1;
        for (size_t k = 0; k < cand.size() && k < n_max; ++k)
        {
            RrCmpRow r = cand[k];
            const double d[3] = {r.act[0] - r.reg[0], r.act[1] - r.reg[1], r.act[2] - r.reg[2]};
            r.err = rr_norm3(d);
            r.rad_reg = s.tab.rocks[r.idx].rad_known ? s.tab.rocks[r.idx].rad_m : -1.0;
            UObject* sv = r.idx < s.save.size() ? s.save[r.idx] : nullptr;
            int64_t rad = 0;
            if (alive_seh(sv) && call_pnum(sv, STR("GetRadius"), rad)) r.rad_live = static_cast<double>(rad) / RR_RAW_PER_M;
            rows.push_back(r);
        }
    }

    // WHAT: runs rr_cmp_fn under SEH. false = a fault.
    bool rr_cmp_run(RrState& s, bool has_c, const double c[3], size_t n_max, std::vector<RrCmpRow>& rows, long long& missing)
    {
        missing = -1;
        return seh_run([&] { rr_cmp_fn(s, has_c, c, n_max, rows, missing); });
    }

    // WHAT: self-check (after each seed pass and every RR_SELF_MS): the registry against the loaded actors, centred on the first ship when there
    //       is one. Logs one 'RR selfcheck SUMMARY' line and keeps it for the RockRegDump.txt header.
    void rr_self_stage(RrState& s)
    {
        if (!s.self_due || s.seeding) return;
        s.self_due = false;
        s.self_ms = GetTickCount64();
        const bool has = !s.ships.empty();
        const double cc[3] = {has ? s.ships[0].p[0] : 0.0, has ? s.ships[0].p[1] : 0.0, has ? s.ships[0].p[2] : 0.0};
        std::vector<RrCmpRow> rows;
        long long missing = -1;
        if (!rr_cmp_run(s, has, cc, RR_SELF_N, rows, missing))
        {
            msc_log(STR("RR selfcheck fault"));
            return;
        }
        const RrSum sm = rr_sum(rows);
        s.self_summary = rr_summary_line(sm.n, sm.max_e, sm.mean_e, missing);
        msc_log(STR("RR selfcheck ") + rr_w(s.self_summary) + STR(" worst_id=") + std::to_wstring(sm.worst_id) + STR(" centre=") + (has ? STR("ship") : STR("none")));
    }

    // WHAT: one line per 60 s: counts, version, tick timing of the window.
    void rr_stats_stage(RrState& s)
    {
        size_t wr = 0, dead = 0;
        for (const RrRock& r : s.tab.rocks)
        {
            if (r.rad_known) ++wr;
            if (!r.alive) ++dead;
        }
        const double avg = s.stat_ticks ? s.ms_sum / static_cast<double>(s.stat_ticks) : 0.0;
        msc_log(STR("RR stats rocks=") + std::to_wstring(s.tab.rocks.size()) + STR(" with_radius=") + std::to_wstring(wr) + STR(" dead=") +
                std::to_wstring(dead) + STR(" version=") + std::to_wstring(s.tab.version) + STR(" ticks=") + std::to_wstring(s.stat_ticks) +
                STR(" tick_avg_ms=") + std::to_wstring(avg) + STR(" tick_worst_ms=") + std::to_wstring(s.ms_worst));
        s.ms_sum = 0.0;
        s.ms_worst = 0.0;
        s.stat_ticks = 0;
    }

    // WHAT: writes the RR_DUMP_N rocks nearest to (sys, p m) to rockreg\<name>: id, ore, abs m (x y z), distance, radius. Returns the row count.
    size_t rr_dump_file(const RrState& s, int sys, const double p[3], const wchar_t* name)
    {
        std::vector<std::pair<double, size_t>> out;
        s.tab.nearest_n(sys, -1, p, nullptr, RR_DUMP_M, RR_DUMP_N, out);
        std::string txt = "# RockRegDump: id ore abs_x_m abs_y_m abs_z_m dist_m radius_m\n# selfcheck " + s.self_summary + "\n";
        for (const auto& pr : out)
        {
            const RrRock& r = s.tab.rocks[pr.second];
            char b[512];
            std::snprintf(b, sizeof(b), "%d %s %.2f %.2f %.2f %.2f %.2f\n", r.id, rr_ore_name(s.tab, r).c_str(), r.c[0], r.c[1], r.c[2], pr.first, r.rad_m);
            txt += b;
        }
        rr_write_atomic(rr_dir() / name, txt);
        return out.size();
    }

    // WHAT: area check: true when the area around p (m, system sys, one chunk each way) is seeded. false = unknown: the caller uses the rocks
    //       already in the table. Logs once per pass.
    bool rr_area_ready(int sys, const double p[3])
    {
        RrState& s = rr_st();
        int64_t c[3] = {0, 0, 0};
        rr_chunk_of_m(p, c);
        if (s.tab.area_ready(sys, c, 1)) return true;
        if (!s.notready_logged)
        {
            s.notready_logged = true;
            msc_log(STR("RR area not ready sys=") + std::to_wstring(sys) + STR(" chunk=") + std::to_wstring(c[0]) + STR(",") +
                    std::to_wstring(c[1]) + STR(",") + std::to_wstring(c[2]) + STR(" (seeded rocks only)"));
        }
        return false;
    }

    // WHAT: drains the delete queue. Returns the rock indexes whose bound save object was deleted (sorted, unique); overflow = the queue overflowed.
    std::vector<size_t> rr_drain_dead(RrState& s, bool& overflow)
    {
        if (!g_rr_listener.load() && !g_rr_listener_gone.load()) { RrListener* l = new RrListener; g_rr_listener = l; UObjectArray::AddUObjectDeleteListener(l); }
        std::vector<const void*> dead;
        {
            RrDeadQ& d = rr_dead();
            std::lock_guard<std::mutex> l(d.mx);
            dead.swap(d.q);
            overflow = d.overflow;
            d.overflow = false;
        }
        std::vector<size_t> hit;
        for (const void* p : dead)
        {
            auto it = s.save_at.find(p);
            if (it != s.save_at.end() && it->second < s.save.size() && s.save[it->second] == p) hit.push_back(it->second);
        }
        std::sort(hit.begin(), hit.end());
        hit.erase(std::unique(hit.begin(), hit.end()), hit.end());
        return hit;
    }

    // WHAT: a different world was loaded or the world is gone: the table is emptied (its rows belong to the old world and must never be reused), every
    //       object pointer and the running pass are dropped, a running cache write is aborted, the cache state starts over (a later seed of the new world
    //       writes the cache again; an emptied table is never written: rr_seed_finish needs alive rocks).
    void rr_world_reset(RrState& s)
    {
        if (s.cw.on) rr_cache_write_abort(s, "world changed", false);
        rr_seed_abort(s);
        rr_seed_unbind(s);
        s.tab.clear();
        s.save.clear();
        s.seen.clear();
        s.rad_fail.clear();
        s.rad_next.clear();
        s.sys_done.clear();
        s.valid_cur = 0;
        s.cache_ok = false;
        s.cache_pending = false;
        s.cache_verdict = -1;
        s.insane_n = 0;
        s.prev_alive = 0;
        s.self_due = false;
        s.notready_logged = false;
    }

    // WHAT: the registry tick: deletes, world check, triggers, then the writer (writing ticks) or the seed slice (own budget), then the cheap stages
    //       (validate, radius; they also run on writing ticks) and the heavy ones (self-check, stats) on non-writing ticks. A stage is skipped when its budget is spent.
    // NOTE: no recovery code for a faulted tick: every export runs under seh_run (rr_run); a fault disables the module for the session.
    void rr_tick(RrState& s, const std::vector<MsRegShip>& ships, const std::vector<int>& targets)
    {
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        const ULONGLONG now = GetTickCount64();
        ++s.ticks;
        s.ships = ships;
        s.targets = targets;
        // deletes first (before any save is read this tick)
        bool overflow = false;
        const std::vector<size_t> hit = rr_drain_dead(s, overflow);
        // world probe EVERY tick, through the rr_world_step debounce (one failed player read changes nothing; RR_NOWORLD_TICKS in a row = LOST).
        RrWorldCtx wc;
        const bool rd = seh_run([&] { rr_world_probe(wc); }) && wc.sys > 0;
        const RrWorldEv we = rr_world_step(s.wt, rd ? wc.id : 0, RR_NOWORLD_TICKS);
        if (rd) s.world_sys = wc.sys;   // a warp only updates the system: pointers stay bound
        s.world_ok = s.wt.ok;
        if (rd) { if (s.rd_ok_ticks < 1000) ++s.rd_ok_ticks; } else s.rd_ok_ticks = 0;
        const bool may_kill = rr_kill_allowed(s.world_ok, s.rd_ok_ticks, RR_NOWORLD_TICKS);   // a failing / unknown world probe never kills
        const bool mass = overflow || hit.size() > RR_MASS_DEAD;
        const bool world_bad = we != RWE_NONE || !s.world_ok || mass;
        if (we != RWE_NONE)
        {
            msc_log(std::wstring(STR("RR world ")) + (we == RWE_LOADED ? STR("loaded") : STR("lost")) + STR(", table cleared"));
            rr_world_reset(s);
            s.seed_due = false;   // a pending seed must not skip the RR_EMPTY_RETRY_MS wait after the new world
            s.world_sys = rd ? wc.sys : 0;
        }
        else if (mass)
        {
            // a mass delete of save objects (a world unload): pointers dropped, nothing killed, the same rows are bound again by the next pass
            msc_log(STR("RR world changed (mass delete), pointers dropped"));
            rr_seed_unbind(s);
            s.seed_due = false;
        }
        else
        {
            for (size_t i : hit)   // the save object is gone: the rock is gone
            {
                if (i < s.save.size() && s.save[i]) { s.save_at.erase(s.save[i]); s.save[i] = nullptr; }
                if (s.tab.rocks[i].alive && !world_bad && !s.world_wait && may_kill) s.tab.kill(s.tab.rocks[i].id, s.ticks);   // unreadable world: only clear the pointer
            }
        }
        if (world_bad)
        {
            rr_seed_abort(s);
            s.seed_ms = now;
            s.world_wait = true;
        }
        if (!world_bad && !s.world_wait) rr_target_stage(s, may_kill);
        // seed triggers, no ship needed: first tick, 10 min refresh, world loaded / incomplete pass (after RR_EMPTY_RETRY_MS), no alive rock (retry every 5 s),
        // a ship in a system no pass covered (also restarts a running pass)
        if (now - s.trig_ms >= RR_TRIG_MS)
        {
            s.trig_ms = now;
            if (s.world_wait && s.world_ok && now - s.seed_ms >= RR_EMPTY_RETRY_MS) { s.world_wait = false; s.seed_due = true; }
            if (!s.seeding && s.world_ok && s.tab.alive_count() == 0 && now - s.seed_ms >= RR_EMPTY_RETRY_MS) s.seed_due = true;
        }
        bool sys_new = false;
        if (now - s.sys_ms >= RR_SYS_MS)
        {
            s.sys_ms = now;
            for (const MsRegShip& sh : ships) if (sh.sys > 0 && !s.sys_done.count(sh.sys)) { sys_new = true; break; }
        }
        if (s.cw.on)
        {
            rr_cache_write_step(s, RR_CACHE_WRITE_MS);   // a writing tick: the writer, then only the cheap stages
        }
        else
        {
            if (!s.seeding)
            {
                if (s.seed_ms == 0 || now - s.seed_ms >= RR_SEED_REFRESH_MS) s.seed_due = true;
                if ((s.seed_due || sys_new) && s.world_ok && !s.world_wait) { s.seed_due = false; rr_seed_begin(s, ships); }
            }
            else if (sys_new && s.world_ok && !s.world_wait)
            {
                rr_seed_abort(s);
                rr_seed_begin(s, ships);
            }
            if (s.seeding) s.seed_due = false;
            if (s.seeding && !s.world_ok) { rr_seed_abort(s); rr_seed_unbind(s); s.seed_ms = now; }
            if (s.seeding) rr_seed_slice(s, rr_seed_budget(s, now));
            // stats and dump: not budget-gated (cheap, once a minute)
            if (now - s.stats_ms >= RR_STATS_MS)
            {
                s.stats_ms = now;
                rr_stats_stage(s);
                if (!ships.empty()) rr_dump_file(s, ships[0].sys, ships[0].p, L"RockRegDump.txt");
            }
        }
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        if (!rr_over_budget(rr_perf_ms(t1))) rr_validate_stage(s, ships, may_kill);
        if (!world_bad && !s.world_wait && !rr_over_budget(rr_perf_ms(t1))) rr_radius_stage(s, ships, now, may_kill);
        if (!s.cw.on)
        {
            if (!rr_over_budget(rr_perf_ms(t1))) rr_self_stage(s);
            if (s.self_ms == 0 && !s.seeding) s.self_ms = now;   // the periodic self-check counts from the first full pass
            if (!s.seeding && !s.self_due && now - s.self_ms >= RR_SELF_MS) s.self_due = true;
        }
        const double ms = rr_perf_ms(t0);
        s.ms_sum += ms;
        if (ms > s.ms_worst) s.ms_worst = ms;
        ++s.stat_ticks;
        if (rr_over_budget(ms, RR_SEED_A_BUDGET_MS) && (s.slow_ms == 0 || now - s.slow_ms >= RR_SLOW_LOG_MS))
        {
            s.slow_ms = now;
            msc_log(STR("RR slow ms=") + std::to_wstring(ms) + STR(" rocks=") + std::to_wstring(s.tab.rocks.size()));
        }
    }

    // WHAT: merges the feeds younger than RR_FEED_MAX_MS: ships by sid (first feed wins), targets as a set. Older feeds are dropped.
    void rr_merge(RrState& s, ULONGLONG now, std::vector<MsRegShip>& ships, std::vector<int>& targets)
    {
        std::set<int> sids, tg;
        for (auto it = s.feeds.begin(); it != s.feeds.end();)
        {
            if (now - it->second.ms >= RR_FEED_MAX_MS) { it = s.feeds.erase(it); continue; }
            for (const MsRegShip& sh : it->second.ships) if (sids.insert(sh.sid).second) ships.push_back(sh);
            tg.insert(it->second.targets.begin(), it->second.targets.end());
            ++it;
        }
        targets.assign(tg.begin(), tg.end());
    }

    // WHAT: stores the feed of owner and runs rr_tick when RR_TICK_MS passed since the last tick (both mods fed = one tick).
    void rr_feed(int owner, const MsRegShip* ships, int n, const int* targets, int nt)
    {
        RrState& s = rr_st();
        const ULONGLONG now = GetTickCount64();
        RrFeed& f = s.feeds[owner];
        f.ms = now;
        n = (ships && n > 0) ? std::min(n, RR_MAX_FEED_N) : 0;
        nt = (targets && nt > 0) ? std::min(nt, RR_MAX_FEED_N) : 0;
        f.ships.clear();
        for (int i = 0; i < n; ++i)   // a ship with a bad position is dropped; a bad approach target clears has_seg
        {
            MsRegShip sh = ships[i];
            if (!rr_ok3(sh.p)) continue;
            if (sh.has_seg && !rr_ok3(sh.q)) sh.has_seg = 0;
            f.ships.push_back(sh);
        }
        f.targets.assign(targets, targets + nt);
        if (s.last_tick_ms != 0 && now - s.last_tick_ms < RR_TICK_MS) return;
        std::vector<MsRegShip> ms;
        std::vector<int> tg;
        rr_merge(s, now, ms, tg);
        s.last_tick_ms = now;
        rr_tick(s, ms, tg);
    }

    // WHAT: registers the cap row 'rockreg' (ok) once; it depends on 'rocks' (same class and save fields). Call after scan_resolve().
    void rr_resolve()
    {
        static bool done = false;
        if (done) return;
        done = true;
        caps_add("rockreg", true, "");
        caps_dep(STR("rockreg"), STR("rocks"));
    }

    // WHAT: selftest (check "rockreg"): one kClsAsteroid object has ResourceItems, bAlive and GetRadius. Missing = layout_changed, module off.
    //       SKIP (false, detail starts with SKIP) when the world holds no asteroid.
    bool rr_selftest(std::string& detail)
    {
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(widen(kClsAsteroid).c_str(), v);
        UObject* o = nullptr;
        for (UObject* x : v) if (alive_seh(x)) { o = x; break; }
        if (!o) { detail = "SKIP no asteroids (world not loaded)"; return false; }
        bool b = false;
        const bool items = o->GetPropertyByNameInChain(STR("ResourceItems")) != nullptr;
        const bool al = get_bool_prop(o, STR("bAlive"), b);
        const bool rad = o->GetFunctionByNameInChain(STR("GetRadius")) != nullptr;
        if (!items || !al || !rad)
        {
            detail = std::string("layout changed:") + (items ? "" : " ResourceItems") + (al ? "" : " bAlive") + (rad ? "" : " GetRadius");
            module_disable(STR("rockreg"), STR("layout_changed"));
            return false;
        }
        detail = "ResourceItems, bAlive, GetRadius present; " + std::to_string(rr_st().tab.rocks.size()) + " rocks in the table";
        return true;
    }

    // WHAT: runs an export body f (returns int) under SEH. -2 when the module is off, -2 after a fault (module off), -1 after an exception (module off).
    template <class F>
    int rr_run(F&& f)
    {
        if (!module_ok(STR("rockreg"))) return -2;
        int r = 0;
        try
        {
            if (!seh_run([&] { r = f(); }))
            {
                msc_log(STR("RR native fault: rockreg OFF for this session"));
                module_disable(STR("rockreg"), STR("native_fault"));
                return -2;
            }
        }
        catch (...) { module_disable(STR("rockreg"), STR("exception")); return -1; }
        return r;
    }

    // WHAT: fills an MsRegRock from a table row.
    void rr_fill(const RrRock& r, MsRegRock& o)
    {
        o.id = r.id;
        o.sys = r.sys;
        o.flags = (r.alive ? 1 : 0) | (r.rad_known ? 2 : 0);
        o.pad = 0;
        std::copy(r.c, r.c + 3, o.c);
        o.rad_m = r.rad_known ? r.rad_m : 0.0;
    }

    // WHAT: the report body of MSCore_rockreg_report: kind 0 dump (rows around p), 1 query (arg = radius m), 2 check (arg = n rocks). Returns the text.
    std::wstring rr_report(int kind, int sys, const double p[3], double arg)
    {
        RrState& s = rr_st();
        char b[320];
        if (!rr_ok3(p)) return STR("error: position must be finite and within 1e12 m");
        if (kind == 0)
        {
            const size_t n = rr_dump_file(s, sys, p, L"RockRegDump.txt");
            return STR("ok dump rows=") + std::to_wstring(n) + STR(" (RockRegDump.txt)");
        }
        if (kind == 1)
        {
            double rad = 0.0;
            if (!rr_len(arg, rad) || !(rad > 0.0)) return STR("error: radius must be > 0");
            arg = rad;
            std::vector<std::pair<double, size_t>> out;
            s.tab.nearest_n(sys, -1, p, nullptr, arg, RR_LIST_N, out);
            std::string txt;
            std::snprintf(b, sizeof(b), "# RockRegQuery sys=%d pos_m=(%.2f,%.2f,%.2f) radius_m=%.2f rows=%zu (nearest first)\n", sys, p[0], p[1], p[2], arg, out.size());
            txt += b;
            txt += "# columns: rank id dist_m rad_m ore abs_x_m abs_y_m abs_z_m\n";
            for (size_t k = 0; k < out.size(); ++k)
            {
                const RrRock& r = s.tab.rocks[out[k].second];
                std::snprintf(b, sizeof(b), "%zu %d %.2f %.2f %s %.2f %.2f %.2f\n", k + 1, r.id, out[k].first, r.rad_m, rr_ore_name(s.tab, r).c_str(), r.c[0], r.c[1], r.c[2]);
                txt += b;
            }
            if (!rr_write_atomic(rr_dir() / L"RockRegQuery.txt", txt)) return STR("error: RockRegQuery.txt not written");
            return STR("ok rocks=") + std::to_wstring(out.size()) + STR(" (RockRegQuery.txt)");
        }
        if (kind == 2)
        {
            size_t n = (std::isfinite(arg) && arg >= 1.0) ? static_cast<size_t>(std::min(arg, 1000.0)) : 30;
            n = std::min(n, RR_CHECK_MAX_N);
            std::vector<RrCmpRow> rows;
            long long missing = -1;
            if (!rr_cmp_run(s, true, p, n, rows, missing)) return STR("error: probe fault");
            const RrSum sm = rr_sum(rows);
            const std::string summary = rr_summary_line(sm.n, sm.max_e, sm.mean_e, missing);
            std::string txt;
            std::snprintf(b, sizeof(b), "# RockRegCheck sys=%d pos_m=(%.2f,%.2f,%.2f) requested=%zu rows=%zu\n", sys, p[0], p[1], p[2], n, rows.size());
            txt += b;
            txt += "# columns: id reg_x_m reg_y_m reg_z_m act_x_m act_y_m act_z_m err_m reg_rad_m live_rad_m dist_m (rad -1 = not read)\n";
            for (const RrCmpRow& r : rows)
            {
                std::snprintf(b, sizeof(b), "%d %.2f %.2f %.2f %.2f %.2f %.2f %.3f %.2f %.2f %.2f\n", r.id, r.reg[0], r.reg[1], r.reg[2], r.act[0], r.act[1], r.act[2], r.err, r.rad_reg, r.rad_live, r.dist);
                txt += b;
            }
            txt += summary + "\n";
            if (!rr_write_atomic(rr_dir() / L"RockRegCheck.txt", txt)) return STR("error: RockRegCheck.txt not written");
            msc_log(STR("RR check n=") + std::to_wstring(n) + STR(" ") + rr_w(summary));
            return STR("ok RockRegCheck.txt rows=") + std::to_wstring(rows.size()) + STR(" ") + rr_w(summary);
        }
        return STR("error: kind must be 0 dump, 1 query, 2 check");
    }
}
