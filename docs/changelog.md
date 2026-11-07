---
title: Changelog
nav_order: 6
---

# Changelog

## 0.7.5 - Refund redirect
- **New:** refunds and build charges for the Blueprint Unit go to an InventoryGroup of [unit, player] (`msc_refund` detour). On by default; off switch `Mods\MSCore\refund_redirect_off.txt`. Verified in game.
- **Fixed:** `inv_move` (`MSCore_inv_move`, `factory_native.inv_move`) takes the player object's id as receiver. Receiver `0` was silently dropped by the game.
- **New:** native `read_inventory` and `read_station` (added across 0.7.1-0.7.4).

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
