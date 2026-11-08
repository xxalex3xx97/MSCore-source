---
title: Glossary
nav_order: 5
---

# Glossary

| Term | Meaning |
|---|---|
| **UE4SS** | The mod framework the manager installs into the game: `dwmapi.dll` and the `ue4ss\` folder. It loads Lua mods and native DLLs from `ue4ss\Mods`. |
| **Mod loader** | The MSCore Manager plus UE4SS's `mods.txt` load list. The manager installs, updates and removes mods. UE4SS loads them when the game starts. |
| **mods.txt** | `ue4ss\Mods\mods.txt`. One line per mod, `<ModName> : 1`. MSCore's line goes first. |
| **enabled.txt** | An empty file in a mod folder. Its presence means the mod is loaded from `mods.txt`. Without it the manager asks you to add the line yourself. |
| **Manifest** | `manifest.txt`, attached to a GitHub release. Lists the zip name, `sha256`, `size` and version. The manager reads it for update checks. See [Publishing]({{ site.baseurl }}{% link mod-authors/publishing.md %}#manifesttxt). |
| **Latest release** | The GitHub release marked **Latest**. The manager reads `releases/latest`, so a release that is not marked Latest is not seen. |
| **Pak** | Unreal's packed content: `.pak`, `.utoc` and `.ucas` files. They go to `Windows\MainSequence\Content\Paks\`. |
| **Staged update** | An update the manager has downloaded and checked. It waits in `ue4ss\Mods\MSCore\updates\` and is applied at the next game start (`pending.txt`). |
| **Backup** | The old file kept before an update replaces it, in `ue4ss\Mods\MSCore\backup\<mod>\`. Rollback uses it. |
| **Rollback** | Restoring the backup files of the last update. Restart the game afterwards. |
| **Native DLL** | `MSCore.dll`, installed as `ue4ss\Mods\MSCore\dlls\main.dll`. It holds the native part and exports the `MSCore_*` C functions. |
| **MSCore Lua API v3** | The Lua API, a counter separate from the native API. `MSCore.api` is `3` (v2 added the `factory`, `inv`, `wbp` modules, v3 added `blocks`). Mods call `MSCore.require(3)` to say they need it. |
| **MSCore native API v5** | The native API. `MSCore_api()` returns `5` (v3 = scan and chunk hold, v4 = factory exports, v5 = station block natives, which are Lua only and add no C export). |
| **MSCore native API v2 (base)** | The native API as first shipped. `MSCore_api()` returned `2`. Added the orders, autopilot, skip-to, filters, warp, sector list, map pin, dock and item lookup exports (0.4.0). |
| **selfcheck.log** | `ue4ss\Mods\MSCore\selfcheck.log`. Written about 8 seconds after a world loads. Lines are `OK`, `BROKEN`, `SKIP` or `CHANGED`. |
| **Chunk** | An integer grid cell of the world (`ch`). One chunk is `2048` metres (`units.CHUNK_M`). |
| **Raw units** | Fixed-point values used for locations (`loc`, `arr`). `65536` raw units make one metre (`units.RAW_PER_M`). |
| **Dock id** | The id of a dock, found with `docks` or `MSCore_find_dock`. `-1` means none and `-2` means several matches (native). |
| **Verified build** | The game exe size MSCore was verified with (`MSCore.verified_build`). A different size gives a `CHANGED` line. |
| **Not verified in game** | Marker for code that was built and checked offline only. It does not mean the feature is broken. |
