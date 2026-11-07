---
title: Changelog
nav_order: 6
---

# Changelog

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
