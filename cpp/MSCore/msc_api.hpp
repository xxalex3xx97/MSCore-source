// FILE: cpp/MSCore/msc_api.hpp
// WHAT: plain structs that cross the C export boundary to MiningBenchMod (mb_mscore.hpp). No UE types in here.
// WHY: the mining DLL reads and writes these by layout, so both sides static_assert the sizes (x64: 16 and 584 bytes).
#pragma once
#include <cstdint>

// one logistic filter row: item (UObject*), amount, action (0 LIMIT, 1 BUFFER)
struct MsFilterRow
{
    void* item;
    int32_t amount;
    int32_t action;
};

// one sector: save id, solar system id, ore count, chunk, half extents, warp arrival distance (raw), ore items (UObject*, max 40), display name
struct MsSector
{
    int32_t id;
    int32_t sys;
    int32_t nores;
    int32_t pad;
    long long ch[3];
    long long he[3];
    long long warp_dist;
    void* ores[40];
    wchar_t name[96];
};

// one scanned object (ship, asteroid, station), see msc_scan.hpp. kind 0 ship, 1 asteroid, 2 station. hull, max_hull, order_idx = -1 when unknown.
// order: "" when none. ch = chunk, loc = chunk-relative location (raw), world = actor location (UE units) when world_ok.
struct MsScanRow
{
    void* obj;              // off 0: save object (ship, station, asteroid save object), or the rock actor for loaded rows
    void* actor;            // off 8: rock actor or the object's Actor property, or null
    int32_t id;             // off 16: save object id (ObjectId for loaded rock rows)
    int32_t kind;           // off 20
    int32_t sys;            // off 24: solar system id
    int32_t loaded;         // off 28: rock actor exists in the world
    int32_t docked;         // off 32
    int32_t piloted;        // off 36: the player pilots this ship
    int32_t dock_id;        // off 40
    int32_t hull;           // off 44
    int32_t max_hull;       // off 48
    int32_t order_idx;      // off 52
    int32_t order_dock;     // off 56
    int32_t world_ok;       // off 60
    long long ch[3];        // off 64
    long long loc[3];       // off 88
    double world[3];        // off 112
    int32_t nores;          // off 136 (+4 pad)
    void* ores[8];          // off 144: ore item objects
    wchar_t name[64];       // off 208
    wchar_t type[64];       // off 336
    wchar_t oname[64];      // off 464
    wchar_t order[48];      // off 592
};

static_assert(sizeof(MsFilterRow) == 16, "MsFilterRow layout must match mb_mscore.hpp");
static_assert(sizeof(MsSector) == 584, "MsSector layout must match mb_mscore.hpp");
static_assert(sizeof(MsScanRow) == 688, "MsScanRow layout must match the MSCore native API 3 doc");
