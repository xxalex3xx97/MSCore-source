---
title: Native C API
parent: Mod authors
nav_order: 3
---

# Native C API

The native part is `MSCore.dll`, installed as `ue4ss\Mods\MSCore\dlls\main.dll`. It exports C functions that other native mods (C++ UE4SS mods) can call with `GetProcAddress`.

**Native API version: 2.** `MSCore_api()` returns `2`. Check it before you use the other exports. The MiningBenchMod refuses to load below 2.

Conventions: all exports use the C calling convention and are declared `extern "C"`. Pointers are game `UObject*` values as `void*`. Chunk and location arrays hold 3 `long long` values in `X, Y, Z` order.

## Exports

Every export, in the order of the groups below. Args are listed without the function name. Chunk = `ch3`, location = `arr3`, both `long long[3]`.

| Export | Args | Returns | What it does | Status |
|---|---|---|---|---|
| `MSCore_api` | `void` | `int` (`2`) | Native API version. Check it first. | Not verified in game |
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

Both structs use the default C packing of a 64-bit build. Declare them exactly like this in your code. The sizes are checked with `static_assert` in MSCore.

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
if (!api || api() < 2) return false;   // API too old or missing

auto view_send = reinterpret_cast<MSCore_view_send_fn>(GetProcAddress(h, "MSCore_view_send"));
wchar_t err[256] = {0};
if (view_send && !view_send(cid, 0, 0, err, 256)) {
    // err holds the reason
}
```

`cid` is the pilot console id (`ID` property) of the ship's `PilotConsoleObject`.

## Gotchas

- Always resolve `MSCore_api` first and stop if it returns less than `2`.
- Use a 256 `wchar_t` buffer for `err` so you get the full reason.
- The MSCore export `MSCore_add_input` skips the type id fix. The Lua `cmd.send` applies it.
