// FILE: cpp/MSCore/msc_chunks.hpp
// WHAT: chunk hold. A 3x3x3 cube of chunks around a centre chunk stays loaded: SetLevel(S,&key,1) + Apply(S,&key) per chunk (native).
//       chunk_hold(owner, sys, c, err)  sets or moves the owner's cube. Returns 0 fail (err says why), 1 set, 2 unchanged.
//       chunk_hold_release_owner(owner) drops the owner's cube.
//       chunk_hold_release_all()        drops every cube (game exit / mod unload).
//       chunk_ready(why)                natives found by pattern (nat_resolve); why = reason when not ready.
// WHY: LoadChunks() streams only the player's surroundings, so far-away chunks stay unloaded until a caller holds their cube.
// RULES: the player's own loaded set (player chunk +- CH_PLAYER_ZONE) is never set or cleared (deferred instead);
//        a chunk held by several owners is cleared when the last one drops it; at most CH_MAX_CHUNKS held chunks;
//        only chunks that have a Chunk object are set (the rest is counted as no_object in the log).
// SAFETY: game thread only. The natives are found by byte pattern (msc_natives.hpp, nat_resolve: exactly one match each, else
//         the feature is off) and ch_native_check (get_system(PlayerObject) == FindAllOf SolarSystem) runs once per session before
//         the first set. Every native call is inside a ch_seh_* helper (SEH). A native failure -> ch_disable: level 0 on every held
//         chunk outside the player zone, feature off for the session. The first set is verified by a canary (chunk object still
//         there after a re-read of the map). The controller is never written. bForceLoadChunks is never written.
// DEPENDS ON: msc_cmd.hpp (alive, first_of), msc_ue.hpp (cached_ok, obj_id, find_by_id, get_obj_prop, msc_log), msc_warp.hpp (sec_axes, sys_id),
//             msc_natives.hpp (g_nat, nat_resolve).
// USED BY: dllmain.cpp (MSCore_chunk_hold, MSCore_chunk_release, MSCore_chunk_ready, MSCoreNative_chunk_*, check "chunks", ~MSCoreMod).
// Log keyword: 'CHUNKHOLD' (every line starts with CHUNKHOLD).
#pragma once
#include <cstdlib>
#include <set>
#include "msc_natives.hpp"

namespace
{
    constexpr int64_t CH_PLAYER_ZONE = 2;               // the player's own loaded set: PlayerCachedChunkLocation +- this (ChunkLoadingDistance)
    constexpr size_t CH_MAX_CHUNKS = 150;               // cap on distinct held chunks (all owners together)
    constexpr ULONGLONG CH_LOG_EVERY_MS = 10000;        // refusal lines that repeat every call are logged at most this often (ms)
    constexpr size_t CH_KEY_SIZE = 12;

    // one held chunk: solar system id + chunk key (the ChunkMap key, 3 x int32)
    struct ChKey
    {
        int sys = 0;
        int32_t k[3] = {0, 0, 0};
    };
    bool operator<(const ChKey& a, const ChKey& b)
    {
        if (a.sys != b.sys) return a.sys < b.sys;
        for (int i = 0; i < 3; ++i) if (a.k[i] != b.k[i]) return a.k[i] < b.k[i];
        return false;
    }
    bool operator==(const ChKey& a, const ChKey& b) { return !(a < b) && !(b < a); }

    // one owner's cube: the chunks it references and its centre
    struct ChRec
    {
        int sys = 0;
        int32_t c[3] = {0, 0, 0};
        std::vector<ChKey> keys;
    };

    struct ChState
    {
        bool disabled = false;        // native failure or check failed: no holds for the rest of the session
        bool checked = false;         // exe + native check passed (once, before the first set)
        std::map<ChKey, int> refs;    // held chunk -> number of owners holding it (level 1 applied by us)
        std::set<ChKey> deferred;     // chunks to clear later (player zone, or the System object not found)
        std::map<int, ChRec> owners;  // owner id -> its cube
        ULONGLONG log_ms = 0;         // last throttled log line
        bool canary_done = false;     // the first successful set was re-read (ok or disabled)
    };
    ChState g_ch;

    // ChunkMap pair: key and Chunk object (null when dead)
    struct ChEntry { int32_t k[3] = {0, 0, 0}; UObject* o = nullptr; };

    // ---- SEH helpers: POD arguments only (MSVC forbids __try in functions with C++ objects) ----

    // WHAT: copies n bytes from src to dst. false when the read faults.
    bool ch_seh_copy(const void* src, void* dst, size_t n)
    {
        __try { memcpy(dst, src, n); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: native get_system(P) -> out. false when the call faults.
    bool ch_seh_getsys(const void* fn, void* P, void*& out)
    {
        __try { out = reinterpret_cast<void* (*)(void*)>(const_cast<void*>(fn))(P); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: native set_level(S, &key, level). false when the call faults.
    bool ch_seh_setlevel(const void* fn, void* S, const int32_t* key, int32_t level)
    {
        __try { reinterpret_cast<void (*)(void*, const int32_t*, int32_t)>(const_cast<void*>(fn))(S, key, level); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: native apply(S, &key). false when the call faults.
    bool ch_seh_apply(const void* fn, void* S, const int32_t* key)
    {
        __try { reinterpret_cast<void (*)(void*, const int32_t*)>(const_cast<void*>(fn))(S, key); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ---- helpers ----

    // WHAT: "0x<hex>".
    std::wstring ch_hex(uint64_t v) { wchar_t b[32]; swprintf(b, 32, L"0x%llx", static_cast<unsigned long long>(v)); return b; }

    // WHAT: "(x,y,z)" of a chunk key.
    std::wstring ch_key_text(const int32_t k[3])
    {
        return STR("(") + std::to_wstring(k[0]) + STR(",") + std::to_wstring(k[1]) + STR(",") + std::to_wstring(k[2]) + STR(")");
    }

    // WHAT: get_system(PlayerObject) must be the FindAllOf SolarSystem object of the player's system (the object the set/apply calls get).
    bool ch_native_check(UObject* ctrl, int psys)
    {
        UObject* pobj = get_obj_prop(ctrl, STR("PlayerObject"));
        UObject* ref = psys > 0 ? find_by_id(STR("SolarSystem"), psys) : nullptr;
        if (!alive(pobj) || !alive(ref)) { msc_log(STR("CHUNKHOLD native check: player object or system object missing")); return false; }
        void* got = nullptr;
        const bool called = ch_seh_getsys(g_nat.system, pobj, got);
        const bool same = called && got == static_cast<void*>(ref);
        msc_log(STR("CHUNKHOLD native check get_system(PlayerObject)=") + ch_hex(reinterpret_cast<uint64_t>(got)) +
                STR(" FindAllOf=") + ch_hex(reinterpret_cast<uint64_t>(ref)) + STR(" ") + (same ? STR("same") : STR("DIFFERENT")));
        return same;
    }

    // WHAT: all pairs of S.ChunkMap (key = 12 bytes at pair+0, value = Chunk* at pair+ValueOffset). info gets the layout or the error.
    // WHY: FMapProperty + FScriptMap from UE4SS (no game SDK). Pointers are read inside SEH; dead objects are kept with o = null.
    bool ch_map_read(UObject* S, std::vector<ChEntry>& out, std::wstring& info)
    {
        out.clear();
        FProperty* p = alive(S) ? S->GetPropertyByNameInChain(STR("ChunkMap")) : nullptr;
        auto* mp = p ? CastField<FMapProperty>(p) : nullptr;
        if (!mp) { info = STR("ChunkMap is not a map property"); return false; }
        FProperty* kp = mp->GetKeyProp();
        FProperty* vp = mp->GetValueProp();
        if (!kp || !vp || kp->GetElementSize() != CH_KEY_SIZE) { info = STR("ChunkMap key size is not 12"); return false; }
        auto* sm = reinterpret_cast<FScriptMap*>(reinterpret_cast<uint8_t*>(S) + p->GetOffset_Internal());
        FScriptMapLayout& lay = mp->GetMapLayout();
        for (int32_t i = 0; i < sm->GetMaxIndex(); ++i)
        {
            if (!sm->IsValidIndex(i)) continue;
            const uint8_t* pair = static_cast<const uint8_t*>(sm->GetData(i, lay));
            ChEntry e;
            if (!pair || !ch_seh_copy(pair, e.k, CH_KEY_SIZE)) continue;
            UObject* v = nullptr;
            if (!ch_seh_copy(pair + lay.ValueOffset, &v, sizeof(v))) continue;
            e.o = alive(v) ? v : nullptr;
            out.push_back(e);
        }
        return true;
    }

    // WHAT: the Chunk object of key k in v, or null (also null when the object is dead).
    UObject* ch_find(const std::vector<ChEntry>& v, const int32_t k[3])
    {
        for (const ChEntry& e : v) if (e.k[0] == k[0] && e.k[1] == k[1] && e.k[2] == k[2]) return e.o;
        return nullptr;
    }

    // WHAT: SetLevel(S,&k,level) then Apply(S,&k), both inside SEH. false when S is gone or either call faults.
    bool ch_native(UObject* S, const int32_t k[3], int32_t level)
    {
        if (!alive(S) || !g_nat.setlevel || !g_nat.apply) return false;
        if (!ch_seh_setlevel(g_nat.setlevel, S, k, level)) return false;
        return ch_seh_apply(g_nat.apply, S, k);
    }

    // WHAT: the player controller. Cached: first_of is a full UObject walk.
    // WHY: the cache is checked with cached_ok (SEH), so a freed controller is looked up again.
    UObject* ch_ctrl()
    {
        static UObject* c = nullptr;
        static int cid = 0;
        if (c && cached_ok(c, cid)) return c;
        c = first_of(STR("DMSPlayerController"));
        cid = alive(c) ? obj_id(c) : 0;
        return c;
    }

    // WHAT: player chunk (PlayerCachedChunkLocation) and the player's solar system. false when unreadable: then nothing is set or cleared.
    bool ch_player(int& psys, int64_t pch[3])
    {
        UObject* ctrl = ch_ctrl();
        if (!alive(ctrl) || !sec_axes(ctrl, STR("PlayerCachedChunkLocation"), pch)) return false;
        UObject* pobj = get_obj_prop(ctrl, STR("PlayerObject"));
        psys = alive(pobj) ? sys_id(pobj) : 0;
        return psys > 0;
    }

    // WHAT: true when key lies in the player's own loaded set: same system, every axis within CH_PLAYER_ZONE of the player chunk.
    bool ch_in_zone(const ChKey& key, int psys, const int64_t pch[3])
    {
        if (key.sys != psys) return false;
        for (int i = 0; i < 3; ++i) if (std::llabs(static_cast<int64_t>(key.k[i]) - pch[i]) > CH_PLAYER_ZONE) return false;
        return true;
    }

    // WHAT: logs a refusal line, at most one per CH_LOG_EVERY_MS (refusals repeat on every call).
    void ch_log_throttled(const std::wstring& s)
    {
        const ULONGLONG now = GetTickCount64();
        if (g_ch.log_ms != 0 && now - g_ch.log_ms < CH_LOG_EVERY_MS) return;
        g_ch.log_ms = now;
        msc_log(s);
    }

    // WHAT: native failure: level 0 on every held chunk outside the player zone (zone chunks are kept as deferred), then the feature is off.
    void ch_disable(const std::wstring& why)
    {
        msc_log(STR("CHUNKHOLD disabled for this session: ") + why + STR(" (held chunks released, feature off)"));
        g_ch.disabled = true;
        int psys = 0;
        int64_t pch[3] = {0, 0, 0};
        const bool have = ch_player(psys, pch);
        std::set<ChKey> all;
        for (const auto& p : g_ch.refs) all.insert(p.first);
        for (const ChKey& key : g_ch.deferred) all.insert(key);
        for (const ChKey& key : all)
        {
            if (!have || ch_in_zone(key, psys, pch)) continue;   // never touch the player's zone
            ch_native(find_by_id(STR("SolarSystem"), key.sys), key.k, 0);   // result ignored: already failing
        }
        g_ch.refs.clear();
        g_ch.deferred.clear();
        g_ch.owners.clear();
    }

    // WHAT: drops one owner's references. A chunk no owner holds is cleared (level 0 + apply), unless it is in the player zone or its System
    //       object is gone: then it is deferred (ch_drain retries). false = a native call failed (feature disabled).
    bool ch_drop(const ChRec& r, int& cleared, int& shared)
    {
        for (const ChKey& key : r.keys)
        {
            auto ref = g_ch.refs.find(key);
            if (ref == g_ch.refs.end()) continue;
            if (--ref->second > 0) { ++shared; continue; }
            g_ch.refs.erase(ref);
            int psys = 0;
            int64_t pch[3] = {0, 0, 0};
            const bool have = ch_player(psys, pch);
            UObject* S = find_by_id(STR("SolarSystem"), key.sys);
            if (!have || ch_in_zone(key, psys, pch) || !alive(S)) { g_ch.deferred.insert(key); continue; }
            if (!ch_native(S, key.k, 0)) { ch_disable(STR("level 0 failed at chunk ") + ch_key_text(key.k)); return false; }
            ++cleared;
        }
        return true;
    }

    // WHAT: retries the deferred clears: a chunk nobody holds, outside the player zone, gets level 0 + apply.
    void ch_drain()
    {
        if (g_ch.deferred.empty()) return;
        int psys = 0;
        int64_t pch[3] = {0, 0, 0};
        if (!ch_player(psys, pch)) return;
        for (auto it = g_ch.deferred.begin(); it != g_ch.deferred.end();)
        {
            if (g_ch.refs.count(*it)) { it = g_ch.deferred.erase(it); continue; }
            if (ch_in_zone(*it, psys, pch)) { ++it; continue; }
            UObject* S = find_by_id(STR("SolarSystem"), it->sys);
            if (!alive(S)) { ++it; continue; }
            if (!ch_native(S, it->k, 0)) { ch_disable(STR("level 0 failed on deferred chunk ") + ch_key_text(it->k)); return; }
            it = g_ch.deferred.erase(it);
        }
    }

    // WHAT: exe check and native check, once per session, before the first set. false = not ready (retried next call) or disabled; err = reason.
    bool ch_ready(std::wstring& err)
    {
        if (g_ch.disabled) { err = STR("chunk hold disabled for this session"); return false; }
        if (g_ch.checked) return true;
        UObject* ctrl = ch_ctrl();
        UObject* pobj = alive(ctrl) ? get_obj_prop(ctrl, STR("PlayerObject")) : nullptr;
        const int psys = alive(pobj) ? sys_id(pobj) : 0;
        if (psys <= 0) { err = STR("player system unknown"); return false; }
        g_ch.checked = true;
        if (!nat_resolve())
        {
            ch_disable(STR("native pattern: ") + g_nat.why);
            err = STR("native pattern: ") + g_nat.why;
            return false;
        }
        if (!ch_native_check(ctrl, psys)) { ch_disable(STR("native check failed")); err = STR("native check failed"); return false; }
        return true;
    }
}

// ---- API (called from dllmain.cpp) ----
namespace
{
    // WHAT: drops one owner's cube (its chunks no other owner holds are cleared). No-op without a cube.
    void chunk_hold_release_owner(int owner)
    {
        const auto it = g_ch.owners.find(owner);
        if (it == g_ch.owners.end()) return;
        const ChRec rec = it->second;
        g_ch.owners.erase(it);
        int cleared = 0, kept = 0;
        if (!ch_drop(rec, cleared, kept)) return;
        ch_drain();
        msc_log(STR("CHUNKHOLD released owner=") + std::to_wstring(owner) + STR(" sys=") + std::to_wstring(rec.sys) + STR(" centre=") + ch_key_text(rec.c) +
                STR(" cleared=") + std::to_wstring(cleared) + STR(" shared_kept=") + std::to_wstring(kept) + STR(" held=") + std::to_wstring(g_ch.refs.size()) +
                STR(" deferred=") + std::to_wstring(g_ch.deferred.size()));
    }

    // WHAT: releases every hold, then retries the deferred clears. Game exit, mod unload.
    void chunk_hold_release_all()
    {
        std::vector<int> ids;
        for (const auto& p : g_ch.owners) ids.push_back(p.first);
        for (int id : ids) chunk_hold_release_owner(id);
        ch_drain();
        msc_log(STR("CHUNKHOLD release_all owners=") + std::to_wstring(ids.size()) + STR(" held=") + std::to_wstring(g_ch.refs.size()) +
                STR(" deferred=") + std::to_wstring(g_ch.deferred.size()));
    }

    // WHAT: true when the natives are found (nat_resolve) and the feature is not disabled. why = the reason when false.
    // WHY: lets selfcheck and callers know chunk hold can work, without setting anything.
    bool chunk_ready(std::wstring& why)
    {
        if (g_ch.disabled) { why = STR("chunk hold disabled for this session"); return false; }
        if (!nat_resolve()) { why = g_nat.why; return false; }
        why.clear();
        return true;
    }

    // WHAT: sets (or moves) the owner's cube of 27 chunks centred on c in system sys. Returns 0 fail (err says why), 1 set, 2 unchanged.
    // WHY: the new cube is set before the old one is cleared, so chunks both cubes use never unload.
    int chunk_hold(int owner, int sys, const int64_t c3[3], std::wstring& err)
    {
        if (!ch_ready(err)) return 0;
        int psys = 0;
        int64_t pch[3] = {0, 0, 0};
        if (!ch_player(psys, pch)) { err = STR("player chunk unknown"); return 0; }
        ch_drain();
        const int32_t c[3] = {static_cast<int32_t>(c3[0]), static_cast<int32_t>(c3[1]), static_cast<int32_t>(c3[2])};
        const auto old = g_ch.owners.find(owner);
        if (old != g_ch.owners.end() && old->second.sys == sys && old->second.c[0] == c[0] && old->second.c[1] == c[1] && old->second.c[2] == c[2]) return 2;
        UObject* S = find_by_id(STR("SolarSystem"), sys);
        if (!alive(S))
        {
            err = STR("no SolarSystem object for sys=") + std::to_wstring(sys);
            ch_log_throttled(STR("CHUNKHOLD ") + err + STR(" owner=") + std::to_wstring(owner));
            return 0;
        }
        std::vector<ChEntry> v;
        std::wstring info;
        if (!ch_map_read(S, v, info)) { err = STR("ChunkMap unreadable: ") + info; ch_log_throttled(STR("CHUNKHOLD ") + err); return 0; }
        // the cube: 27 chunks, only those with a Chunk object; the player's own zone is never set
        std::vector<ChKey> want;
        int no_obj = 0, zone = 0;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    ChKey key;
                    key.sys = sys;
                    key.k[0] = c[0] + dx; key.k[1] = c[1] + dy; key.k[2] = c[2] + dz;
                    if (ch_in_zone(key, psys, pch)) { ++zone; continue; }
                    if (!ch_find(v, key.k)) { ++no_obj; continue; }
                    want.push_back(key);
                }
        // cap: distinct held chunks after this move (the old cube's chunks that only it holds are freed first)
        const ChRec* prev = old != g_ch.owners.end() ? &old->second : nullptr;
        size_t need = 0, freed = 0;
        for (const ChKey& key : want) if (!g_ch.refs.count(key)) ++need;
        if (prev)
            for (const ChKey& key : prev->keys)
            {
                const auto r = g_ch.refs.find(key);
                if (r != g_ch.refs.end() && r->second == 1 && std::find(want.begin(), want.end(), key) == want.end()) ++freed;
            }
        if (g_ch.refs.size() + need - freed > CH_MAX_CHUNKS)
        {
            err = STR("cap: ") + std::to_wstring(g_ch.refs.size()) + STR(" held, ") + std::to_wstring(need) + STR(" new chunks needed, max ") +
                  std::to_wstring(CH_MAX_CHUNKS);
            ch_log_throttled(STR("CHUNKHOLD ") + err + STR(" owner=") + std::to_wstring(owner) + STR(", no hold"));
            chunk_hold_release_owner(owner);
            return 0;
        }
        // set the new cube first (a chunk another owner already holds only gets one more reference)
        ChRec rec;
        rec.sys = sys;
        for (int i = 0; i < 3; ++i) rec.c[i] = c[i];
        int set_n = 0, shared = 0;
        for (const ChKey& key : want)
        {
            rec.keys.push_back(key);
            auto ref = g_ch.refs.find(key);
            if (ref != g_ch.refs.end()) { ++ref->second; ++shared; continue; }
            g_ch.refs[key] = 1;   // tracked before the call: ch_disable clears it when the call fails
            if (!ch_native(S, key.k, 1))
            {
                err = STR("set level 1 failed at chunk ") + ch_key_text(key.k);
                ch_disable(err);
                return 0;
            }
            ++set_n;
            if (!g_ch.canary_done)
            {
                // canary: the first set must show up in a fresh read of the map (the chunk object is still alive)
                g_ch.canary_done = true;
                std::vector<ChEntry> v2;
                std::wstring info2;
                if (!ch_map_read(S, v2, info2)) { err = STR("canary: ") + info2; ch_disable(err); return 0; }
                if (!ch_find(v2, key.k))
                {
                    err = STR("canary: chunk object not alive after set at ") + ch_key_text(key.k);
                    ch_disable(err);
                    return 0;
                }
            }
        }
        // then the old cube: the chunks the new one does not use are cleared
        int cleared = 0, kept = 0;
        std::wstring from;
        if (prev)
        {
            const ChRec old_rec = *prev;   // copy: g_ch.owners changes below
            from = STR(" from=") + ch_key_text(old_rec.c);
            g_ch.owners.erase(owner);
            if (!ch_drop(old_rec, cleared, kept)) { err = STR("level 0 failed while moving the cube"); return 0; }
        }
        g_ch.owners[owner] = rec;
        msc_log(STR("CHUNKHOLD set owner=") + std::to_wstring(owner) + STR(" sys=") + std::to_wstring(sys) + STR(" centre=") + ch_key_text(c) + from +
                STR(" cube=") + std::to_wstring(want.size()) + STR(" new=") + std::to_wstring(set_n) + STR(" shared=") + std::to_wstring(shared) +
                STR(" no_object=") + std::to_wstring(no_obj) + STR(" player_zone=") + std::to_wstring(zone) + STR(" cleared=") + std::to_wstring(cleared) +
                STR(" held=") + std::to_wstring(g_ch.refs.size()) + STR(" deferred=") + std::to_wstring(g_ch.deferred.size()));
        return 1;
    }
}
