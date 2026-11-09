---
title: Changelog
nav_order: 6
---

# Changelog

## 0.8.3 - Native docks with block ids
- **New:** native `read_docks(stationId)` (`MSCoreNative_read_docks`, `MSCore.factory_native.read_docks`): the Dock objects of one station as rows `{machine_id, block_id, parent_id, class, name, raw_id}`. Fields are found by property name in C++.
- **New:** `read_station` `B` lines have an 8th column, the block's BlockID (`-1` when unreadable). `parse_station` gives it as `bid` (`nil` on older lines).
- **New:** `read_station` `B` lines have a 9th column `mids`: the comma-separated MachineIDs of the block's child machines (`DefaultMachines`, fallback `DefaultMachineIDs`), empty when none. A dock block can read BlockID `-1`, so match docks through `mids`. `parse_station` gives `blocks[i].mids` (a list, `{}` when absent). Not verified in game yet.
- **New:** selfcheck `native.read_docks` (counts, first row, whether a live block bid equals a dock block_id, and how many blocks have a dock machine_id in `mids`).
- **New:** native `rename_dock(receiverId, text)` (`MSCoreNative_rename_dock`, `MSCore.factory_native.rename_dock`): `SetTextInputCommand.CreateInputSetText(Text, 0)` built in C++ (FText made natively, never through Lua), then `AddInputCommand(receiverId, cmd)`. Returns `true` or `false, err`. Name 1-200 characters, receiver > 0. The Lua form `CreateInputSetText` crashes the game (access violation in `FText::ToFString` while UE4SS pushes the null FText back to Lua; `pcall` cannot catch it). Needs the 0.8.3 build 3 DLL.
- **Fixed:** build 1 of 0.8.3 crashed the game (EXCEPTION_ACCESS_VIOLATION inside a UE4SS Lua UStruct lambda). The cause was the selfcheck `native.types` check, which walked UStruct/UProperty reflection from Lua; `pcall` cannot catch a native access violation. Removed. selfcheck has no Lua reflection now.
- Native API is 6 (`MSCore_api()` returns `6`, the new natives are Lua only). Lua API stays 3. Module version `0.8.3`.

## 0.8.2 - Events tick on the game thread
- **Fixed:** the events tick no longer runs Lua on UE4SS's async thread. It runs on the game thread (`LoopInGameThreadWithDelay`, through the `loopGT` helper). The async thread corrupted the shared Lua state during long imports in other mods (Blueprint Unit).
- Native API stays 5, Lua API 3.

## 0.8.1 - Refund redirect only on demand
- **Fixed:** manual deconstruction refunds went into the Blueprint Unit instead of the player. The redirect was on at MSCore start since 0.7.5.
- **Changed:** redirect off by default. Opt-in file `Mods/MSCore/refund_redirect_on.txt` restores the old always-on behaviour (not used if `refund_redirect_off.txt` exists). Mods that need the redirect call `MSCoreNative_redirect_enable(true)` and must turn it off again with `(false)`. Blueprint Unit does this around its own builds.
- Native API stays 5, Lua API 3.

## 0.8.0 - Station block natives
- **New (Lua only, no C export):** `MSCoreNative_add_block(station, classPath, gx, gy, h, rot)` (returns `ok, err`, AddBlockInput, type id `30`), `MSCoreNative_remove_block(...)` (same args, RemoveBlockInput, type id `31`), `MSCoreNative_pending(station, apply, reset, primaryInvId, playerInvId, editingId)` (PendingBlueprintInput, type id `33`; `apply` and `reset` are `0` or `1`, exactly one of them set; `apply` needs both inventory ids > 0), `MSCoreNative_read_block_class(classPath)` (returns `true, text` or `false, err`). Wrappers: `MSCore.factory_native.add_block`, `remove_block`, `pending`, `read_block_class`. `read_block_class` is parsed to `{station, ship, machines, min, max, cost}`.
- **How:** the commands go to the station as receiver. These three have no AttachTargetID field (BPProbe 2026-11-06). `remove_block` removes the first pending entry that matches class, X, Y, Height and Rotation (the block ID is not compared). Class lookup is type-filtered (UClass) and `Default__` objects are refused.
- **Flow (Blueprint Unit Station Build):** one apply: `pending(reset)`, `remove_block` for each block in the way, `add_block` for each JSON block, then `pending(apply)`. The game nets the costs: removes refund, adds charge, Primary (the unit inventory first).
- **read_station:** `B` lines have an optional 7th column with the full class path. The 6-column form still parses (`path` is `nil`).
- **BlockBounds (probe 2026-11-08):** the bounds from `read_block_class` are raw, divide by `65536`. They are 4x the grid unit horizontally and 2x vertically (Station_01 spans 16, but its blocks sit 4 apart). The game keeps each box slightly under its size, so inclusive overlap checks do not touch.
- **Fixed:** `ui.clearClicks` / `ui.onClick`. The hook removal is deferred 100 ms and a reopen cancels it (generation counter). Invalid buttons are skipped. This was the dev menu crash when the inventory panel was reopened.
- **Verified in game (2026-11-08):** a 1x1 station placed where a Shipyard side stood, through Blueprint Unit (the side was removed first).
- **Unverified in game:** the rest of the station build flow and the 0.8.0 `read_station` / `read_block_class` text. The dev menu crash fix is not confirmed yet. Lua tests: `mods\MSCore\tests\test_factory.lua` (`parse_block_class`, and the add_block / pending / remove_block / read_block_class wrappers with fake natives). The Lua tests cannot call the DLL.
- **Known limits:**
  - `pending` refuses `apply` with an inventory id of `0` or less. Never send `0` to Pending.
  - Blocks only. Machines are not part of this flow.
- Native API is 5 (`MSCore_api()` returns `5`, the new natives are Lua only). Lua API stays 3. Module version `0.8.0`.

## 0.7.6 - Local inventory split, camera block, Dock_2 floor
- **New:** `MSCore_inv_split(inv_id, src_slot, amount, dst_slot, err, errlen)` (C export) and `MSCoreNative_inv_split(invId, src, amount, dst)` (Lua, returns `true` or `false, err`), `MSCore.native.inv_split`, `MSCore.factory_native.inv_split`. Moves `amount` items from slot `src` to the EMPTY slot `dst` of the same Inventory. Slots are 0-based. `amount` must be at most the stack cap (`MaxStackSize`, or 1 when `MaxStackSize` is 1, times the inventory multiplier). If the destination refuses, the items go back to the source.
- **How:** the game's `RemoveAtSlot` and `AddAtSlot` are called by RVA (0x421c730, 0x4216060). The 40 entry bytes of each are compared first; any difference turns the feature off (`native inv_split` error text). The source and destination are re-read after each call (the remove return value is not trusted).
- **Not a game command.** It edits the inventory locally, so co-op clients do not see it. Co-op desync is accepted for this version.
- **wbp:** while a WBP window is open the camera look is blocked (controller AllowCharacterInput(false) / CharacterInputEnabled); close restores the previous value. Window code: mscore_wbp.lua.
- **factory:** Dock_2 floor is 30x30; a footprint across touching floors validates against the union of the padded floors.
- **Fixed:** binary config bytes with NUL were truncated to the first byte; machine configs/recipes/ports now reach the game.
- **Unverified in game:** not run in game yet. Lua tests: ``mods\MSCore\tests\test_factory.lua`` (inv_split native missing / present). No Lua test covers the NUL fix (Lua tests cannot call the DLL); `arg_bytes` checked by code read only.
- **Removed:** Ctrl+F9 steer test hotkey (dev only).
- **Known limits:**
  - `inv_split` is a LOCAL edit: co-op desync is accepted for single-player restore. Do not use it in multiplayer.
  - Stale CIE holder: a WBP window destroyed without close (death/load) is dropped from the holder list only on the next open/close. If that controller differs from the one now in use, its CharacterInputEnabled is not restored by this code (fix not in 0.7.6).
  - Dock_2 floor override lives in `scripts/gen_factory_tables.py` (user-verified, kept).
  - The RemoveAtSlot/AddAtSlot entry-byte check (RVA compare) runs inside the SEH guard: a read fault there is caught and the feature stays off.
  - The stack multiplier is read raw at Inventory+0xB0 and only range-checked (1 to 10,000,000). It is correct on the verified build; a different exe would need a new check.
  - `validate_place` padding union intentionally accepts small gaps between padded floors.
- Native API stays 4, Lua API stays 3.

## 0.7.5 - Blueprint Unit refund / charge redirect
- **New (default ON):** when a station holds a Blueprint Unit container (`BP_BPUnitStoreObj_01_C`), deconstruction refunds go into the unit first and overflow into the player inventory (then the game's drop object), and machine build costs are checked against and taken from unit + normal inventory (unit first). Done with two inline detours (machine refund `DisassembleSelf`, build-charge inventory helper) and the game's own `InventoryGroup` object (created once, rooted, members overwritten per use). Fail closed: exe size 149200896, unique patterns, 4 charge call sites, byte-checked constructor; any doubt in a lookup calls the original unchanged. No group available: refund falls back to the unit inventory alone, charge is not redirected.
- **Switches:** off with the file `Mods/MSCore/refund_redirect_off.txt` or `MSCoreNative_redirect_enable(false)`. Probe log lines stay off by default (`refund_probe.txt` or `MSCoreNative_refund_enable(true)`). New natives: `MSCoreNative_redirect_enable(bool)`, `_redirect_status()`, `_redirect_stats()`. Log lines: `REDIRECT refund ...`, `REDIRECT charge ...` (first 20, then every 25th), `COMPAT refund ...`.
- **New:** `inv_move` takes an optional receiver (the player object id; Lua 5th argument, C export `MSCore_inv_move_to`). `MSCore_inv_move` still sends receiver 0, which the game silently drops (it never moved anything).
- **New:** native `read_station` and `read_inventory` (stacked from 0.7.2 and 0.7.3, see below).
- **Changed:** the 0.7.4 probe hooks now replace the functions and call the original through a trampoline (they were pre-hooks).
- **Known limitation: co-op.** The decision uses sim state only, but the game is lockstep: a client without the hook would put refund/charge in different inventories and desync. Every player needs the same MSCore version with the redirect on, and the pak with `BP_BPUnitStoreObj_01_C` is mandatory. Client_ValidateDeterminism behaviour is unknown. Designer (blueprint) and teardown paths are untested.
- **Verified in game.** Refunds and build charges were redirected to the Blueprint Unit, removing 1700+ machines worked, and full-save remove-all + rebuild worked. The fake-function harness (`scripts/test-refund.ps1`) and the build also pass.

## 0.7.4 - Refund probe (log only)
- **New:** `MSCoreNative_refund_enable(bool)`, `_status()`, `_log()` (also `Mods/MSCore/refund_probe.txt` switches it on at start). Installs two inline detours (machine refund `DisassembleSelf`, build-charge inventory helper) that only LOG what a refund / charge redirect would do, then run the original unchanged. Default OFF. Fail closed: exe size 149200896, unique patterns, exactly 4 charge call sites, else nothing is installed and `COMPAT refund ...` is logged.
- **Not yet run in game.**

## 0.7.3 - Native inventory read
- **New:** `MSCore.native.read_inventory(invId)` and `MSCore.factory_native.read_inventory(invId)`: reads `ItemSlots` of one Inventory in C++ (no Lua struct access). Returns `slots` = `{item, amount, max}` per slot, empty slots included (`item` `""` = empty, `max` = `Item.MaxStackSize`). `MSCore.inv.slots` uses it when present and falls back to the Lua read otherwise. Lua API 3 and native API 4 unchanged.
- **Not yet run in game.**

## 0.7.2 - Native station read
- **New:** `MSCore.native.read_station(stationId)` and `MSCore.factory_native.read_station(stationId)`: reads `Blueprint.Blocks` and `Blueprint.Machines` of one station in C++ (no Lua struct access, which crashes UE4SS). Lua API 3 and native API 4 unchanged.
- **Not yet run in game.**

## 0.7.1 - Lua globals for every mod
- **Fixed:** the `MSCoreNative_*` Lua globals (`MSCore.native`) were only registered for the MSCore Lua mod. They are now registered in every Lua mod's state, so other mods (e.g. BlueprintUnit) can use cmd.send and the factory natives.
- **Not yet run in game:** untested until a retest with a non-MSCore Lua mod.

## 0.7.0 - Factory API
- **New:** Lua API 3: `MSCore.blocks` (`register`, `status`, `debug_dump`). Adds a block to the building registry at runtime, so mods need no `DAT_BuildingBlockRegistry` pak. Research unlock append is opt-in (`MSCore.blocks.allow_research = true`, every player needs the mod). Failed writes are sticky (no retry). Untested in game. Module version stays 0.7.0.
- **New:** native API 4 (`MSCore_api()` returns 4): typed command send (`MSCore_send_typed`) and factory senders: `find_buildable`, `build_place`, `disassemble`, `set_recipe`, `port_toggle`, `inv_move`. Lua: `cmd.send_typed`, `MSCore.factory_native`.
- **New:** Lua modules (API 2): `MSCore.factory` (footprints, placement check, machine config bytes, recipes; `factory.native` = the native senders), `MSCore.inv` (inventory slots, free space, stack moves), `MSCore.wbp` (UMG window helpers, geometry log).
- **API numbers:** Lua API (`MSCore.api`, checked by `MSCore.require(n)`) is `3`. Native API (`MSCore_api()`) is `4`. They are separate counters: a Lua bump does not change the native number and the reverse. Module version `0.7.0`.
- **Docs:** UI window lessons, pak patching guide (`mod-authors/patching-paks.md`), MSCore.wbp API.
- Not yet run in game: the factory senders, the new Lua modules and the API 4 exports. Lua syntax checked only.
- **Fixed:** scan cache: a served row's actor and ore pointers are checked by id (a reused address is dropped); cached age is capped at 5000 ms (native and Lua default), so rows are at most 5 s old.
- **Fixed:** Lua scan fallback matches the native rows: type filter also matches `oname` and ore names, ship `order_dock` / `order` / `order_idx` filled, `piloted` and solar system id read as native.
- **Fixed:** chunk hold: a cube chunk skipped in the player's zone is added on a later hold / release call once the player has left the zone (was a permanent hole).
- Known limits are listed in `lua-api/scan.md`.

## 0.6.0 - Scan and chunk hold
- **New:** world scan of ships, resource asteroids and stations (`MSCore.scan`, native `MSCore_scan`, Lua `MSCoreNative_scan`). Cached per kind.
- **New:** chunk hold: keep the 3x3x3 chunk cube around a point loaded for an owner (`MSCore.chunks.hold` / `release`, native `MSCore_chunk_hold` / `MSCore_chunk_release` / `MSCore_chunk_ready`).
- **New:** selfcheck checks `scan` and `chunks`, and `scan.api`.
- Native API 3 (was 2). Lua API still 1. Scan and chunk hold are not verified in game yet.

## 0.5.1
- **Fixed:** update checks could show an old version because GitHub caches release files. Checks now always fetch the current manifest.
- **Removed:** `force_install`. Nothing is downloaded or installed unless you click Update / Add / Install. The game only checks and logs "update available".
- Status lines from the in-game loader no longer have a "force" field.
- Manager: Force install column removed.

## 0.5.0 - Mod loader
- **New:** MSCore Manager, a desktop app that finds the game, installs UE4SS + MSCore, and installs / updates / rolls back / removes mods.
- **New:** Add mod from `owner/repo` or any GitHub link.
- **New:** Remove mod removes everything the mod installed (files, UI, paks) and restores originals it replaced.
- **New:** in-game update check at start (log + `MSCore.loader.status()` / `updates()`).
- **New:** SHA-256 + size verification, zip path/type checks, one backup version for rollback.
- Public releases on GitHub (binaries only).

## 0.4.0 - Orders, warp and docks
- Shared native API v2: orders, autopilot, skip-to, filters, warp teleport, arrival and jump cost, sector list, map pin, dock and item lookup.
- Lua API: `orders`, `filters`, `warp`, `docks`, `find_item`.

## 0.3.0 - Ships, commands, steering
- `ships`, `cmd` and `steer` modules (list ships, send commands, aim and throttle).

## 0.2.0 / 0.1.0
- Core Lua helpers: safe object access, UI helpers, tick events, game name resolver, self-check log.
