// FILE: cpp/MSCore/msc_api.hpp
// WHAT: plain structs that cross the C export boundary to MiningBenchMod (mb_mscore.hpp). No UE types in here.
// WHY: the mining DLL reads and writes these by layout, so both sides static_assert the sizes (x64: 16 and 584 bytes).
#pragma once
#include <cstddef>
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

// flight state of one ship (API 10, msc_flight.hpp). Caller sets size = sizeof(MsFlight) (mismatch -> -3). Each value is written only when its read worked; flags say which.
// Units: raw = game fixed point (65536 = 1 unit); uu = Unreal units (100 uu = 1 m); chunk = 2048 m cells.
struct MsFlight
{
    int32_t size;               // off 0: set by the caller
    int32_t flags;              // off 4: FF_* bits
    int32_t sid;                // off 8: ship id
    int32_t sys;                // off 12: solar system id (POSE)
    int32_t throttle;           // off 16: Throttle prop (STATE)
    int32_t steps;              // off 20: GetThrottleSteps (STATE)
    int32_t lp_thr;             // off 24: FlightModel LowPowerThrottle (POWER)
    int32_t pad;                // off 28
    int64_t ch[3];              // off 32: chunk location (POSE)
    int64_t max_mod_raw;        // off 56: GetModifiedMaxSpeed (LIMITS), raw
    int64_t max_base_raw;       // off 64: GetMaxSpeed (LIMITS), raw
    int64_t coll_radius_raw;    // off 72: CollisionRadius (STATE), raw
    double act_loc[3];          // off 80: actor location, uu (POSE)
    double fw[3];               // off 104: actor forward axis (POSE)
    double R[3];                // off 128: actor right axis (POSE)
    double U[3];                // off 152: actor up axis (POSE)
    double loc_raw[3];          // off 176: ship object Location, raw, chunk-relative (POSE)
    double quat[4];             // off 200: ship Quat W,X,Y,Z, already / 65536 (POSE)
    double grid_prod;           // off 232: PowerGrid.TotalProduction (POWER)
    double grid_cons;           // off 240: PowerGrid.TotalConsumption (POWER)
    double grid_eff;            // off 248: PowerGrid.ConsumerEfficiency (POWER)
    double lp_frac;             // off 256: FlightModel MaxLowPowerSpeed, fraction (POWER)
};

// MsFlight read groups (mask argument of MSCore_ship_flight)
constexpr int FL_POSE = 1;     // actor frame, ch, loc_raw, quat, sys
constexpr int FL_STATE = 2;    // throttle, steps, the 3 bool flags, collision radius
constexpr int FL_LIMITS = 4;   // both max speeds
constexpr int FL_POWER = 8;    // power grid, low-power flag, low-power limits

// MsFlight.flags bits
constexpr int FF_ALIVE = 1;        // ship found and alive
constexpr int FF_HAS_ACTOR = 2;    // Actor property non-null
constexpr int FF_ACTOR_FRAME = 4;  // act_loc / fw / R / U read
constexpr int FF_OBJ_POSE = 8;     // ch + loc_raw + quat read
constexpr int FF_AUTOPILOT = 16;   // bAutopilot true
constexpr int FF_DOCK_AP = 32;     // bDockingAutopilot true
constexpr int FF_TRAVELLING = 64;  // bTravelling true
constexpr int FF_GRID = 128;       // power grid numbers read
constexpr int FF_LP_KNOWN = 256;   // low-power flag readable
constexpr int FF_LP_ON = 512;      // low-power flag true
constexpr int FF_LP_LIMITS = 1024; // LowPowerThrottle and MaxLowPowerSpeed read
constexpr int FF_THROTTLE = 2048;  // Throttle read
constexpr int FF_STEPS = 4096;     // GetThrottleSteps read
constexpr int FF_COLL = 8192;      // CollisionRadius read and > 0

static_assert(sizeof(MsFlight) == 264, "MsFlight layout must match the MSCore native API 10 doc");
static_assert(offsetof(MsFlight, size) == 0 && offsetof(MsFlight, flags) == 4 && offsetof(MsFlight, sid) == 8, "MsFlight header offsets");
static_assert(offsetof(MsFlight, sys) == 12 && offsetof(MsFlight, throttle) == 16 && offsetof(MsFlight, steps) == 20, "MsFlight state offsets");
static_assert(offsetof(MsFlight, lp_thr) == 24 && offsetof(MsFlight, pad) == 28 && offsetof(MsFlight, ch) == 32, "MsFlight power/chunk offsets");
static_assert(offsetof(MsFlight, max_mod_raw) == 56 && offsetof(MsFlight, max_base_raw) == 64 && offsetof(MsFlight, coll_radius_raw) == 72, "MsFlight limit offsets");
static_assert(offsetof(MsFlight, act_loc) == 80 && offsetof(MsFlight, fw) == 104 && offsetof(MsFlight, R) == 128 && offsetof(MsFlight, U) == 152, "MsFlight frame offsets");
static_assert(offsetof(MsFlight, loc_raw) == 176 && offsetof(MsFlight, quat) == 200 && offsetof(MsFlight, grid_prod) == 232, "MsFlight pose/power offsets");
static_assert(offsetof(MsFlight, grid_cons) == 240 && offsetof(MsFlight, grid_eff) == 248 && offsetof(MsFlight, lp_frac) == 256, "MsFlight power offsets");

// ---- API 16: rock registry (msc_rockreg.hpp). Units: metres, frame = chunk * 2048 + loc_raw / 65536 (= actor location / 100). ----
// one rock: id = save object id, sys = solar system id, flags bit0 alive, bit1 radius known, c = centre (m), rad_m = radius (m, valid when flags bit1)
struct MsRegRock
{
    int32_t id;
    int32_t sys;
    int32_t flags;
    int32_t pad;
    double c[3];
    double rad_m;
};
// one ship fed to the registry: save id, system id, has_seg (q valid), p = centre (m), q = approach target (rock centre, m)
struct MsRegShip
{
    int32_t sid;
    int32_t sys;
    int32_t has_seg;
    int32_t pad;
    double p[3];
    double q[3];
};
// registry state: world_ok (a world is loaded), world_wait (seed waits after a world change), seeding (a pass runs), near_done (phase A of the pass done),
// rocks = rows in the table (dead included), alive = alive rocks, version = bumped on every add / kill / revive
struct MsRegState
{
    int32_t world_ok;
    int32_t world_wait;
    int32_t seeding;
    int32_t near_done;
    int64_t rocks;
    int64_t alive;
    uint64_t version;
};

static_assert(sizeof(MsRegRock) == 48, "MsRegRock layout must match the MSCore native API 16 doc");
static_assert(sizeof(MsRegShip) == 64, "MsRegShip layout must match the MSCore native API 16 doc");
static_assert(sizeof(MsRegState) == 40, "MsRegState layout must match the MSCore native API 16 doc");
static_assert(offsetof(MsRegRock, c) == 16 && offsetof(MsRegRock, rad_m) == 40, "MsRegRock offsets");
static_assert(offsetof(MsRegShip, p) == 16 && offsetof(MsRegShip, q) == 40, "MsRegShip offsets");
static_assert(offsetof(MsRegState, rocks) == 16 && offsetof(MsRegState, alive) == 24 && offsetof(MsRegState, version) == 32, "MsRegState offsets");
