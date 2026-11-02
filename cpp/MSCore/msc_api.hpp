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

static_assert(sizeof(MsFilterRow) == 16, "MsFilterRow layout must match mb_mscore.hpp");
static_assert(sizeof(MsSector) == 584, "MsSector layout must match mb_mscore.hpp");
