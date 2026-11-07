---
title: Native C API
parent: Mod authors
nav_order: 3
---

# Native C API

The native part is `MSCore.dll`, installed as `ue4ss\Mods\MSCore\dlls\main.dll`. It exports C functions that other native mods (C++ UE4SS mods) can call with `GetProcAddress`.

**Native API version: 4.** `MSCore_api()` returns `4`. This counter is separate from the Lua API version (3). Check it before you use the other exports. Version 3 added `MSCore_scan`, `MSCore_chunk_hold`, `MSCore_chunk_release` and `MSCore_chunk_ready`. A mod that needs them refuses to load below 3. Version 4 added the factory exports `MSCore_send_typed` to `MSCore_inv_move` (see the API 4 section). A mod that needs them checks `MSCore_api() >= 4`.

Conventions: all exports use the C calling convention and are declared `extern "C"`. Pointers are game `UObject*` values as `void*`. Chunk and location arrays hold 3 `long long` values in `X, Y, Z` order.

## Exports

Every export, in the order of the groups below. Args are listed without the function name. Chunk = `ch3`, location = `arr3`, both `long long[3]`.

| Export | Args | Returns | What it does | Status |
|---|---|---|---|---|
| `MSCore_api` | `void` | `int` (`4`) | Native API version. Check it first. | Not verified in game |
| `MSCore_add_input` | `int receiver, void* cmd, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Sends `DMSPlayerController.AddInputCommand`. `receiver` = ship id. `cmd` = live input command UObject. Does not apply the ShipMovementInputCommand type id fix that `cmd.send` applies. | In game (MiningBench) |
| `MSCore_view_send` | `int cid, long long yaw_raw, long long pitch_raw, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Sets the view offset. `cid` = PilotConsole ID. `yaw_raw` and `pitch_raw` are raw values, 65536 per degree. | In game (MiningBench) |
| `MSCore_throttle` | `int sid, int delta, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Sends one ShipMovementInputCommand with `DeltaThrottle = delta`. `sid` = ship id. | In game (MiningBench) |
| `MSCore_find_item` | `const char* res, wchar_t* why, int len` | `void*` (Item UObject, or `NULL`) | `res` = object name, display name (case-insensitive) or numeric ID as text. `NULL` with `why` set on zero or several matches. | Not verified in game |
| `MSCore_find_dock` | `const char* name, wchar_t* why, int len` | `int` (dock id > 0; `-1` none; `-2` several) | `name` = in-game dock name (DisplayName). `why` says which case applies when there is no single match. | Not verified in game |
| `MSCore_new_order` | `int receiver, const wchar_t* cls, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | `cls` = class name without the path, for example a DeliverOrder class name. `receiver` = ship id or parent order id. | Not verified in game |
| `MSCore_update_order` | `int oid, int target, void* item, int cmp, int amount, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Updates order `oid`. `item` = Item UObject or `NULL`. `target` = dock or `0`. | Not verified in game |
| `MSCore_delete_order` | `int sid, int oid, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Deletes order `oid` of ship `sid`. | Not verified in game |
| `MSCore_autopilot` | `int sid, int on, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | `on`: `0` = off, any nonzero = on. | Not verified in game |
| `MSCore_skip_to` | `int sid, int idx, int* before, int* after, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Jumps to order index `idx`. `before` and `after` receive `CurrentOrderIndex` around the call. Either pointer may be `NULL`. | Not verified in game |
| `MSCore_filter_send` | `int sid, int idx, void* item, int amount, int action, int remove, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Sets logistic filter row `idx`. `action`: `0` = LIMIT, `1` = BUFFER. `remove != 0` removes row `idx`. `item` may be `NULL` (empty row). | Not verified in game |
| `MSCore_filters_read` | `void* ship, MsFilterRow* out, int max` | `int` (row count, or `-1` on failure) | Copies `min(count, max)` rows into `out`. `out = NULL` or `max = 0`: count only. | Not verified in game |
| `MSCore_warp_send` | `int sid, int sys, const long long* ch3, const long long* arr3, int wrap, int set_target, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Teleport warp for ship `sid` into system `sys`. `wrap != 0` wraps in piloting on/off. `set_target != 0` sets `bSetTarget` on the command. | Not verified in game |
| `MSCore_arrival` | `void* ship, int sys, const long long* ch3, long long dist, long long* arr3, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Jump arrival point in sector `ch3` of system `sys`. `dist` = warp arrival distance (raw). `arr3` receives the raw location. | Not verified in game |
| `MSCore_jump_cost` | `void* ship, int sys, const long long* ch3, int* cost, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | `cost` receives the jump cost integer. | Not verified in game |
| `MSCore_chunk_of` | `void* obj, long long* ch3` | `int` (1 ok, 0 fail) | Fills `ch3` with the chunk of `obj`. | Not verified in game |
| `MSCore_axes_of` | `void* obj, const wchar_t* prop, long long* out3` | `int` (1 ok, 0 fail) | Fills `out3` with the X, Y, Z raw values of the struct property `prop` of `obj`. | Not verified in game |
| `MSCore_sys_id` | `void* obj` | `int` (system id) | Solar system id of `obj`. `0` when unreadable. | Not verified in game |
| `MSCore_sec_warp_dist` | `void* sec, long long* out` | `int` (1 ok, 0 fail) | Writes `GenParams.WarpArrivalDistance` (raw) of the sector to `*out`. | Not verified in game |
| `MSCore_sectors` | `MsSector* out, int max` | `int` (total found, or `-1` on exception) | Writes every SolarSystemSector into `out[0..max)`. `out = NULL` or `max = 0`: count only. | Not verified in game |
| `MSCore_pin_ensure` | `void` | `int` (1 when armed) | Arms the map pin hook. Call every tick until it returns `1`. The controller does not exist when the mod loads. | Not verified in game |
| `MSCore_pinned` | `void` | `int` (sector id, `0` = none) | Last map-pinned sector id. | Not verified in game |
| `MSCore_scan` | `int kind, long long max_age_ms, const char* type, int sys, int loaded_only, MsScanRow* out, int max` | `int` (row total, or `-1` on exception) | `kind`: `0` ship, `1` asteroid, `2` station. `max_age_ms` = cache age in ms, capped at `5000` (`0` rescans now). Served rows are checked for identity (object id, actor, ore ids) but not refreshed. `type` = `NULL` or `""` for no filter; otherwise a case-insensitive match on the row type, object name or any ore name. `sys > 0` keeps one solar system. `loaded_only` (asteroids): only rock actors in the world. Copies `min(total, max)` rows into `out`. `out = NULL` or `max = 0`: count only. Returns `0` rows while the native scan is off (see `MSCore_scan_ok`). | Not verified in game |
| `MSCore_scan_ok` | `void` | `int` (`1` on, `0` off for this session) | Kill switch state. `0` after a native scan walk faulted: `MSCore_scan` then returns `0` rows, and `MSCore.scan` uses its pure-Lua fallback. Read-only. | Not verified in game |
| `MSCore_chunk_hold` | `int owner, int sys, const long long* ch3, wchar_t* err, int errlen` | `int` (`0` fail, `1` set, `2` unchanged) | Loads the 3x3x3 chunk cube around `ch3` in system `sys` for `owner`. Each owner holds one cube; a new call moves it. Chunks the player's own zone uses are never set; such a chunk is pending and set by a later call once the player has left the zone (`2` is returned only when nothing was added). | Not verified in game |
| `MSCore_chunk_release` | `int owner` | `int` (`1` done, `0` exception) | Drops the owner's cube. `1` also when it had none. | Not verified in game |
| `MSCore_chunk_ready` | `wchar_t* why, int len` | `int` (`1` ready, `0` not) | `1` when the chunk-hold natives were found by byte pattern. `why` gets the reason otherwise. Read-only. | Not verified in game |
| `MSCore_send_typed` | `int receiver, void* cmd, int type_id, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | Sends `cmd` to `receiver` with AddInputCommand. Writes `type_id` at `cmd+0x28` only when that int is `0`. `type_id` `0` is refused. | Not verified in game |
| `MSCore_find_buildable` | `const char* dat_name` | `void*` (Buildable UObject, or `NULL`) | Buildable data asset by object name, for example `DAT_Refinery`. First match. | Not verified in game |
| `MSCore_build_place` | `int station, void* buildable, int x, int y, int height, int rot, const uint8_t* cfg, int cfglen, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | BuildInput at receiver `0`: `buildable` = `MSCore_find_buildable` result, `AttachTargetID` = `station`, GridTransform (`x`, `y`, `height`, `rot`), identity quat, `MachineConfigData` = `cfg` (`NULL`/`0` = none). Type id `0x2e`. | Not verified in game |
| `MSCore_disassemble` | `int target_id, int mass, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | DisassembleInput at receiver `0`: `TargetId` = `target_id`, `bMassDisassemble` = `mass != 0`. Type id `0x2f`. | Not verified in game |
| `MSCore_set_recipe` | `int machine, void* recipe, int amount, const uint8_t* cfg, int cfglen, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | SetRecipeInput at receiver = `machine` id (must be > 0): `Recipe`, `Amount`, `Config`. `cfg` must be 0 bytes or 4 bytes (int32 `Config`, little endian). Type id `20`. | Not verified in game |
| `MSCore_port_toggle` | `int machine, int port, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | MachinePortInput at receiver = `machine` id (must be > 0): `PortIndex` = `port`. Toggles that port. Type id `17`. | Not verified in game |
| `MSCore_inv_move` | `int primary_inv_id, int secondary_inv_id, int slot, int action, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | InventoryInput at receiver `0`. **The game silently drops receiver 0**, so this export never moves anything; use `MSCore_inv_move_to`. Kept for ABI compatibility. Type id `12`. | Not verified in game |
| `MSCore_inv_move_to` | `int primary_inv_id, int secondary_inv_id, int slot, int action, int receiver, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | InventoryInput sent to `receiver` = the **player object id** (the player handler applies it). `PrimaryInventoryID`, `SecondaryInventoryID`, `SlotIndex`, `Action` (u8; `2` = quick move). Added in 0.7.5, native API stays 4 (check with `MSCoreNative_export_ok`). Type id `12`. | Not verified in game |

## API 4: factory commands

Used by the blueprint and factory tools. Source: `cpp\MSCore\msc_factory.hpp`. Ported from the MiningBench factory probe (`bp_send`, `bp_place`), same route and fields.

`read_station` and `read_inventory` are Lua-only (no C export): `MSCoreNative_read_station`, `MSCoreNative_read_inventory`, source `cpp\MSCore\msc_station.hpp`.

Typed id table (value written at `cmd+0x28`, the int that the flush checks):

| Input class | Type id | Receiver | Fields (offset in the object) | Export |
|---|---|---|---|---|
| `BuildInput` | `0x2e` (46) | `0` | Buildable@0x40, Location, Quat, Chunk, SolarSystemID, AttachTargetID@0x74, GridTransform@0x78, Config, MachineConfigData@0x90 | `MSCore_build_place` |
| `DisassembleInput` | `0x2f` (47) | `0` | TargetId@0x40, bMassDisassemble@0x44 | `MSCore_disassemble` |
| `InventoryInput` | `12` | player object id (`0` is dropped) | PrimaryInventoryID@0x40, SecondaryInventoryID@0x44, SlotIndex@0x48, Action(u8)@0x4c | `MSCore_inv_move` |
| `SetRecipeInput` | `20` | machine id | Recipe@0x40, Amount@0x48, Config@0x4c | `MSCore_set_recipe` |
| `MachinePortInput` | `17` | machine id | PortIndex@0x40 | `MSCore_port_toggle` |
| `SetConfigInput` | `21` | machine id | Config@0x40, Config2@0x44, PlayerObjectID@0x48 | no export yet (use `MSCore_send_typed` with a built object) |

Notes:

- `MSCore_send_typed` is the generic route. Build the object yourself (NewObject or StaticConstructObject of the input class), set its fields, then send it with the type id.
- The factory exports build their object with StaticConstructObject, so the type id is written by hand. That is why `MSCore_send_typed` writes it when `cmd+0x28` is `0`.
- A receiver of `0` is the world. Never put a station id there for a build.
- Each factory export returns `0` with `err` set when a field is missing, the class is missing, or a native call faults (`native fault`). A missing field is named in `err` (`BuildInput field missing: <path>`).
- `cfglen` above 64 KiB (65536 bytes) is refused with an error (`cfg larger than 64 KiB`).
- Native exports must be called from the game thread (ProcessEvent/FindAllOf are not thread-safe).
- `MSCore_find_buildable` returns a pointer into the live game objects. Do not cache it across level or save loads in native code. Look it up again each time. (The Lua wrapper `MSCore.factory_native.find_buildable` caches per name and re-checks `IsValid`.)
- A native fault inside `MSCore_find_buildable` returns `NULL` and logs a line.
- Not verified in game: all the factory exports above.

{: .warning }
**Not for mods:** `start_mod` and `uninstall_mod` are the UE4SS entry points of the DLL. Do not call them.

## Error convention

Most functions that take `wchar_t* err, int errlen` follow the same rule:

- Return value `1` = success, `0` = failure.
- `err` gets the reason as a null-terminated wide string. `errlen` is the size of the buffer in `wchar_t` units. Longer text is truncated.
- Exceptions inside MSCore are caught and reported as `"exception"` with return `0`.
- Pass a buffer of at least 256 `wchar_t` to get useful messages.

Functions with a `why` buffer (`MSCore_find_item`, `MSCore_find_dock`) use the same buffer rule for the reason.

Functions that take a `void* obj` check that the object is alive before they use it. A dead object returns `0`, `NULL` or `-1`, depending on the function.

## Structs

The structs use the default C packing of a 64-bit build. Declare them exactly like this in your code. The sizes are checked with `static_assert` in MSCore.

### MsFilterRow (16 bytes)

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0 | `item` | `void*` | Item UObject. |
| 8 | `amount` | `int32_t` | |
| 12 | `action` | `int32_t` | `0` LIMIT, `1` BUFFER. |

```cpp
struct MsFilterRow { void* item; int32_t amount; int32_t action; };
```

### MsSector (584 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | `id` | Save id of the sector. |
| 4 | 4 | `sys` | Solar system id. |
| 8 | 4 | `nores` | Number of ore entries in `ores`. |
| 12 | 4 | `pad` | Unused. |
| 16 | 24 | `ch[3]` | Chunk X, Y, Z (`long long`). |
| 40 | 24 | `he[3]` | Half extents X, Y, Z (`long long`). |
| 64 | 8 | `warp_dist` | Warp arrival distance (raw). |
| 72 | 320 | `ores[40]` | Ore item UObject pointers, max 40. |
| 392 | 192 | `name[96]` | Display name, `wchar_t`, null-terminated. |

```cpp
struct MsSector {
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
```

### MsScanRow (688 bytes)

Filled by `MSCore_scan`. Field notes: `kind` 0 ship, 1 asteroid, 2 station. `hull`, `max_hull` and `order_idx` are `-1` when unknown. `piloted` = the player pilots this ship. `loaded` = the rock actor is in the world (asteroids). `world` is the actor location in UE units, valid when `world_ok` is `1`. `loc` is chunk-relative and raw. `order` is the class name of the current order, empty when none.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 8 | `obj` | Save object, or the rock actor for loaded rows. |
| 8 | 8 | `actor` | Rock actor, or the object's `Actor` property. Null when none. |
| 16 | 4 | `id` | Save object id (`ObjectId` for loaded rock rows). |
| 20 | 4 | `kind` | `0` ship, `1` asteroid, `2` station. |
| 24 | 4 | `sys` | Solar system id. |
| 28 | 4 | `loaded` | `1` when the rock actor is in the world. |
| 32 | 4 | `docked` | `1` when `DockID` is not 0. |
| 36 | 4 | `piloted` | `1` when the player pilots the ship. |
| 40 | 4 | `dock_id` | `DockID`. |
| 44 | 4 | `hull` | Current hull, `-1` unknown. |
| 48 | 4 | `max_hull` | `MaxHealth`, `-1` unknown. |
| 52 | 4 | `order_idx` | `CurrentOrderIndex`, `-1` none or out of range. |
| 56 | 4 | `order_dock` | `DockID` of the current order. |
| 60 | 4 | `world_ok` | `1` when `world` was read. |
| 64 | 24 | `ch[3]` | Chunk X, Y, Z (`long long`). |
| 88 | 24 | `loc[3]` | Chunk-relative location X, Y, Z (raw). |
| 112 | 24 | `world[3]` | Actor location X, Y, Z (`double`, UE units). |
| 136 | 4 | `nores` | Number of ore entries in `ores` (plus 4 bytes pad). |
| 144 | 64 | `ores[8]` | Ore item UObject pointers. |
| 208 | 128 | `name[64]` | Display name, `wchar_t`. |
| 336 | 128 | `type[64]` | Type: class name (ship, station), first ore display name (asteroid). |
| 464 | 128 | `oname[64]` | Object name. |
| 592 | 96 | `order[48]` | Current order class name, `wchar_t`. |

```cpp
struct MsScanRow {
    void* obj;
    void* actor;
    int32_t id;
    int32_t kind;
    int32_t sys;
    int32_t loaded;
    int32_t docked;
    int32_t piloted;
    int32_t dock_id;
    int32_t hull;
    int32_t max_hull;
    int32_t order_idx;
    int32_t order_dock;
    int32_t world_ok;
    long long ch[3];
    long long loc[3];
    double world[3];
    int32_t nores;
    void* ores[8];
    wchar_t name[64];
    wchar_t type[64];
    wchar_t oname[64];
    wchar_t order[48];
};
```

The header `cpp\MSCore\msc_api.hpp` in the MSCore source has the same definitions. Copy the layout, not the file.

## Loading from another native mod

Load the MSCore DLL by path, then resolve the exports you need. MiningBenchMod uses a path relative to its own DLL folder.

```cpp
#include <windows.h>

typedef int (*MSCore_api_fn)(void);
typedef int (*MSCore_view_send_fn)(int, long long, long long, wchar_t*, int);

// dll_path: ue4ss\Mods\MSCore\dlls\main.dll, found from your own DLL folder
HMODULE h = LoadLibraryW(dll_path);
if (!h) return false;

auto api = reinterpret_cast<MSCore_api_fn>(GetProcAddress(h, "MSCore_api"));
if (!api || api() < 3) return false;   // API too old or missing

auto view_send = reinterpret_cast<MSCore_view_send_fn>(GetProcAddress(h, "MSCore_view_send"));
wchar_t err[256] = {0};
if (view_send && !view_send(cid, 0, 0, err, 256)) {
    // err holds the reason
}
```

`cid` is the pilot console id (`ID` property) of the ship's `PilotConsoleObject`.

## Gotchas

- Always resolve `MSCore_api` first and stop if it returns less than the version your code needs (`3` for the scan and chunk hold exports).
- Use a 256 `wchar_t` buffer for `err` so you get the full reason.
- The MSCore export `MSCore_add_input` skips the type id fix. The Lua `cmd.send` applies it.
