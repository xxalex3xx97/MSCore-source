---
title: Changelog
nav_order: 6
---

# Changelog

## 0.9.0 - UE4SS 4.0.0
- **Changed:** MSCore and the manager now target UE4SS 4.0.0. The release ships `ue4ss-4.0.0.zip`; the manager installs it.
- **New:** `min_ue4ss` gate: the manager and the loader refuse to run on an older UE4SS, with a message that says which version is needed.
- **Changed:** build and release scripts default to the v4 tree (guarded). `scripts/setup-ue4ss-v4.ps1` builds it.

## Unreleased - chunk stats (API 28)
- **New:** C export `MSCore_chunk_stats(int* chunks, int* owners)`: distinct held chunks and owners with a cube. Container sizes only, no game reads. Native API 27 -> 28.
- **Changed:** chunk hold cap removed (`CH_MAX_CHUNKS` 1,000,000; real limit = chunks the game can load).
- **Changed:** `MsStationName.pad` flag bit0 = player-station class, bit1 = valid.

## Unreleased - floaters (API 27)
- **New:** floaters module, on unless `Mods\MSCore\floaters_off.txt` exists: C export `MSCore_floaters` (floating ore per sector, `MsFloaterSec` 40 B: id, sys, count, ch). Selfcheck `ships.floaters`. One-time `PROBE floaters` log line (totals, class histogram, item crate count). Native API 27. Unverified in game.

## Unreleased - enemy stations (API 26)
- **New:** estations module, on unless `Mods\MSCore\estations_off.txt` exists: C export `MSCore_enemy_stations` (enemy stations in the ship's system, nearest first, `MsEnemyStation` 64 B: id, sys, hull, max_hull, flags, build_pct, d_m, rel_m, radius_m). Radius is not read yet (flag bit2 clear). Native API 25 -> 26, kill switch module_disable("estations"), selfcheck ships.estations, PROBE estation / estation_collider lines. Game names are unverified (static RE, never run live).

## Unreleased - ballistics (API 25)
- **New:** ballistics module, on unless `Mods\MSCore\ballistics_off.txt` exists: C exports `MSCore_ship_guns` (aim gun group: bullet speed per frame, lifetime, range, shot period, group size) and `MSCore_sim_rate` (measured sim ticks per second, median of the cannon `LastShotTime` rate). Native API 24 -> 25, kill switch module_disable("ballistics"), selfcheck ships.ballistics, PROBE bullet_gun / bullet_speed / sim_rate lines.
- **Changed:** `MSCore_enemy_velocity` and `MSCore_enemy_motion` use the measured sim rate (fallback 22.4) instead of 60 (speeds were about 2.7x too high).

## Unreleased - dock kind (API 19)
- **New:** C export MSCore_dock_kind(dock), Native API 18 -> 19, kill switch module_disable("stations"), selfcheck stations.dock_kind.

## 0.8.15 - Holograms, item icons, fleets, repair, ship hull
- **New:** combat module, OFF by default (turn on with the file `Mods\MSCore\combat_on.txt`): C exports `MSCore_enemies_near` (enemy ships around a ship, nearest first, `MsEnemy` 48 B), `MSCore_ship_fire` (ShipFiringInput on / off, sent on change only) and `MSCore_ship_weapon_range` (stub, `-1`). Game names are unverified (static RE); fails closed. Caps row `combat`, selfcheck `world.combat`, kill switch `module_disable("combat")`. Class contract `enemy_ship`. Native API 17 -> 18 (`MSCore_api()` returns `18`). See the API 18 section of native-api.md.
- **Fixed (game bug workaround):** opening the design editor (ship or station, even in a new save) crashed the game with EXCEPTION_ACCESS_VIOLATION reading 0x110. Cause: the game's native `Designer::IsBlockAvailable(TSubclassOf<BuildingBlock>)` casts the class and reads its default object with no null check, and the Designer widget passes a null class. New module `designer_guard` (cpp `msc_designer.hpp`, Lua `mscore_designer.lua`): at start MSCore patches the function IN MEMORY (the exe on disk is never changed) so a null class returns `false` (block not available) instead of crashing. Found by a unique 35 byte prologue pattern; fail closed (pattern not unique / bytes differ / no memory = nothing patched, `COMPAT designer_guard failed: <why>`). Log `COMPAT designer_guard on target=0x...` and `DESIGNER GUARD null block class (n=<count>)` when it fired. Switches: file `Mods/MSCore/designer_guard_off.txt` (checked at start), `MSCore.module_disable('designer_guard')` or `MSCoreNative_designer_guard(false)` (original bytes back). `MSCoreNative_designer_guard_status()` -> `<status> blocked=<n>`. Caps row `designer_guard`. Patch safety: only the first 8 bytes of the function are written, in one interlocked 8-byte compare-exchange (a thread already inside the function keeps valid code; if the 8 bytes straddle a cache line all other threads are suspended for the write, nothing is written if that fails); the restore is the same single store. The live bytes must be exactly the two expected instructions as well as the 35 byte pattern. The guard cannot protect the `je`-taken path of the function if a future build changes it. The kill switch reports the truth: a failed restore keeps the status `restore failed: ...` (guard still on, Lua `designer.disable` does not claim off); after a successful `designer_guard(false)`, `designer_guard(true)` re-verifies everything and patches again. Installed once per process, the kill-switch file is read once; unload only restores the bytes. Not verified in game yet. Remove once the game fixes the bug.
- **Fixed (review):** Remove and Disable of a mod that is already disabled work without an install list (the disabled copy is found, its save_scrub declarations are read, mods.txt is cleaned). Disable/Enable that fails half way puts the files already moved back and names the files that could not move; a mod with files in both places shows as "(partly enabled)" and Enable completes it. File moves never delete the destination first (same volume: one replace; other volume: copy to a temp name, replace, then delete the source). Update refuses a disabled mod ("enable it first") and a disabled dependency is never installed again. The mod folder scan no longer throws on a folder that vanishes. The manager self-update reads only 2 bytes for the MZ check. The save scrub reports progress in the status line while it runs (it already ran off the UI thread).
- **Note:** the self-update SHA-256 detects a corrupted or truncated download only, not a malicious release (it comes from the same release as the exe); "Auto-update manager" can be turned off. The built-in `save_scrub` table is deprecated: new mod releases must declare `save_scrub=` in their `mscore.mod`. Remove restores nothing from `backup\`, so a mod must not overwrite files it does not own.
- **New:** the manager updates itself. A background check at start compares `manager_version` of the latest manifest with its own version; a newer one is downloaded, verified (`manager_sha256`, `manager_size`, MZ header), swapped in (running exe renamed to `.old`, deleted by the new instance) and restarted; every failure rolls back and keeps the running exe. Silent offline. Never while a mod operation runs. Checkbox "Auto-update manager" (default on, `%APPDATA%\MSCoreManager\manager.ini`), `--self-update` (headless, no restart), `--no-self-update`. `scripts
elease.ps1` writes the new manifest keys `manager_version`, `manager_sha256`, `manager_size` and refuses a build whose exe reports another version. A manifest without the keys means no self-update.
- **Fixed:** the save scrub missed object nodes that follow a child-bearing node (their block kept the neutralised class index: a "station block" on the ship, and a null block class in the Designer, crash 0x110). Nodes are now recognised by their own payload (`[idx][id][0]` ... `[0][6][parent]`), not by the words before them. Fail closed: after the rewrite the scrub looks for any remaining plausible reference to the neutralised class slots and leaves the save untouched, listing the offsets, when one is found. Run `--restore-saves <backup folder>` and `--scrub-saves <mod>` again for saves scrubbed by an earlier build.
- **Fixed:** Remove no longer moves saved designs to `DesignsBackup` (the ship lost its design and the game crashed with 0x110 when editing it). Designs are rewritten in place by a same-length class path substitution after a backup in `save-backup\<time>\Designs\`; a design that cannot be rewritten that way is left alone and reported. New `--scrub-designs <mod>` (also brings designs back that an older Remove moved) and `--restore-saves <save-backup folder>` (originals back after a fresh backup of the current files). The manager window has a scrolling details box for the full result.
- **Fixed:** the game crashed ("Failed to load class") after Remove mod, because the saves still held the removed mod's block classes. Remove now scrubs the saves: every `*_S.sav` that holds a class declared with `save_scrub=<modded>><stock>` in the mod's `mscore.mod` is first backed up (with its `_M.sav`) to `Mods\MSCore\save-backup\<time>\`, then its objects, class-table slot, statistics counts and per-ship path strings are switched to the stock class; the result is verified by re-reading it and a save that cannot be rewritten safely is left untouched and listed as refused. Designs that use the class are moved to `SaveGames\DesignsBackup\`. Disable does not touch saves. MiningBenchMod and BlueprintUnit have a built-in declaration until their own `mscore.mod` carries the lines. New headless `--scrub-saves <mod> [--dry-run] [--saves <dir>]`. See mod-authors/save-scrub.md.
- **Fixed:** MSCore Manager "Remove mod" left files behind (MiningBenchMod: Mods\MiningUnitUI, pakchunk98, enabled.txt stayed, the F4 window still opened). Cause: Remove put the `backup\<mod>\` copies (the previous version of those files, saved by an update) back over the deleted ones. Remove now restores nothing from backup, deletes the mod's disabled copy, drops empty mod folders and the mods.txt lines of every Mods\<dir> the mod shipped.
- **Fixed:** Disable now really stops the game loading the mod: all files of the install list (Mods\ and Paks\, so main.dll and the paks too) are moved to `Mods\MSCore\disabled\<mod>\` (same relative layout), Enable moves them back. The row shows "(disabled)" and stays listed. UE4SS only starts DLLs/scripts of folders that exist and the engine only mounts paks in Paks\; the MSCore native loader never loads other mods' DLLs.
- **New:** the manager shows its version: window title and a label "MSCore Manager v<version>  native API <n>  build <date>"; `MSCoreManager.exe --version` prints the same. Version and API number come from `mscl::kVersion` / `mscl::kNativeApi` in msc_loader.hpp, which the DLL (`MSCore_api()`, Lua version) also uses.
- **New:** shared rock registry service: C exports `MSCore_rockreg_feed / state / get / box / seg / nearest_n / nearest_surface / area_ready / report` (MsRegRock 48 B, MsRegShip 64 B, MsRegState 40 B). One table of every resource asteroid seeded from the save objects (nearest first), disk cache `rockreg\RockRegCache.bin` (format v2), one delete listener, radius read lazily, self-check against the loaded actors. Every mod feeds it about every 0.25 s (owner 1 = mining, 2 = Fleet) and MSCore merges the feeds. Caps row `rockreg` (depends on `rocks`), kill switch `module_disable("rockreg")`, selfcheck `world.rockreg`. Native API 15 -> 16 (`MSCore_api()` returns `16`). Lua API stays 3. The old per-mod `MiningRegistryCache.bin` files are orphaned and safe to delete. See the API 16 section of native-api.md.
- **Fixed:** MSCore.fleets.capture(true): hook now reads AddInputCommand(receiver, cmd) correctly and dumps every FleetTagInput property (dev probe, sends nothing).
- **Fixed:** stations names: DisplayName read failed on StationObject, now tries DisplayName, Blueprint.DisplayName, StationName, CustomName (first non-empty); selfcheck world.stations logs a one-shot probe and the winning source. C API unchanged.
- **New:** MSCore.wbp.open_fleets_window() (the game's own fleet window, ShowWidget(WBP_Fleet, PlayerObject)), open_fleet_assign(shipId) (add-to-fleet popup, enabled: WidgetSubsystem:ShowPopup(WBP_TagSelectPopup_C, shipObject, nil, false); logs "[MSCore.wbp] open_fleet_assign ship=.. class=.. ok=.."), fleet_assign_caps() ("ok" or "off:<why>"), probe_fleet_popup() (dev capture, logs the ShowPopup widget class name, sends nothing). Contract widgets fleet_window / fleet_tag_popup, selfcheck wbp.fleet_open. Lua API stays 3.
- **New:** fleets module: C exports MSCore_fleet_tags / MSCore_fleet_ships (MsFleetTag 200 B, MsFleetShip 48 B), Lua natives fleets_read / fleet_cmd / fleets_probe, Lua mscore_fleets.lua (MSCore.fleets.list / of_ship, write wrappers), caps fleets and fleets_write (off: unverified), kill switch module_disable("fleets"), selfcheck world.fleets / world.fleets_write / fleets.list. Native API 12 -> 13. Lua API stays 3.
- **New:** C export `MSCore_stations(MsStationName*, int)` (id + DisplayName of every live station), caps `stations`, kill switch `module_disable("stations")`, selfcheck `world.stations`. Native API 11 -> 12. C only.
- **New:** MSCore_ship_repair / MSCore_ship_repair_need (game's RepairFrameInputCommand, bCreative = free), caps repair, kill switch module_disable('repair'), selfcheck world.repair. Native API 10 -> 11. C only (no Lua wrapper, like the flight exports).
- **New:** mscore_wbp alone (`require "mscore_wbp"`, no mscore.lua): ship_hologram (primary, returns the texture; hologram_texture alias) + set_ship_hologram wrapper, item_icon + set_item_icon wrapper, texture lifetime rule, pictures need opts.host (widgets parented to the consumer's hidden panel so the GC keeps them), hologram_release(host) on close and at open; created once per ship per window (refresh OFF, C.holo_refresh=false: re-rendering an existing widget crashed the game; 2 renders per 0.25 s with FIFO queue, hologram_stats; items: missing key retried every 30 s), caps hologram, kill switch module_disable("hologram"), selfcheck wbp.hologram.
- **New:** mscore_wbp item_list() ({key,name} sorted, cached from the item map, no extra FindAllOf), selfcheck items.list. Lua API stays 3.
- **New:** mscore_wbp item_icon / set_item_icon (MSCore.items.* = aliases), caps items_icon, kill switch module_disable("items"), selfcheck items.icon. Lua API stays 3.
- **New:** C export `MSCore_ship_hull(ship, cur, max)` (GetCurrentHull / GetTotalHull, whole units, bitmask 1 cur ok / 2 max ok, -2 module off, -1 exception). Kill switch `module_disable("ships")`. Native API 13 -> 14 (`MSCore_api()` returns `14`). Lua API stays 3.
- **New:** C export `MSCore_ship_mining_caps(ship)` (bitmask 1 MiningLaser, 2 ItemScooper, 0 no mining weapon, -1 unknown, -2 module off). Weapon names read in `cpp\MSCore\msc_weapons.hpp`; kill switch `module_disable("ships")`; selfcheck `ships.mining_caps`. Native API 14 -> 15 (`MSCore_api()` returns `15`). Lua API stays 3.
- **New:** C export `MSCore_presence_step(bad_n, listed, read, bad_out)`: pure debounce (a ship read absent 3 times in a row is dropped; never-listed ships rejected at once; any read other than 0 or 1, e.g. -1, holds). Header `cpp\MSCore\msc_presence.hpp`, native unit test `tests\test_presence.cpp` run by `scripts\test-native.ps1` (from `scripts\check.ps1`).
- **Fixed:** scan `piloted` flag: a ship counts as piloted when its PilotID matches the controller id or the PlayerObject id. Logs the pilot tuple once on change (`SCAN pilot`).
- **Fixed:** `items` never caches an empty item map; the next call reads it again.
- **Fixed:** wbp fleets window counts as open only when it is in the viewport and visible (a cached hidden `WBP_Fleet_C` no longer counts).
- **Fixed:** wbp hologram queue: queue purge, items stall guard, pruning of dead entities, nil guards. No in-place re-render without refresh_ok (refresh stays OFF).
- **Changed:** native fleet unassign sends `bRemove=true` (`false` is ADD, per the 2026-11-20 log). Verified in game 2026-11-21: the pill trash removes a fleet from a ship. Other fleet write ops (create, rename, delete, assign, reparent) stay unverified and off (`fleet_write` refuses them; `fleets_write` cap off by default).
- **Changed:** selftest `world.fleets_write` arms the cap only for the verified factory `CreateInputFleetTag` (FText, int and bool params). Any other FleetTagInput factory keeps `fleets_write` off.
- **Fixed:** fleets scan skips a ship whose `FleetTagIDs` layout is unexpected (counted) and fails (module off) only when no ship could be read. `MSCore.module_disable("fleets")` also switches off native `fleets_write` (new narrow native `MSCoreNative_module_disable`, names `fleets` / `fleets_write` only).
- **Known issues:**
  - `open_fleet_assign` (mscore_wbp.lua): in standalone mode the `holo.hold - now <= 3` bypass is always true, so the 3 s settle hold never blocks the + (add to fleet) click. Left unchanged: standalone mode has no warp/load event, so a bypass limited to "this session start" cannot be told apart from a warp without the live game. The + flow is confirmed working in game; the warp guard stays as before. Follow-up: a warp/load signal for standalone mode.
  - Native fleets export: only the first 8 fleet tag ids per ship are exported (`MsFleetShip.tags[8]`). A ship in 9+ fleets shows 8 ids; `total` has the real count.
  - Native exports that walk game objects (`ship_repair_need`, `ship_hull`, `fleet_tags`, `fleet_ships`, `stations`) must be called from the game thread. SEH only catches access violations.
  - A hologram widget dropped after its ship disappears stays parented to the host panel until `hologram_release`. Memory grows in very long sessions.
  - The hologram render queue can hold a ship behind an abandoned queued ship for up to 10 s.
  - `MSCore_stations` does a full scan per call (no cache). Count call + fill call = two scans.
  - `MSCore.caps().fleets` reports `ok` from the Lua side only. A native fleets module off is not reflected yet.
  - The `piloted` flag accepts a PilotID equal to the controller id or the player object id.
- **Changed:** `MSCore.fleets.list(true)` bypasses the cache. `open_fleet_assign` scans ships on its first click.
- **New:** `MSCore.fleets.on_fleet_cmd` (a function you set; nil clears it): called with `(FleetTagID, bRemove)` whenever the game sends a FleetTagInput, even with the log muted. ID 0 = create fleet.
- **New:** mscore_wbp `close_fleets_window()` closes the game's fleet window again after the add-to-fleet popup; `fleets_window_open()` reports whether it is open.
- **Docs:** hologram ownership contract and test checklist (`docs/mod-authors/lua-api/ui.md`). Native API 11 to 14 sections and the quick reference updated to API 14.
- **Dev:** aimed tests in `mods/MSCore/tests/` for fleets, fleet open, hologram and item icon. The class contract test passes: `C.classes.player_obj` (`DMSPlayerObject`) has its mirror `kClsPlayerObj` in `cpp/MSCore/msc_contract.hpp` (`cls_name("player_obj")`).

## 0.8.14 - Game update 2026-11-20 + pause query
- **New:** C export `MSCore_world_paused()` (1 paused, 0 running, -1 unknown, -2 module off; UGameplayStatics::IsGamePaused by reflection, no patterns), caps `world`, selfcheck `world.paused`. API stays 10.
- **Fixed:** selfcheck no longer expects `MSCore.native.rename_ship` (it is a Lua wrapper over `rename_dock`); new check `factory_native.rename_ship` instead. Game build 149329408 is now the verified build (Lua `verified_build` and comments).

## 0.8.13 - Flight module
- **New:** C exports `MSCore_ship_flight(sid, mask, MsFlight*)` (one read of the ship state: actor frame, chunk / Location / Quat, Throttle and steps, bAutopilot / bDockingAutopilot / bTravelling, collision radius, max speeds, power grid, low-power flag and limits), `MSCore_ship_set_throttle`, `MSCore_ship_zero` (Throttle 0 and RotationImpulseInput 0), `MSCore_ship_console` (PilotConsoleObject id) and `MSCore_actor_loc` (K2_GetActorLocation). caps flight=ok|off:why, kill switch `MSCore.module_disable("flight")`, selfcheck `world.flight`.
- Native API 9 -> 10. Lua API stays 3.
- **Manager:** `requires=` dependencies are installed automatically, the warning is gone (PR #35).

## 0.8.12 - Ship shields
- **New:** C export `MSCore_ship_shields(ship, cur, max)`: current and max shield of a ship in whole units, read from the Shields / MaxShields PNum (GetShields / GetMaxShields when missing). Returns bitmask 1 = cur ok, 2 = max ok. caps ships=ok|off:why, kill switch `MSCore.module_disable("ships")`, selfcheck `world.ships`.
- **New:** class `ship` (DMSPlayerShipObject) in `MSCore.class("ship")` and `MSCore_class`.
- Native API 8 -> 9. Lua API stays 3 (additions only).
- **New:** factory_native.rename_ship(shipId, text): renames a ship (same command as rename_dock, ship id as receiver). Lua-only wrapper.

## 0.8.11 - Hook crash fix
- **Fixed:** a crash when a mod window and the dev menu were opened in turn. The UMG layout hook is now registered once and kept (it returns at once when no button is listed) instead of being removed and registered again on every open.
- Includes everything in 0.8.10 (class names and rock scan in MSCore; native API 8, Lua API 3).

## 0.8.10 - Class names, rocks and blocks by logical name
- **New:** `MSCore.class(name)` and C export `MSCore_class(logical, out, n)` return the game class name for asteroid, asteroid_actor, floating_ore, station, player_station, container, container_actor, item_crate. The names live only in mscore_contract.lua and msc_contract.hpp.
- **New:** `MSCore.rocks(sys, f)` and C export `MSCore_scan_rocks(sys, loaded_only, out, max)`: asteroid rows of one solar system. caps rocks=ok|off:why. Kill switch `MSCore.module_disable("scan")`.
- **Changed:** mscore_blocks takes its registry, research and category names from the contract. caps blocks=ok|off:why, kill switch `MSCore.module_disable("blocks")`, selfcheck blocks.contract, world.rocks, rocks.caps.
- Lua API stays 3 (additions only). Native API 7 -> 8. Mods feature-detect `MSCore.class` / `MSCore_class`.

## 0.8.9 - Widgets by logical name
- **New:** mscore_wbp resolves game widgets by logical name (inventory_grid, item_amount, fleet_ship_entry, pause_menu) from one contract table (mscore_contract.lua), then by name. caps widgets=ok|off:why, kill switch MSCore.module_disable("widgets"), COMPAT CAPS line, selfcheck widgets.contract.
- Lua API stays 3 (additions only). Native API 7. Mods feature-detect wbp.widget.

## 0.8.8 - Dock filters
- **New:** Lua native `MSCore.factory_native.set_dock_filter(receiverId, index, itemName, bLoading)` sets a dock item filter: it spawns a `StationConsoleInput`, calls its `SetFilter(index, Item, bLoading)` and sends it to the dock, the way the game's own dock window does. Item name with or without the `item_` prefix, empty name clears the slot.
- No C export and no API change: native API 7, Lua API 3. Run in game through a Blueprint Unit paste.

## 0.8.7 - Chunk hold radius
- **New:** C export `MSCore_chunk_hold2(owner, sys, ch3, radius, err, errlen)`. Radius 1 = 3x3x3 chunks (same as `MSCore_chunk_hold`), radius 0 = centre chunk only. Other radius values fail with `radius must be 0 or 1`.
- No Lua API change. Native API 7, Lua API 3.

## 0.8.6 - Update-proof addresses
- **Changed:** the refund redirect and the inventory split no longer depend on fixed game addresses or an exe size check. The FindObject and group-constructor functions, the split functions, the group vtable and the player Inventory / creative-flag offsets are found at load time (patterns, call target, reflection by name). If one cannot be found the feature turns off and the log says which (`COMPAT ...`), instead of running on a wrong address.
- **New:** one `COMPAT CAPS` line in the log lists what resolved (chunk hold, refund; split and player offsets appear on first use).
- No API change. Native API 5, Lua API 3. Checked offline against game build 149299200 (all addresses unchanged); refund redirect run in game, split not yet.

## 0.8.5 - Game update compat
- **Compat:** game build 149299200 supported: the refund redirect's exe size check, function addresses and the player creative-flag offset follow the new game build, and `verified_build` in the Lua side is bumped to match. Without this the refund redirect refused to start on the new build ("COMPAT refund failed: exe size").
- No API change. Native API 5, Lua API 3. Not yet run in game beyond the Blueprint Unit build click.

## 0.8.4 - Quiet release install
- **Changed:** the `UE4SS-settings.ini` that ships with a release has the UE4SS console and GUI hidden (`ConsoleEnabled`, `GuiConsoleEnabled`, `GuiConsoleVisible` = 0). Dev installs keep their own file. The manager never overwrites an existing `UE4SS-settings.ini`, so players who already have one keep their console until they set those three lines to `0`.
- No API change. Native API 5, Lua API 3. Not yet run in game.

## 0.8.3 - Native docks with block ids
- **New:** native `read_docks(stationId)` (`MSCoreNative_read_docks`, `MSCore.factory_native.read_docks`): the Dock objects of one station as rows `{machine_id, block_id, parent_id, class, name, raw_id}`. Fields are found by property name in C++.
- **New:** `read_station` `B` lines have an 8th column, the block's BlockID (`-1` when unreadable). `parse_station` gives it as `bid` (`nil` on older lines).
- **New:** `read_station` `B` lines have a 9th column `mids`: the comma-separated MachineIDs of the block's child machines (`DefaultMachines`, fallback `DefaultMachineIDs`), empty when none. A dock block can read BlockID `-1`, so match docks through `mids`. `parse_station` gives `blocks[i].mids` (a list, `{}` when absent). Not verified in game yet.
- **New:** selfcheck `native.read_docks` (counts, first row, whether a live block bid equals a dock block_id, and how many blocks have a dock machine_id in `mids`).
- **New:** native `rename_dock(receiverId, text)` (`MSCoreNative_rename_dock`, `MSCore.factory_native.rename_dock`): `SetTextInputCommand.CreateInputSetText(Text, 0)` built in C++ (FText made natively, never through Lua), then `AddInputCommand(receiverId, cmd)`. Returns `true` or `false, err`. Name 1-200 characters, receiver > 0. Param buffer sized from the real offsets (text/config/ret) and the UFunction parms size; FText copied as its first 16 bytes (object ptr + ref), config written after; offsets out of range or overlapping return `CreateInputSetText param offsets out of range: psize=.. text=.. config=.. ret=.. sizeof(FText)=..`. The Lua form `CreateInputSetText` crashes the game (access violation in `FText::ToFString` while UE4SS pushes the null FText back to Lua; `pcall` cannot catch it). Needs the 0.8.3 build 3 DLL.
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