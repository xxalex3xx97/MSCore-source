---
title: Native C API
parent: Mod authors
nav_order: 3
---

# Native C API

The native part is `MSCore.dll`, installed as `ue4ss\Mods\MSCore\dlls\main.dll`. It exports C functions that other native mods (C++ UE4SS mods) can call with `GetProcAddress`.

**Native API version: 16.** `MSCore_api()` returns `16`. This counter is separate from the Lua API version (3). Check it before you use the other exports. Version 3 added `MSCore_scan`, `MSCore_chunk_hold`, `MSCore_chunk_release` and `MSCore_chunk_ready`. A mod that needs them refuses to load below 3. Version 4 added the factory exports `MSCore_send_typed` to `MSCore_inv_move` (see the API 4 section). A mod that needs them checks `MSCore_api() >= 4`. Version 5 added the station block natives (`add_block`, `remove_block`, `pending`, `read_block_class`). They are Lua only, so there is no new C export in version 5 (see the API 5 section). Version 6 added `read_docks` (Lua only), the block id column of `read_station`, and its 9th column `mids` (child MachineIDs of each block, comma-separated; see `msc_station.hpp`). Version 7 added `MSCore_chunk_hold2` (radius 0 or 1; see the table row below). Version 8 added `MSCore_class` and `MSCore_scan_rocks`. Version 9 added `MSCore_ship_shields`. Version 10 added the flight exports (see the API 10 section). Version 11 added `MSCore_ship_repair` and `MSCore_ship_repair_need` (API 11 section). Version 12 added `MSCore_stations` (API 12 section). Version 13 added `MSCore_fleet_tags` and `MSCore_fleet_ships` (API 13 section). Version 14 added `MSCore_ship_hull` (API 14 section). Version 15 added `MSCore_ship_mining_caps` and `MSCore_presence_step` (API 15 section). Version 16 added the rock registry exports `MSCore_rockreg_*` (API 16 section).

Conventions: all exports use the C calling convention and are declared `extern "C"`. Pointers are game `UObject*` values as `void*`. Chunk and location arrays hold 3 `long long` values in `X, Y, Z` order.

## Exports

Every export, in the order of the groups below. Args are listed without the function name. Chunk = `ch3`, location = `arr3`, both `long long[3]`.

| Export | Args | Returns | What it does | Status |
|---|---|---|---|---|
| `MSCore_api` | `void` | `int` (`16`) | Native API version. Check it first. | Not verified in game |
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
| `MSCore_chunk_hold2` | `int owner, int sys, const long long* ch3, int radius, wchar_t* err, int errlen` | `int` (`0` fail, `1` set, `2` unchanged) | Same as `MSCore_chunk_hold` with `radius`: `1` = 3x3x3 cube, `0` = centre chunk only. Other values fail with `radius must be 0 or 1`. | Not verified in game |
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
| `MSCore_inv_split` | `int inv_id, int src_slot, int amount, int dst_slot, wchar_t* err, int errlen` | `int` (1 ok, 0 fail) | 0.7.6. Local move of `amount` items from slot `src_slot` to the EMPTY slot `dst_slot` of Inventory `inv_id`. Slots are 0-based. Same inventory only, `amount` <= the stack cap. Not a game command (no InventoryInput): co-op clients do not see it, desync accepted. Added in 0.7.6, native API stays 4 (check with `MSCoreNative_export_ok`). | Not verified in game |

{: .warning }
Receiver `0` is silently dropped by the game; pass the player object's id as receiver (fixed in MSCore 0.7.5). See [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}).

## API 4: factory commands

Used by the blueprint and factory tools. Source: `cpp\MSCore\msc_factory.hpp`. Ported from the MiningBench factory probe (`bp_send`, `bp_place`), same route and fields.

`read_station`, `read_docks`, `read_inventory`, `read_block_class` and `rename_dock` are Lua-only (no C export): `MSCoreNative_read_station`, `MSCoreNative_read_docks`, `MSCoreNative_read_inventory`, `MSCoreNative_read_block_class`, `MSCoreNative_rename_dock` (`rename_dock(receiverId, text) -> ok, err`, source `cpp\MSCore\msc_rename.hpp`), source `cpp\MSCore\msc_station.hpp`. `MSCoreNative_inv_split` (0.7.6) is the Lua form of `MSCore_inv_split`: returns `true` or `false, err`.

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

## API 5: station block commands

Lua only. Source: `cpp\MSCore\msc_factory.hpp` (`add_block`, `remove_block`, `pending`) and `cpp\MSCore\msc_station.hpp` (`read_block_class`). Lua globals: `MSCoreNative_add_block`, `MSCoreNative_remove_block`, `MSCoreNative_pending`, `MSCoreNative_read_block_class`.

| Input class | Type id | Receiver | Fields (by name) | Lua |
|---|---|---|---|---|
| `AddBlockInput` | `30` | station id | Class (BuildingBlockObject class), GridTransform (GridLocation X and Y, Height, GridRotation) | `MSCoreNative_add_block` |
| `RemoveBlockInput` | `31` | station id | Class, GridTransform (same as above) | `MSCoreNative_remove_block` |
| `PendingBlueprintInput` | `33` | station id | bApply, bReset, PrimaryInventoryID, PlayerInventoryID, EditingFromBlockID | `MSCoreNative_pending` |

Notes:

- These three have no AttachTargetID field (BPProbe 2026-11-06). Use the station id as receiver, not `0`.
- `classPath` is the full class path. The class must be a BuildingBlockObject class found by type (UClass). `Default__` objects are refused.
- `remove_block` drops the first pending entry that matches Class and GridTransform. The block ID is not compared.
- `pending` takes exactly one of apply or reset. `apply` needs both inventory ids > 0. Never send `0` to Pending.
- `read_block_class` reads the class defaults by reflection (no offsets). It returns the bounds, the `ItemCost` list and the `bStationBlock` / `bShipBlock` flags. Bounds are raw values, `65536` = 1.0. Probe 2026-11-08: BlockBounds (raw / 65536) are 4x the grid unit horizontally and 2x vertically (Station_01 spans 16, its blocks sit 4 apart). The game keeps each box slightly under its size, so inclusive overlap checks do not touch.

{: .warning }
**Not for mods:** `start_mod` and `uninstall_mod` are the UE4SS entry points of the DLL. Do not call them.

## API 8: class names and rocks (0.8.10)

| Export | Signature | Returns | Notes |
|---|---|---|---|
| `MSCore_class` | `const char* logical, wchar_t* out, int n` | `int` (`1` found, `0` unknown or too small) | Game class name for a logical name (asteroid, asteroid_actor, floating_ore, station, player_station, container, container_actor, item_crate, ship). Names live in msc_contract.hpp. |
| `MSCore_scan_rocks` | `int sys, int loaded_only, out, int max` | `int` (`-2` scan off, `-1` exception, else total rows) | Asteroid rows of one solar system, as `MsScanRow`. Kill switch: `module_disable("scan")`. |

## API 9: ship shields (0.8.12)

| Export | Signature | Returns | Notes |
|---|---|---|---|
| `MSCore_ship_shields` | `void* ship, int* cur, int* max` | `int` bitmask: `1` cur readable, `2` max readable, `0` none (or ship not alive), `-2` ships module off, `-1` exception | Shield in whole units (raw PNum Value / 65536, floored). `cur` / `max` get `-1` when unreadable. Reads `Shields` / `MaxShields` PNum first, `GetShields` / `GetMaxShields` double when the PNum is missing. Negative or NaN = unreadable. Kill switch: `module_disable("ships")`. Source `cpp\MSCore\msc_ships.hpp`. |

## API 10: flight (0.8.13)

| Export | Signature | Returns | Notes |
|---|---|---|---|
| `MSCore_ship_flight` | `int sid, int mask, MsFlight* out` | `int` `1` read, `0` ship gone, `-1` exception or a group faulted (module off), `-2` flight module off, `-3` bad size or null out | `out->size` must be `sizeof(MsFlight)` (264). `mask`: `1` POSE, `2` STATE, `4` LIMITS, `8` POWER (OR them). Each value is written only when its read worked; `flags` says which (see the MsFlight table). Kill switch: `module_disable("flight")`. Source `cpp\MSCore\msc_flight.hpp`. |
| `MSCore_ship_set_throttle` | `int sid, int step, wchar_t* err, int errlen` | `int` (`1` ok, `0` fail) | Writes the `Throttle` IntProperty of ship `sid`. |
| `MSCore_ship_zero` | `int sid, wchar_t* err, int errlen` | `int` (`1` ok, `0` fail) | `Throttle = 0` and `RotationImpulseInput` X, Y, Z = 0. |
| `MSCore_ship_console` | `int sid` | `int` (`>0` PilotConsoleObject id, `0` none, `-2` flight off, `-1` exception) | Console matched by `ShipID`, then `ParentID`. |
| `MSCore_actor_loc` | `void* actor, double* out3` | `int` (`1` ok, `0` fail, `-2` flight off, `-1` exception) | `K2_GetActorLocation` of the actor, in uu. |

### MsFlight (264 bytes)

| Off | Field | Type | Group | Meaning |
|---|---|---|---|---|
| 0 | size | int32 | - | set by the caller to `sizeof(MsFlight)` |
| 4 | flags | int32 | - | `FF_*` bits (below) |
| 8 | sid | int32 | - | ship id |
| 12 | sys | int32 | POSE | solar system id (0 when unreadable) |
| 16 | throttle | int32 | STATE | `Throttle` |
| 20 | steps | int32 | STATE | `GetThrottleSteps` |
| 24 | lp_thr | int32 | POWER | `FlightModel.LowPowerThrottle` |
| 28 | pad | int32 | - | |
| 32 | ch[3] | int64 | POSE | chunk location |
| 56 | max_mod_raw | int64 | LIMITS | `GetModifiedMaxSpeed`, raw |
| 64 | max_base_raw | int64 | LIMITS | `GetMaxSpeed`, raw |
| 72 | coll_radius_raw | int64 | STATE | `CollisionRadius`, raw |
| 80 | act_loc[3] | double | POSE | actor location, uu |
| 104 | fw[3] | double | POSE | actor forward axis |
| 128 | R[3] | double | POSE | actor right axis |
| 152 | U[3] | double | POSE | actor up axis |
| 176 | loc_raw[3] | double | POSE | ship object `Location`, raw (chunk-relative) |
| 200 | quat[4] | double | POSE | ship `Quat` W, X, Y, Z, divided by 65536 |
| 232 | grid_prod | double | POWER | `PowerGrid.TotalProduction` |
| 240 | grid_cons | double | POWER | `PowerGrid.TotalConsumption` |
| 248 | grid_eff | double | POWER | `PowerGrid.ConsumerEfficiency` |
| 256 | lp_frac | double | POWER | `FlightModel.MaxLowPowerSpeed` (fraction) |

Flag bits (`FF_*`): 1 ALIVE, 2 HAS_ACTOR (Actor non-null), 4 ACTOR_FRAME, 8 OBJ_POSE (ch, loc_raw, quat), 16 AUTOPILOT (`bAutopilot` true), 32 DOCK_AP (`bDockingAutopilot` true), 64 TRAVELLING (`bTravelling` true), 128 GRID, 256 LP_KNOWN, 512 LP_ON, 1024 LP_LIMITS, 2048 THROTTLE, 4096 STEPS, 8192 COLL (`CollisionRadius` > 0). The three bool bits are set only when true, so a false bool and an unreadable bool look the same.

## API 11: repair

| Export | Signature | Returns | Notes |
|---|---|---|---|
| `MSCore_ship_repair` | `int sid, int count, int creative, wchar_t* err, int errlen` | `int` (`1` ok, `0` fail) | Sends the game's `RepairFrameInputCommand` (`CreateInputRepair(count, bCreative)`, receiver = ship id) through `AddInputCommand`. `creative` non-zero = free (no repair items used). One item heals 10 Health. Kill switch: `module_disable("repair")`. Source `cpp\MSCore\msc_repair.hpp`. |
| `MSCore_ship_repair_need` | `int sid` | `int` (`>0` items needed, `0` full hull, `-1` unreadable or exception, `-2` repair module off) | `ceil((GetTotalHull - GetCurrentHull) / 10)`. |

## API 12: station names

| Export | Signature | Returns | Notes |
|---|---|---|---|
| `MSCore_stations` | `MsStationName* out, int max` | `int` (`n` stations found, `-2` stations module off, `-1` fault or exception) | Writes at most `max` rows into `out` (skipped when `out` is null or `max <= 0`; `n` is still the total, so call once with `NULL, 0` to size the buffer). `MsStationName` = `int32 id, int32 pad, wchar_t name[96]` (200 bytes); `name` is the DisplayName, tab / CR / LF turned into spaces, max 95 chars + NUL, empty when unset. Reads only (no ProcessEvent). Each call does a full scan (the count call and the fill call are two scans). Kill switch: `module_disable("stations")`. Source `cpp\MSCore\msc_stnames.hpp`. |

## API 13: fleets

| Export | Signature | Returns | Notes |
|---|---|---|---|
| `MSCore_fleet_tags` | `MsFleetTag* out, int max, int max_age_ms` | `int` (`n` tags, `-2` fleets module off, `-1` fault) | `MsFleetTag { int32 id; int32 parent; wchar_t name[96]; }` (200 B). `max_age_ms = 0` rescans now (no cache); otherwise a snapshot younger than `min(max_age_ms, 10000)` is reused. The export has no default: the caller passes it (Lua `fleets_read()` with no argument passes `0`; `mscore_fleets.lua` passes `3000`). Kill switch `module_disable("fleets")`. Source `cpp\MSCore\msc_fleets.hpp`. |
| `MSCore_fleet_ships` | `MsFleetShip* out, int max, int max_age_ms` | `int` (`n` ships, `-2` fleets module off, `-1` fault) | `MsFleetShip { int32 ship; int32 count; int32 total; int32 pad; int32 tags[8]; }` (48 B). `count` = ids stored (at most 8), `total` = real number of fleet ids, so a ship in 9+ fleets shows 8 ids. Same `max_age_ms` rule, kill switch and source. |

## API 14: ship hull (0.8.15)

Call from the game thread (`MSCore_ship_hull` walks game objects).

| Export | Signature | Returns | Notes |
|---|---|---|---|
| `MSCore_ship_hull` | `void* ship, int* cur, int* max` | `int` bitmask: `1` cur readable, `2` max readable, `0` none (or ship not alive), `-2` ships module off, `-1` exception | Hull in whole units from `GetCurrentHull` / `GetTotalHull` (int). `cur` / `max` get `-1` when unreadable. Kill switch: `module_disable("ships")`. Source `cpp\MSCore\msc_ships.hpp`. Native API 14. |

## API 15: mining caps and presence debounce (0.8.15)

Call from the game thread (`MSCore_ship_mining_caps` reads game objects). `MSCore_presence_step` is pure and has no game access.

| Export | Signature | Returns | Notes |
|---|---|---|---|
| `MSCore_ship_mining_caps` | `void* ship` | `int` bitmask: `1` MiningLaser present, `2` ItemScooper present, `0` readable with no mining weapon, `-1` unknown, `-2` ships module off, `-1` also on exception | Reads `DMSPlayerShipObject.Weapons` and matches each weapon's class name (`MiningLaser`, `ItemScooper`). `-1` unknown: ship null or dead, array unreadable, count outside 0..100000, or a native fault (logged once). An empty readable array is `0`, not `-1`. Kill switch: `module_disable("ships")`. Source `cpp\MSCore\msc_weapons.hpp`. Native API 15. |
| `MSCore_presence_step` | `int bad_n, int listed, int read, int* bad_out` | `int` listed flag `1` / `0` | Debounce for "still in the list". `read`: `1` present, `0` absent, any other value (`-1` unknown) = unknown. `read 1`: bad = 0, listed. Unknown: bad unchanged, keeps `listed`. `read 0`: bad = min(bad_n + 1, 3); a never-listed ship is rejected at once, a listed one is dropped on the 3rd consecutive absent read. `bad_out` may be `NULL`. Source `cpp\MSCore\msc_presence.hpp`. Native API 15. |

## API 16: rock registry (0.8.15)

One shared table of every resource asteroid, seeded from the save objects (class `SmallResourceAsteroidObject`), nearest to the ships first, cached on disk, kept current by a delete listener. Mods read it through these exports instead of walking the game. Source `cpp\MSCore\msc_rockreg.hpp` (game side) and `cpp\MSCore\msc_rockreg_logic.hpp` (pure logic, unit test `tests\test_rockreg.cpp`).

Rules for every export:

- **Game thread only.**
- **Units: metres.** Frame = `chunk * 2048 + loc_raw / 65536`, which equals the actor location in uu divided by 100. `MsRegShip.p` and `.q` and every `double[3]` argument use that frame. Radii are metres too.
- **`-2`** = the `rockreg` module is off (caps row `rockreg`: off after a native fault, a selftest layout mismatch, or when `rocks` is off). **`-1`** = exception inside MSCore (the module is then also off). Every body runs under SEH; a fault turns the module off for the session.
- Rock ids are the save object ids (`ObjectId` of the rock actor).
- Cache file `<MSCore mod folder>\rockreg\RockRegCache.bin`. The per-mod `MiningRegistryCache.bin` files of older mining / Fleet builds are orphaned and safe to delete.

**Feed protocol.** MSCore has no frame hook for the registry. Every mod that uses it calls `MSCore_rockreg_feed` from its own frame loop about every 0.25 s, with its `owner` id (**1 = standalone mining mod, 2 = Fleet**), its ships (position in metres, system id, optional approach segment to a rock centre `q`) and the rock ids its ships target. MSCore keeps the last feed per owner, merges the feeds younger than 2000 ms (ships by `sid`, targets as a set) and runs one registry tick when at least 200 ms passed since the last one, so two mods fed together still tick once. A mod that stops feeding drops out of the merge after 2 s. The first feed after the game start waits about 5 s for the world, then seeds (about 3.5 min for 100k rocks without a cache, seconds with one). Until `MSCore_rockreg_area_ready` says 1 for an area, only rocks already seeded are known there.

| Export | Signature | Returns | Notes |
|---|---|---|---|
| `MSCore_rockreg_feed` | `int owner, const MsRegShip* ships, int n, const int* targets, int nt` | `1` | Stores the feed and runs the tick when due. `ships` and `targets` may be `NULL` with `n`/`nt` 0. |
| `MSCore_rockreg_state` | `MsRegState* out` | `1` | Fills the state struct. |
| `MSCore_rockreg_get` | `int id, MsRegRock* out` | `1` found (dead rocks too: check `flags` bit 0), `0` unknown | |
| `MSCore_rockreg_box` | `int sys, const double lo[3], const double hi[3], MsRegRock* out, int max` | total alive rocks in the box | `out` gets `min(total, max)` rocks; `out = NULL` or `max = 0` counts only. |
| `MSCore_rockreg_seg` | `int sys, const double a[3], const double b[3], double margin_m, MsRegRock* out, int max` | total alive rocks whose centre is within `margin_m` of the segment a to b | Same `out` rule. The path check uses it. |
| `MSCore_rockreg_nearest_n` | `int sys, void* item, const double p[3], const int* skip_ids, int nskip, double max_m, int n, MsRegRock* out, double* dist_out` | count (nearest first) | `item` = Item `UObject*`: only rocks holding that ore (matched by display name); an item no rock holds returns 0 rocks. `item = NULL`: any ore. `max_m` must be > 0. `skip_ids` may be `NULL`. `dist_out` (may be `NULL`) gets the centre distances in metres. |
| `MSCore_rockreg_nearest_surface` | `int sys, const double p[3], int skip_id, double unknown_rad_m, MsRegRock* out, double* surf_m` | `1` found, `0` none | Smallest `centre distance - radius`. A radius not read yet counts as `unknown_rad_m`. `skip_id > 0` is left out. `surf_m` may be `NULL`. |
| `MSCore_rockreg_area_ready` | `int sys, const double p[3]` | `1` seeded, `0` not yet | `1` when the chunk of `p` and its neighbours lie inside a seeded zone (every save there is read). |
| `MSCore_rockreg_report` | `int kind, int sys, const double p[3], double arg, wchar_t* out, int len` | `1` | Writes a report file to `<MSCore mod folder>\rockreg` and a one-line text to `out` (may start with `error`). `kind` 0 dump around `p` (`RockRegDump.txt`), 1 query (`arg` = radius m, `RockRegQuery.txt`), 2 check (`arg` = n rocks, `RockRegCheck.txt`: registry vs loaded actor positions). |

Radius reads happen lazily for rocks near a ship or a targeted rock, so `flags` bit 1 may be 0 (`rad_m` is then 0).

### MsRegRock (48 bytes)

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0 | `id` | `int32_t` | Save object id. |
| 4 | `sys` | `int32_t` | Solar system id. |
| 8 | `flags` | `int32_t` | bit 0 alive, bit 1 radius known. |
| 12 | `pad` | `int32_t` | |
| 16 | `c` | `double[3]` | Centre, metres. |
| 40 | `rad_m` | `double` | Radius, metres (valid when bit 1). |

### MsRegShip (64 bytes)

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0 | `sid` | `int32_t` | Ship save id. |
| 4 | `sys` | `int32_t` | Solar system id (0 = unknown, the ship is ignored for seeding). |
| 8 | `has_seg` | `int32_t` | Non-zero: `q` is valid (approach to a rock centre). |
| 12 | `pad` | `int32_t` | |
| 16 | `p` | `double[3]` | Ship centre, metres. |
| 40 | `q` | `double[3]` | Approach target, metres. |

### MsRegState (40 bytes)

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0 | `world_ok` | `int32_t` | A game world is loaded. |
| 4 | `world_wait` | `int32_t` | World changed; the next seed waits a few seconds. |
| 8 | `seeding` | `int32_t` | A seed pass runs. |
| 12 | `near_done` | `int32_t` | Phase A of the pass (rocks near the ships) is read. |
| 16 | `rocks` | `int64_t` | Rows in the table (dead included). |
| 24 | `alive` | `int64_t` | Alive rocks. |
| 32 | `version` | `uint64_t` | Bumped on every add, kill and revive. |

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
