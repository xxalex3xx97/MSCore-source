// msc_savescrub.hpp - Remove-mod save scrubber for Main Sequence *_S.sav (port of the verified python prototypes
// scrub.py / revert_stock.py). Header only, no game code, no DLL use (manager + tests). Needs msc_loader.hpp first.
//
// Why: the save's class table holds every class path used by saved objects; the game loads each by path and aborts
// ("Failed to load class") when the mod's pak is gone. A mod declares `save_scrub=<modded>><stock>` (see SaveScrub).
// Per save, per pair (copy-on-write, original untouched until verified):
//   nodes [0|65536][0][classIdx][id<=NextId][0] using the modded table index -> stock index
//   class table entry of the modded class -> an unused vanilla class (kStandins)
//   Statistics flat map [u32 N][N x (FString path, u32 count)]: modded count merged into the stock entry, key removed, N-1
//   (stock entry absent: key renamed); other length-prefixed path strings (per-ship tables) -> stock path, same length only
// Format facts: u32 0, u32 version, u32 N, N x FString, root block (1,1,5,2,3,4,NextId,Ticks), tree without lengths.
#pragma once
#include "msc_loader.hpp"

namespace mscl {

struct ScrubRep {
    int nodes = 0, table = 0, paths = 0, merged = 0, renamed = 0, removed = 0, lists = 0;
};

enum class ScrubSt { Skip, Ok, Refused };

struct ScrubOut {
    ScrubSt st = ScrubSt::Skip;
    std::string why;   // Refused: reason
    std::string data;  // Ok: the scrubbed save
    ScrubRep rep;
};

namespace ss {

inline uint32_t U(const std::string& b, size_t o) {
    if (o + 4 > b.size()) return 0xFFFFFFFFu;
    uint32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
inline std::string pk(uint32_t v) { return std::string(reinterpret_cast<const char*>(&v), 4); }
inline std::string fstr(const std::string& s) { return pk(static_cast<uint32_t>(s.size() + 1)) + s + std::string(1, '\0'); }

// vanilla classes (all exist in the stock pak) that can take an unused table slot; first one not in the table wins
inline const std::vector<std::string>& standins() {
    static const std::vector<std::string> v = {
        "/Game/Blueprints/BuildingBlocks/BPO_LadderBlock.BPO_LadderBlock_C",
        "/Game/Blueprints/BuildingBlocks/BPO_Station_2x1.BPO_Station_2x1_C",
        "/Game/Blueprints/BuildingBlocks/BPO_ScannerModule.BPO_ScannerModule_C",
        "/Game/Blueprints/Machines/BPO_Ladder_01.BPO_Ladder_01_C",
        "/Game/Blueprints/Machines/BPO_SignWall.BPO_SignWall_C",
        "/Game/Blueprints/Machines/BPO_Elevator.BPO_Elevator_C",
        "/Game/Blueprints/Machines/BPO_Teleporter.BPO_Teleporter_C",
        // more vanilla classes (each seen in real saves) so a save that already uses the first seven still has free slots
        "/Game/Blueprints/BuildingBlocks/BPO_Station_03.BPO_Station_03_C",
        "/Game/Blueprints/BuildingBlocks/BPO_StationAdapter_01.BPO_StationAdapter_01_C",
        "/Game/Blueprints/BuildingBlocks/BPO_StationCorridor_02.BPO_StationCorridor_02_C",
        "/Game/Blueprints/BuildingBlocks/BPO_Connector_07T.BPO_Connector_07T_C",
        "/Game/Blueprints/BuildingBlocks/BPO_Connector_07B.BPO_Connector_07B_C",
        "/Game/Blueprints/BuildingBlocks/BPO_XLargeConnector.BPO_XLargeConnector_C",
        "/Game/Blueprints/Machines/BPO_ElevatorFiller.BPO_ElevatorFiller_C",
        "/Game/Blueprints/Machines/BPO_BeltBalancer_03.BPO_BeltBalancer_03_C"};
    return v;
}

struct Parsed {
    uint32_t version = 0, nextid = 0, ticks = 0;
    std::vector<std::string> table;
    size_t tab_end = 0;
};

inline bool parse(const std::string& b, Parsed& P, std::string& why) {
    if (b.size() < 12 || U(b, 0) != 0 || U(b, 4) < 1 || U(b, 4) > 1000) {
        why = "not a Main Sequence _S save";
        return false;
    }
    uint32_t n = U(b, 8);
    if (n > 100000) {
        why = "class table count out of range";
        return false;
    }
    size_t o = 12;
    P = Parsed{};
    P.version = U(b, 4);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t l = U(b, o);
        if (!(l > 2 && l < 400) || o + 4 + l > b.size() || b[o + 3 + l] != 0) {
            why = "bad class table entry " + std::to_string(i);
            return false;
        }
        P.table.push_back(b.substr(o + 4, l - 1));
        o += 4 + l;
    }
    static const uint32_t want[6] = {1, 1, 5, 2, 3, 4};
    for (int i = 0; i < 6; ++i)
        if (U(b, o + 4 * i) != want[i]) {
            why = "root block unexpected";
            return false;
        }
    P.nextid = U(b, o + 24);
    P.ticks = U(b, o + 28);
    P.tab_end = o;
    return true;
}

struct Ent {
    size_t off, len;
};

// consecutive [FString '/..', u32] entries starting at p
inline std::vector<Ent> run(const std::string& b, size_t p) {
    std::vector<Ent> ents;
    size_t o = p;
    while (o + 8 <= b.size()) {
        uint32_t l = U(b, o);
        if (!(l > 5 && l < 300) || o + 4 + l + 4 > b.size() || b[o + 3 + l] != 0 || b[o + 4] != 0x2f) break;
        ents.push_back({o, 8 + static_cast<size_t>(l)});
        o += 8 + l;
    }
    return ents;
}

struct FMap {
    size_t p;
    uint32_t N;
    size_t occ, len;
};

// flat TMap<FString,u32> holding `path` after `start`; true start = candidate whose contiguous run is exactly N (outermost)
inline std::vector<FMap> find_flatmaps(const std::string& b, const std::string& path, size_t start) {
    std::string key = fstr(path);
    std::vector<FMap> res;
    for (size_t pos = b.find(key); pos != std::string::npos; pos = b.find(key, pos + key.size())) {
        size_t occ = pos;
        if (occ < start) continue;
        size_t lo = occ > 400 * 320 ? occ - 400 * 320 : 0;
        if (lo < start) lo = start;
        for (size_t p = lo; p + 3 < occ; ++p) {
            uint32_t N = U(b, p);
            if (N < 1 || N > 400) continue;
            uint32_t l0 = U(b, p + 4);  // cheap reject: the run must start with a valid '/..' entry (same result as run() below)
            if (!(l0 > 5 && l0 < 300) || p + 8 >= b.size() || b[p + 8] != 0x2f) continue;
            std::vector<Ent> ents = run(b, p + 4);
            if (ents.size() != N) continue;
            bool hit = false;
            size_t ln = 0;
            for (const auto& e : ents)
                if (e.off == occ) {
                    hit = true;
                    ln = e.len;
                    break;
                }
            if (hit) {
                res.push_back({p, N, occ, ln});
                break;
            }
        }
    }
    return res;
}

// Candidate object nodes of class index idx. A node is [classIdx][id][0] followed by the block/machine base payload whose
// fixed words are ... [0][6][parent id] at +44 / +48 / +52 (same for every BuildingBlockObject and machine node seen in the
// real saves; both the block and the machine node of a Mining Unit have it). The words BEFORE the node are NOT used: a node
// that follows a child-bearing node or is the first child has any value there (that is what the old signature missed).
// Needs 1 <= id <= NextId, and the id unique among the hits of this idx (a repeated id is a repeated record, not an object).
inline bool node_shape(const std::string& b, size_t pos, const Parsed& P) {
    return U(b, pos + 4) >= 1 && U(b, pos + 4) <= P.nextid && U(b, pos + 8) == 0 && U(b, pos + 44) == 0 && U(b, pos + 48) == 6 &&
           U(b, pos + 52) >= 1 && U(b, pos + 52) <= P.nextid;
}

inline std::vector<size_t> usage(const std::string& b, uint32_t idx, const Parsed& P) {
    std::vector<size_t> hits, r;
    std::string key = pk(idx);
    for (size_t pos = b.find(key); pos != std::string::npos; pos = b.find(key, pos + 4))
        if (pos > P.tab_end && node_shape(b, pos, P)) hits.push_back(pos);
    std::map<uint32_t, int> cnt;
    for (size_t h : hits) ++cnt[U(b, h + 4)];
    for (size_t h : hits)
        if (cnt[U(b, h + 4)] == 1) r.push_back(h);
    return r;
}

// Fail-closed residual scan: after the rewrite, every plausible reference to a neutralised class index that is not
// understood. Plausible = [idx][id in 1..NextId][0] preceded by a 0 word (what a sibling node looks like) with an id that is
// not repeated by another hit (repeated ids are repeated records, e.g. the embedded blueprint copies). Returns offsets.
inline std::vector<size_t> residual(const std::string& b, uint32_t idx, const Parsed& P) {
    std::vector<size_t> hits, r;
    std::string key = pk(idx);
    for (size_t pos = b.find(key); pos != std::string::npos; pos = b.find(key, pos + 4))
        if (pos > P.tab_end && pos >= 4 && U(b, pos + 4) >= 1 && U(b, pos + 4) <= P.nextid && U(b, pos + 8) == 0) hits.push_back(pos);
    std::map<uint32_t, int> cnt;
    for (size_t h : hits) ++cnt[U(b, h + 4)];
    for (size_t h : hits)
        if (cnt[U(b, h + 4)] == 1 && U(b, h - 4) == 0) r.push_back(h);
    return r;
}

struct Edit {
    size_t a, e;
    std::string d;
    bool nmark = false;
    size_t p = 0;
};

inline std::string name_of(const std::string& b, const Ent& e) { return b.substr(e.off + 4, e.len - 9); }

inline std::vector<std::pair<std::string, uint32_t>> mapdict(const std::string& b, size_t p) {
    std::vector<std::pair<std::string, uint32_t>> r;
    for (const auto& e : run(b, p + 4)) r.push_back({name_of(b, e), U(b, e.off + e.len - 4)});
    return r;
}

inline std::string apply_edits(const std::string& b, const std::vector<Edit>& edits) {
    std::string out;
    size_t pos = 0;
    for (const auto& e : edits) {
        out.append(b, pos, e.a - pos);
        out += e.d;
        pos = e.e;
    }
    out.append(b, pos, std::string::npos);
    return out;
}

// maps (deduped by start, ordered) that hold the stock path or one of the modded paths mapped to it
inline std::vector<FMap> maps_for(const std::string& b, const std::vector<std::string>& paths, size_t start) {
    std::map<size_t, FMap> m;
    for (const auto& s : paths)
        for (const auto& f : find_flatmaps(b, s, start)) m.emplace(f.p, f);
    std::vector<FMap> v;
    for (auto& kv : m) v.push_back(kv.second);
    return v;
}

// independent check of the output; "" = fine
inline std::string verify(const std::string& a, const std::string& c, const std::map<uint32_t, uint32_t>& mp,
                          const std::vector<SaveScrub>& active) {
    Parsed A, C;
    std::string why;
    if (!parse(a, A, why) || !parse(c, C, why)) return "re-parse failed: " + why;
    if (A.version != C.version || A.nextid != C.nextid || A.ticks != C.ticks || U(a, 0) != U(c, 0)) return "header/root changed";
    if (A.table.size() != C.table.size()) return "class table size changed";
    for (size_t i = 0; i < A.table.size(); ++i) {
        if (mp.count(static_cast<uint32_t>(i))) {
            if (C.table[i] == A.table[i]) return "modded table entry not replaced";
            for (const auto& t : A.table)
                if (t == C.table[i]) return "stand-in already in table";
        } else if (C.table[i] != A.table[i]) {
            return "table changed at " + std::to_string(i);
        }
    }
    for (const auto& s : active)
        if (c.find(s.modded) != std::string::npos) return "modded path left: " + s.modded;
    std::map<uint32_t, size_t> expect;
    for (const auto& kv : mp) {
        if (!usage(c, kv.first, C).empty()) return "modded idx still used";
        expect[kv.second] += usage(a, kv.first, A).size();
    }
    for (const auto& kv : expect)
        if (usage(c, kv.first, C).size() != usage(a, kv.first, A).size() + kv.second) return "node count mismatch";
    // stats maps: dict equality after the expected merge, per map
    std::map<std::string, std::string> stock;
    std::vector<std::string> all;
    for (const auto& s : active) {
        stock[s.modded] = s.stock;
        all.push_back(s.modded);
        all.push_back(s.stock);
    }
    std::vector<FMap> ma = maps_for(a, all, A.tab_end), mc = maps_for(c, all, C.tab_end);
    if (ma.size() != mc.size()) return "stats map count changed";
    for (size_t i = 0; i < ma.size(); ++i) {
        std::map<std::string, uint32_t> d;
        for (const auto& kv : mapdict(a, ma[i].p)) {
            auto it = stock.find(kv.first);
            d[it == stock.end() ? kv.first : it->second] += kv.second;
        }
        auto nw = mapdict(c, mc[i].p);
        std::map<std::string, uint32_t> got(nw.begin(), nw.end());
        if (got != d || nw.size() != d.size() || U(c, mc[i].p) != d.size()) return "stats map mismatch";
    }
    return "";
}

}  // namespace ss

// The whole algorithm on one save's bytes. Skip = no modded path in it. Refused = left alone, why says what to do.
inline ScrubOut scrub_bytes(const std::string& b, const std::vector<SaveScrub>& pairs_in) {
    using namespace ss;
    ScrubOut R;
    std::vector<SaveScrub> pairs;
    for (const auto& p : pairs_in)
        if (!p.modded.empty() && p.modded != p.stock && b.find(p.modded) != std::string::npos) pairs.push_back(p);
    if (pairs.empty()) return R;  // Skip
    auto refuse = [&](const std::string& w) {
        R.st = ScrubSt::Refused;
        R.why = w;
        R.data.clear();
        return R;
    };
    Parsed P;
    std::string why;
    if (!parse(b, P, why)) return refuse(why);
    const auto& T = P.table;
    auto idx_of = [&](const std::string& s) -> int {
        for (size_t i = 0; i < T.size(); ++i)
            if (T[i] == s) return static_cast<int>(i);
        return -1;
    };

    std::map<uint32_t, uint32_t> mp;                 // modded idx -> stock idx
    std::map<uint32_t, std::string> standin;         // modded idx -> vanilla stand-in
    std::set<std::string> used;
    for (const auto& p : pairs) {
        int j = idx_of(p.stock);
        if (j < 0) return refuse("needs manual: stock class " + p.stock + " is not in the save's class table");
        int i = idx_of(p.modded);
        if (i < 0) continue;
        mp[static_cast<uint32_t>(i)] = static_cast<uint32_t>(j);
        std::string n = p.standin;
        if (n.empty())
            for (const auto& c : standins())
                if (idx_of(c) < 0 && !used.count(c)) {
                    n = c;
                    break;
                }
        if (n.empty() || idx_of(n) >= 0 || used.count(n)) return refuse("no unused vanilla stand-in class for " + p.modded);
        used.insert(n);
        standin[static_cast<uint32_t>(i)] = n;
    }

    std::vector<Edit> edits;
    ScrubRep rep;
    std::vector<size_t> allnodes;
    for (const auto& kv : mp) {
        std::vector<size_t> ns = usage(b, kv.first, P);
        std::set<uint32_t> ids;
        for (size_t o : ns) ids.insert(U(b, o + 4));
        if (ids.size() != ns.size()) return refuse("duplicate node ids for class index " + std::to_string(kv.first));
        for (size_t o : ns) {
            edits.push_back({o, o + 4, pk(kv.second)});
            allnodes.push_back(o);
            ++rep.nodes;
        }
    }
    // [classIdx][id][0] entries that are not nodes (lists inside embedded design copies, unique ids): same class word,
    // same stock native parent, so remap like a node. Anything still plausible afterwards is refused below.
    for (const auto& kv : mp)
        for (size_t o : residual(b, kv.first, P))
            if (std::find(allnodes.begin(), allnodes.end(), o) == allnodes.end()) {
                edits.push_back({o, o + 4, pk(kv.second)});
                ++rep.lists;
            }
    std::sort(allnodes.begin(), allnodes.end());
    for (size_t i = 1; i < allnodes.size(); ++i)
        if (allnodes[i] - allnodes[i - 1] < 16) return refuse("ambiguous overlapping node matches");

    size_t o = 12;
    for (size_t i = 0; i < T.size(); ++i) {
        auto it = standin.find(static_cast<uint32_t>(i));
        if (it != standin.end()) {
            edits.push_back({o, o + 4 + T[i].size() + 1, fstr(it->second)});
            ++rep.table;
        }
        o += 4 + T[i].size() + 1;
    }

    std::set<size_t> handled;
    for (const auto& p : pairs) {
        for (const FMap& f : find_flatmaps(b, p.modded, P.tab_end)) {
            handled.insert(f.occ);
            std::map<std::string, Ent> vals;
            for (const auto& e : run(b, f.p + 4)) vals[name_of(b, e)] = e;
            auto sv = vals.find(p.stock);
            auto mv = vals.find(p.modded);
            if (mv == vals.end()) return refuse("stats map parse mismatch");
            if (sv != vals.end()) {
                Ent se = sv->second, me = mv->second;
                uint32_t add = U(b, me.off + me.len - 4);
                edits.push_back({se.off + se.len - 4, se.off + se.len, pk(U(b, se.off + se.len - 4) + add)});
                edits.push_back({me.off, me.off + me.len, ""});
                Edit n{f.p, f.p + 4, "", true, f.p};
                edits.push_back(n);
                ++rep.merged;
                ++rep.removed;
            } else {
                edits.push_back({f.occ, f.occ + 4 + p.modded.size() + 1, fstr(p.stock)});
                ++rep.renamed;
            }
        }
    }
    std::map<size_t, uint32_t> nrem;
    std::vector<Edit> keep;
    for (auto& e : edits) {
        if (e.nmark) ++nrem[e.p];
        else keep.push_back(e);
    }
    edits = keep;
    for (const auto& kv : nrem) edits.push_back({kv.first, kv.first + 4, pk(U(b, kv.first) - kv.second)});

    for (const auto& p : pairs) {
        std::string k = p.modded + std::string(1, '\0');
        for (size_t occ = b.find(k); occ != std::string::npos; occ = b.find(k, occ + k.size())) {
            if (occ < P.tab_end) continue;
            bool h = false;
            for (size_t x : handled) h = h || x + 4 == occ;
            if (h) continue;
            if (occ < 4 || U(b, occ - 4) != k.size()) return refuse("modded path inside a longer string at " + std::to_string(occ));
            bool dup = false;
            for (const auto& e : edits) dup = dup || e.a == occ - 4;
            if (dup) continue;
            if (p.stock.size() != p.modded.size())
                return refuse("modded path in a per-ship/station table and stock path has another length: " + p.modded);
            edits.push_back({occ - 4, occ + k.size(), fstr(p.stock)});
            ++rep.paths;
        }
    }
    std::stable_sort(edits.begin(), edits.end(), [](const Edit& x, const Edit& y) { return x.a < y.a; });
    for (size_t i = 1; i < edits.size(); ++i)
        if (edits[i - 1].e > edits[i].a) return refuse("overlapping edits");

    std::string out = apply_edits(b, edits);
    {
        Parsed PO;
        std::string w2;
        if (!parse(out, PO, w2)) return refuse("re-parse failed: " + w2);
        for (const auto& kv : mp) {
            std::vector<size_t> rs = residual(out, kv.first, PO);
            if (!rs.empty()) {
                std::string at;
                for (size_t i = 0; i < rs.size() && i < 5; ++i) at += (i ? "," : "") + std::to_string(rs[i]);
                return refuse("cannot revert safely: " + std::to_string(rs.size()) + " unexplained reference(s) to class index " +
                              std::to_string(kv.first) + " (" + T[kv.first] + ") at offset(s) " + at + ": save left untouched");
            }
        }
    }
    std::string v = verify(b, out, mp, pairs);
    if (!v.empty()) return refuse("verification failed: " + v);
    R.st = ScrubSt::Ok;
    R.data = out;
    R.rep = rep;
    return R;
}

// ---------- designs (GVAS DesignSaveGame) ----------

// A design holds class paths as plain FStrings. Same-length substitution keeps every length field of the file valid,
// so that is the only edit made: each occurrence of a modded path must be a whole FString ([u32 len+1][path][NUL]) and the
// stock path must have the same length. Anything else = Refused, the design is left alone. The design must not be moved
// away either: ships reference their design and the game crashes (0x110) when it is missing.
inline ScrubOut scrub_design_bytes(const std::string& b, const std::vector<SaveScrub>& pairs_in) {
    using namespace ss;
    ScrubOut R;
    std::vector<SaveScrub> pairs;
    for (const auto& p : pairs_in)
        if (!p.modded.empty() && p.modded != p.stock && b.find(p.modded) != std::string::npos) pairs.push_back(p);
    if (pairs.empty()) return R;  // Skip
    auto refuse = [&](const std::string& w) {
        R.st = ScrubSt::Refused;
        R.why = w;
        R.data.clear();
        return R;
    };
    if (b.compare(0, 4, "GVAS") != 0) return refuse("not a GVAS design file");
    std::string out = b;
    std::vector<std::pair<size_t, size_t>> ranges;  // replaced [begin, end)
    for (const auto& p : pairs) {
        if (p.stock.size() != p.modded.size()) return refuse("stock path has another length than " + p.modded + ": design left alone");
        for (size_t occ = b.find(p.modded); occ != std::string::npos; occ = b.find(p.modded, occ + p.modded.size())) {
            size_t e = occ + p.modded.size();
            if (occ < 4 || U(b, occ - 4) != p.modded.size() + 1 || e >= b.size() || b[e] != 0)
                return refuse("modded path is not a whole string at offset " + std::to_string(occ) + ": design left alone");
            out.replace(occ, p.modded.size(), p.stock);
            ranges.push_back({occ, e});
            ++R.rep.paths;
        }
    }
    // independent check: same length, nothing outside the replaced ranges changed, ranges hold the stock path, no modded left
    if (out.size() != b.size()) return refuse("length changed");
    std::sort(ranges.begin(), ranges.end());
    size_t pos = 0;
    for (const auto& r : ranges) {
        if (r.first < pos || out.compare(pos, r.first - pos, b, pos, r.first - pos) != 0) return refuse("bytes outside the replaced strings changed");
        pos = r.second;
    }
    if (out.compare(pos, std::string::npos, b, pos, std::string::npos) != 0) return refuse("tail changed");
    for (const auto& p : pairs)
        if (out.find(p.modded) != std::string::npos) return refuse("modded path left");
    R.st = ScrubSt::Ok;
    R.data = out;
    return R;
}

// ---------- files ----------

struct ScrubFile {
    std::string name;
    ScrubSt st = ScrubSt::Skip;
    std::string why;
    ScrubRep rep;
    bool written = false;
    bool restored = false;  // design taken back from DesignsBackup
};

struct ScrubSummary {
    std::vector<ScrubFile> files;    // saves
    std::vector<ScrubFile> designs;  // designs (scrubbed in place, never moved away)
    fs::path backup_dir;             // empty = nothing backed up
    bool dry = false;
    static int count(const std::vector<ScrubFile>& v, ScrubSt s) {
        int n = 0;
        for (const auto& f : v) n += f.st == s;
        return n;
    }
    int scrubbed() const { return count(files, ScrubSt::Ok); }
    int refused() const { return count(files, ScrubSt::Refused); }
    int designs_scrubbed() const { return count(designs, ScrubSt::Ok); }
    int designs_refused() const { return count(designs, ScrubSt::Refused); }
    std::string text() const {
        std::string t = std::string(dry ? "Save scrub (dry run, nothing written): " : "Save scrub: ") + std::to_string(scrubbed()) +
                        (dry ? " save(s) would change, " : " save(s) scrubbed, ") + std::to_string(refused()) + " refused; " +
                        std::to_string(designs_scrubbed()) + (dry ? " design(s) would change, " : " design(s) scrubbed in place, ") +
                        std::to_string(designs_refused()) + " refused.\n";
        for (const auto& f : files) {
            if (f.st == ScrubSt::Ok)
                t += "  " + std::string(dry ? "would scrub " : "scrubbed ") + f.name + " (nodes " + std::to_string(f.rep.nodes) + ", stats merged " +
                     std::to_string(f.rep.merged) + ", renamed " + std::to_string(f.rep.renamed) + ", paths " + std::to_string(f.rep.paths) + ")\n";
            else if (f.st == ScrubSt::Refused)
                t += "  REFUSED " + f.name + ": " + f.why + "\n";
        }
        for (const auto& f : designs) {
            if (f.st == ScrubSt::Ok)
                t += "  design " + std::string(dry ? "would be " : "") + (f.restored ? "restored from DesignsBackup and scrubbed: " : "scrubbed: ") + f.name +
                     " (" + std::to_string(f.rep.paths) + " path(s))\n";
            else if (f.st == ScrubSt::Refused)
                t += "  design REFUSED " + f.name + ": " + f.why + "\n";
            else if (!f.why.empty())
                t += "  design skipped " + f.name + ": " + f.why + "\n";
        }
        if (!backup_dir.empty()) t += "  backup folder: " + to_utf8(backup_dir.wstring()) + "\n";
        return t;
    }
};

inline fs::path default_save_dir() {
    wchar_t* v = nullptr;
    size_t n = 0;
    fs::path r;
    if (_wdupenv_s(&v, &n, L"LOCALAPPDATA") == 0 && v) {
        r = fs::path(v) / L"MainSequence" / L"Saved" / L"SaveGames";
        free(v);
    }
    return r;
}

inline std::string read_bin(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// temp file next to f, read back and compared, then renamed over f
inline void write_verified(const fs::path& f, const std::string& data) {
    std::error_code ec;
    fs::path tmp = f;
    tmp += L".scrubtmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();
        if (out.fail()) throw std::runtime_error("write failed");
    }
    if (read_bin(tmp) != data) {
        fs::remove(tmp, ec);
        throw std::runtime_error("written file differs from the verified result");
    }
    fs::rename(tmp, f);  // replaces the original
}

enum : int { kScrubSaves = 1, kScrubDesigns = 2, kRestoreDesigns = 4 };

// what: kScrubSaves = every *_S.sav in savedir that holds a modded path (backup first);
//       kScrubDesigns = Designs\*.sav rewritten in place (same-length substitution, backup first);
//       kRestoreDesigns = designs a former Remove moved to DesignsBackup\ (no same-named file in Designs\, not a byte copy of
//       one) go back into Designs\ scrubbed. dry = report only. Never throws; a failing file is Refused and left untouched.
inline ScrubSummary scrub_saves(const fs::path& savedir, const fs::path& backup_root, const std::vector<SaveScrub>& pairs, bool dry,
                                int what = kScrubSaves | kScrubDesigns, const std::function<void(const std::string&)>& progress = {}) {
    ScrubSummary S;
    S.dry = dry;
    std::error_code ec;
    if (pairs.empty() || !fs::is_directory(savedir, ec)) return S;
    auto ensure_backup = [&]() {
        if (!S.backup_dir.empty()) return true;
        char st[32];
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        std::strftime(st, sizeof st, "%Y%m%d-%H%M%S", &tm);
        fs::path d = backup_root / st;
        for (int i = 1; fs::exists(d, ec); ++i) d = backup_root / (std::string(st) + "-" + std::to_string(i));
        fs::create_directories(d, ec);
        if (ec) return false;
        write_text(d / L"RESTORE.txt",
                   "Originals of the saves and designs MSCore Manager scrubbed when a mod was removed.\n"
                   "To undo: close the game, copy the files here back into\n  " + to_utf8(savedir.wstring()) +
                       "\n(saves: <slot>_S.sav and <slot>_M.sav; designs: the Designs subfolder go into SaveGames\\Designs).\n"
                       "They need the removed mod's pak to load.\n");
        S.backup_dir = d;
        return true;
    };
    auto has_modded = [&](const std::string& b) {
        for (const auto& p : pairs)
            if (!p.modded.empty() && b.find(p.modded) != std::string::npos) return true;
        return false;
    };
    if (what & kScrubSaves) {
        std::vector<fs::path> saves;
        for (auto& e : fs::directory_iterator(savedir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            std::wstring n = e.path().filename().wstring();
            if (n.size() > 6 && n.compare(n.size() - 6, 6, L"_S.sav") == 0) saves.push_back(e.path());
        }
        std::sort(saves.begin(), saves.end());
        size_t done = 0;
        for (const auto& f : saves) {
            ScrubFile r;
            r.name = to_utf8(f.filename().wstring());
            if (progress) progress("Scrubbing saves " + std::to_string(++done) + "/" + std::to_string(saves.size()) + ": " + r.name);
            try {
                std::string b = read_bin(f);
                ScrubOut o = scrub_bytes(b, pairs);
                r.st = o.st;
                r.why = o.why;
                r.rep = o.rep;
                if (o.st == ScrubSt::Ok && !dry) {
                    if (!ensure_backup()) throw std::runtime_error("cannot create backup folder");
                    fs::copy_file(f, S.backup_dir / f.filename(), fs::copy_options::overwrite_existing);
                    fs::path m = f;
                    std::wstring mn = f.filename().wstring();
                    m.replace_filename(mn.substr(0, mn.size() - 6) + L"_M.sav");
                    if (fs::is_regular_file(m, ec)) fs::copy_file(m, S.backup_dir / m.filename(), fs::copy_options::overwrite_existing);
                    write_verified(f, o.data);
                    r.written = true;
                }
            } catch (const std::exception& e) {
                r.st = ScrubSt::Refused;
                r.why = std::string("error, save left untouched: ") + e.what();
            }
            if (r.st != ScrubSt::Skip) S.files.push_back(r);
        }
    }
    fs::path dd = savedir / L"Designs", bd = savedir / L"DesignsBackup";
    auto list_sav = [&](const fs::path& dir) {
        std::vector<fs::path> v;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && e.path().extension() == L".sav") v.push_back(e.path());
        std::sort(v.begin(), v.end());
        return v;
    };
    // one design: scrub bytes, backup the original, write dest (in place or restored), optionally drop the source
    auto do_design = [&](const fs::path& src, const fs::path& dest, bool restored) {
        ScrubFile r;
        r.name = to_utf8(src.filename().wstring());
        if (progress) progress("Checking design: " + r.name);
        r.restored = restored;
        try {
            std::string b = read_bin(src);
            if (!has_modded(b)) return;
            ScrubOut o = scrub_design_bytes(b, pairs);
            r.st = o.st;
            r.why = o.why;
            r.rep = o.rep;
            if (o.st == ScrubSt::Ok && !dry) {
                if (!ensure_backup()) throw std::runtime_error("cannot create backup folder");
                fs::create_directories(S.backup_dir / (restored ? L"DesignsBackup" : L"Designs"), ec);
                fs::copy_file(src, S.backup_dir / (restored ? L"DesignsBackup" : L"Designs") / src.filename(), fs::copy_options::overwrite_existing);
                write_verified(dest, o.data);
                if (restored) fs::remove(src, ec);
                r.written = true;
            }
        } catch (const std::exception& e) {
            r.st = ScrubSt::Refused;
            r.why = std::string("error, design left untouched: ") + e.what();
        }
        if (r.st != ScrubSt::Skip) S.designs.push_back(r);
    };
    if (what & kRestoreDesigns) {  // before the in-place pass: "byte copy of a design in Designs" is compared with the originals
        std::vector<fs::path> have = list_sav(dd);
        for (const auto& f : list_sav(bd)) {
            std::string b = read_bin(f);
            if (!has_modded(b)) continue;  // not one of ours (the user's own backups stay)
            fs::path dest = dd / f.filename();
            ScrubFile skip;
            skip.name = to_utf8(f.filename().wstring());
            if (fs::exists(dest, ec)) {
                skip.why = "a design with this name is already in Designs";
                S.designs.push_back(skip);
                continue;
            }
            ScrubOut sc = scrub_design_bytes(b, pairs);  // a design already in Designs, scrubbed or not, is the same design
            bool same = false;
            for (const auto& h : have) {
                if (fs::file_size(h, ec) != b.size()) continue;
                std::string hb = read_bin(h);
                same = same || hb == b || (sc.st == ScrubSt::Ok && hb == sc.data);
            }
            if (same) {
                skip.why = "same content as a design in Designs, left in DesignsBackup";
                S.designs.push_back(skip);
                continue;
            }
            do_design(f, dest, true);
        }
    }
    if (what & kScrubDesigns)
        for (const auto& f : list_sav(dd)) do_design(f, f, false);
    return S;
}

// Puts the originals from one save-backup\<time>\ folder back (saves, metas, Designs\, DesignsBackup\), after a fresh backup of
// the files they replace (new save-backup\<time>\ folder). Used to re-run a scrub with a better policy. Never throws.
struct RestoreResult {
    int restored = 0, failed = 0;
    fs::path fresh_backup;
    std::string text;
};

inline RestoreResult restore_saves(const fs::path& backup_dir, const fs::path& savedir, const fs::path& backup_root, bool dry) {
    RestoreResult R;
    std::error_code ec;
    if (!fs::is_directory(backup_dir, ec) || !fs::is_directory(savedir, ec)) {
        R.failed = 1;
        R.text = "restore: backup folder or save folder not found\n";
        return R;
    }
    std::vector<fs::path> files;  // paths relative to backup_dir
    for (auto it = fs::recursive_directory_iterator(backup_dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && it->path().filename() != L"RESTORE.txt") files.push_back(fs::relative(it->path(), backup_dir));
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        R.failed = 1;
        R.text = "restore: nothing to restore in " + to_utf8(backup_dir.wstring()) + "\n";
        return R;
    }
    if (!dry) {
        char st[32];
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        std::strftime(st, sizeof st, "%Y%m%d-%H%M%S", &tm);
        fs::path d = backup_root / (std::string(st) + "-before-restore");
        for (int i = 1; fs::exists(d, ec); ++i) d = backup_root / (std::string(st) + "-before-restore-" + std::to_string(i));
        fs::create_directories(d, ec);
        R.fresh_backup = d;
        write_text(d / L"RESTORE.txt", "Files that were in the save folder just before a restore from\n  " + to_utf8(backup_dir.wstring()) +
                                           "\nCopy them back into\n  " + to_utf8(savedir.wstring()) + "\nto undo the restore.\n");
    }
    for (const auto& rel : files) {
        fs::path src = backup_dir / rel, dst = savedir / rel;
        std::string name = to_utf8(rel.wstring());
        try {
            if (!dry) {
                if (fs::exists(dst, ec)) {
                    fs::create_directories((R.fresh_backup / rel).parent_path(), ec);
                    fs::copy_file(dst, R.fresh_backup / rel, fs::copy_options::overwrite_existing);
                }
                fs::create_directories(dst.parent_path(), ec);
                write_verified(dst, read_bin(src));
            }
            ++R.restored;
            R.text += std::string(dry ? "  would restore " : "  restored ") + name + "\n";
        } catch (const std::exception& e) {
            ++R.failed;
            R.text += "  FAILED " + name + ": " + e.what() + "\n";
        }
    }
    R.text = std::string(dry ? "Restore (dry run): " : "Restore: ") + std::to_string(R.restored) + " file(s), " + std::to_string(R.failed) +
             " failed.\n" + R.text;
    if (!R.fresh_backup.empty()) R.text += "  fresh backup of the replaced files: " + to_utf8(R.fresh_backup.wstring()) + "\n";
    return R;
}

// Built-in declarations for mods released before they carried save_scrub lines in mscore.mod.
inline std::vector<SaveScrub> builtin_save_scrub(const std::string& mod) {
    const std::string B = "/Game/Blueprints/BuildingBlocks/", M = "/Game/Blueprints/Machines/";
    auto P = [](const std::string& d, const std::string& n) { return d + n + "." + n + "_C"; };
    std::string l = lower(mod);
    if (l == "miningbenchmod" || l == "miningunitui")
        return {{P(B, "BPO_MinerModuleBlock"), P(B, "BPO_WorkstationBlock"), ""}, {P(M, "BPO_MiningMod"), P(M, "BPO_Workbench"), ""}};
    if (l == "blueprintunit")
        return {{P(B, "BPO_BlueprintUnit"), P(B, "BPO_CommandModule"), ""}, {P(M, "BP_BPUnitStoreObj_01"), P(M, "BP_ContainerObject_2"), ""}};
    return {};
}

// What Remove must scrub: save_scrub lines of the mod's mscore.mod files (installed or disabled copy), else the built-in table.
// Call BEFORE remove_mod (it deletes the mscore.mod files).
inline std::vector<SaveScrub> scrub_declarations(const Paths& p, const std::string& mod) {
    std::vector<SaveScrub> out;
    std::vector<RemoveItem> items;
    std::string err;
    if (remove_plan(p, mod, items, err)) {
        for (const auto& it : items) {
            if (lower(to_utf8(it.target.filename().wstring())) != "mscore.mod") continue;
            std::string t = read_text(it.target);
            if (t.empty()) t = read_text(disabled_dir(p, mod) / to_wide(it.rel));
            for (const auto& s : parse_save_scrub(t)) out.push_back(s);
        }
    }
    if (out.empty()) out = builtin_save_scrub(mod);
    return out;
}

}  // namespace mscl
