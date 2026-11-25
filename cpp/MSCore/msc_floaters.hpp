// FILE: cpp/MSCore/msc_floaters.hpp
// WHAT: floaters module (default ON): floating ore per sector. Rows = sectors that hold at least one floater, count desc then id asc. Read-only.
// WHY: Fleet cleaners pick the sector with the most floating ore; the game names (FloatingOreObject, SpawnChunk, sector chunk) live only here.
// READ: FindAllOf FloatingOreObject (sim objects, subclasses included by the engine) cached 10 s (flo_refresh); each object binned to the containing sector by chunk (msc_floaters_logic.hpp).
//       One-time PROBE floaters line: totals, sector count, unbound, class histogram of the found objects, and crates = count of ItemCrateObject (count only, not binned).
// SAFETY: off when Mods\MSCore\floaters_off.txt exists (g_flo_off). Every raw access under seh_run; a fault turns module 'floaters' off. Writes nothing.
// CAPS: module 'floaters' (floaters_resolve). Kill switch: module_disable("floaters").
// USED BY: dllmain.cpp (floaters_resolve, check_one "floaters", MSCore_floaters). Test: tests/test_floaters_logic.cpp.
// UNVERIFIED (static RE only until PROBE floaters): class FloatingOreObject, sim objects expose a chunk (get_chunk / SpawnChunk), sim objects exist for unloaded sectors.
// DEPENDS ON: msc_floaters_logic.hpp, msc_warp.hpp (get_chunk, sec_axes, sys_id, sectors_fill), msc_contract.hpp, msc_caps.hpp, msc_ue.hpp.
#pragma once
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "msc_floaters_logic.hpp"

namespace
{
    constexpr ULONGLONG kFloCacheMs = 10000;  // floater snapshot max age (FindAllOf over ~10k objects is not free; Fleet asks rarely)

    std::atomic<bool> g_flo_off{false};       // marker Mods\MSCore\floaters_off.txt (default ON)

    struct FloSnap
    {
        ULONGLONG at = 0;
        bool valid = false;
        std::vector<MsFloaterSec> rows;
        std::vector<std::pair<std::wstring, int>> hist;   // class name -> count of the FloatingOreObject scan, count desc
        int total = 0, readable = 0, unbound = 0, nsec = 0, crates = -1;
    };
    FloSnap g_flo_snap;
    std::mutex g_flo_mx;                      // guards g_flo_snap (flo_refresh holds it from floaters_read only)
    std::mutex g_flo_log_mx;                  // guards g_flo_logged (flo_refresh runs with or without g_flo_mx)
    bool g_flo_logged = false;                // PROBE floaters once

    void floaters_resolve() { static bool done = false; if (done) return; done = true; caps_add("floaters", true, ""); }
    bool floaters_enabled() { return !g_flo_off.load() && module_ok(STR("floaters")); }

    // "Name:n,Name:n" of the first max entries (class names are ASCII)
    std::wstring flo_hist_text(const std::vector<std::pair<std::wstring, int>>& h, size_t max)
    {
        std::wstring s;
        for (size_t i = 0; i < h.size() && i < max; ++i)
        {
            if (i) s += STR(",");
            s += h[i].first + STR(":") + widen(std::to_string(h[i].second));
        }
        return s;
    }

    std::string flo_narrow(const std::wstring& w) { std::string s; for (wchar_t c : w) s.push_back(c < 128 ? char(c) : '?'); return s; }

    // WHAT: fresh scan of floating ore objects: pts = readable ones (sys + chunk), total = alive objects, classes = class histogram. false on a SEH fault only.
    bool flo_scan(std::vector<FlPt>& pts, int& total, std::map<std::wstring, int>& classes)
    {
        pts.clear();
        total = 0;
        std::vector<UObject*> all;
        if (!seh_run([&] { UObjectGlobals::FindAllOf(widen(std::string(cls_name("floating_ore_object"))).c_str(), all); })) return false;
        for (UObject* o : all)
        {
            FlPt p{};
            bool keep = false;
            const bool ok = seh_run([&] {
                if (!alive(o) || o->GetName().rfind(STR("Default__"), 0) == 0) return;
                ++total;
                ++classes[std::wstring(o->GetClassPrivate()->GetName())];
                int64_t ch[3];
                if (!get_chunk(o, ch) && !sec_axes(o, STR("SpawnChunk"), ch)) return;
                p.sys = sys_id(o);
                if (p.sys <= 0) return;
                for (int k = 0; k < 3; ++k) p.ch[k] = ch[k];
                keep = true;
            });
            if (!ok) return false;
            if (keep) pts.push_back(p);
        }
        return true;
    }

    // WHAT: one full refresh into n (rows, counts, histogram). false on a SEH fault in the floater or sector read. Item crates are counted only; their fault is not fatal (crates = -1).
    bool flo_refresh(FloSnap& n)
    {
        std::vector<FlPt> pts;
        int total = 0;
        std::map<std::wstring, int> classes;
        if (!flo_scan(pts, total, classes)) return false;

        int crates = -1;
        seh_run([&] {
            std::vector<UObject*> cr;
            UObjectGlobals::FindAllOf(widen(std::string(cls_name("item_crate_object"))).c_str(), cr);
            crates = static_cast<int>(cr.size());
        });

        std::vector<MsSector> sec;
        int cnt = 0, got = 0;
        if (!seh_run([&] { cnt = sectors_fill(nullptr, 0); sec.resize(size_t(cnt < 0 ? 0 : cnt) + 16); got = sectors_fill(sec.data(), int(sec.size())); })) return false;
        if (got > int(sec.size())) got = int(sec.size());
        if (got < 0) got = 0;
        std::vector<FlSecBox> boxes;
        boxes.reserve(size_t(got));
        for (int i = 0; i < got; ++i)
        {
            FlSecBox b{};
            b.id = sec[size_t(i)].id;
            b.sys = sec[size_t(i)].sys;
            for (int k = 0; k < 3; ++k) { b.ch[k] = sec[size_t(i)].ch[k]; b.he[k] = sec[size_t(i)].he[k]; }
            boxes.push_back(b);
        }

        int unbound = 0;
        n.rows = fl_bin(boxes, pts, unbound);
        n.total = total;
        n.readable = int(pts.size());
        n.unbound = unbound;
        n.nsec = got;
        n.crates = crates;
        n.hist.assign(classes.begin(), classes.end());
        std::sort(n.hist.begin(), n.hist.end(), [](const std::pair<std::wstring, int>& a, const std::pair<std::wstring, int>& b) { return a.second > b.second || (a.second == b.second && a.first < b.first); });

        {
            std::lock_guard<std::mutex> lk(g_flo_log_mx);
            if (!g_flo_logged)
            {
                g_flo_logged = true;
                msc_log(STR("PROBE floaters total=") + widen(std::to_string(n.total)) + STR(" readable=") + widen(std::to_string(n.readable)) +
                        STR(" sectors=") + widen(std::to_string(n.nsec)) + STR(" rows=") + widen(std::to_string(n.rows.size())) +
                        STR(" unbound=") + widen(std::to_string(n.unbound)) + STR(" classes=") + flo_hist_text(n.hist, 12) +
                        STR(" crates=") + widen(std::to_string(n.crates)));
            }
        }
        return true;
    }

    // WHAT: sector rows with floating ore, most first, copied to out (max rows). Returns the row count.
    // RETURNS: -2 module off, -1 scan fault (module turned off), >= 0 row count.
    int floaters_read(MsFloaterSec* out, int max)
    {
        if (!floaters_enabled()) return -2;
        std::lock_guard<std::mutex> lk(g_flo_mx);
        const ULONGLONG now = GetTickCount64();
        if (!g_flo_snap.valid || now - g_flo_snap.at >= kFloCacheMs)
        {
            FloSnap n;
            if (!flo_refresh(n))
            {
                module_disable(STR("floaters"), STR("floaters scan fault"));
                g_flo_snap = FloSnap();
                return -1;
            }
            n.valid = true;
            n.at = now;
            g_flo_snap = std::move(n);
        }
        if (out && max > 0) for (size_t i = 0; i < g_flo_snap.rows.size() && static_cast<int>(i) < max; ++i) out[i] = g_flo_snap.rows[i];
        return static_cast<int>(g_flo_snap.rows.size());
    }

    // WHAT: read-only selftest: the scan must not fault; zero floaters is fine (none loaded); with floaters at least one needs a readable chunk.
    bool floaters_selftest(std::string& detail)
    {
        FloSnap n;
        if (!flo_refresh(n)) { detail = "floaters scan faulted"; return false; }
        detail = "floaters=" + std::to_string(n.total) + " readable=" + std::to_string(n.readable) + " sectors_rows=" + std::to_string(n.rows.size()) +
                 " unbound=" + std::to_string(n.unbound) + " classes=" + flo_narrow(flo_hist_text(n.hist, 5));
        if (n.total == 0) { detail += " (none found)"; return true; }
        return n.readable > 0;
    }
}
