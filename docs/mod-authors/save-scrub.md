---
title: Removing a mod - saves
parent: Mod authors
nav_order: 6
---

# Removing a mod: saves

The save keeps the class path of every block or machine your mod added. The game loads each saved class by path, and it stops with "Failed to load class" when your pak is missing. Autosave writes the path again every time your mod is loaded, so the game crashes after the player removes the mod.

MSCore Manager fixes the saves when the player presses **Remove mod** (also `MSCoreManager.exe --remove <mod>`). It needs to know which of your classes were cloned from which stock class.

## Declare your classes

Add one line per class to your `mscore.mod` (repeatable, old managers ignore it):

```
save_scrub=<your class path>><stock class path>
```

Paths are written exactly as the save's class table has them. Both lines below are the real declarations of the two mods released so far.

MiningBenchMod:

```
save_scrub=/Game/Blueprints/BuildingBlocks/BPO_MinerModuleBlock.BPO_MinerModuleBlock_C>/Game/Blueprints/BuildingBlocks/BPO_WorkstationBlock.BPO_WorkstationBlock_C
save_scrub=/Game/Blueprints/Machines/BPO_MiningMod.BPO_MiningMod_C>/Game/Blueprints/Machines/BPO_Workbench.BPO_Workbench_C
```

BlueprintUnit:

```
save_scrub=/Game/Blueprints/BuildingBlocks/BPO_BlueprintUnit.BPO_BlueprintUnit_C>/Game/Blueprints/BuildingBlocks/BPO_CommandModule.BPO_CommandModule_C
save_scrub=/Game/Blueprints/Machines/BP_BPUnitStoreObj_01.BP_BPUnitStoreObj_01_C>/Game/Blueprints/Machines/BP_ContainerObject_2.BP_ContainerObject_2_C
```

Rules for the stock class:

- Your class must have been cloned from it, so the saved data has the same layout. The block data is not changed, only the class.
- It must be a class the game ships, and the save must already use it somewhere (it is in the save's class table). Otherwise that save is refused and left as it is.
- Use the same path length for both paths (rename the clone with the same number of characters). A path that is stored in a per-ship or per-station table with another length is refused.

The manager still carries a built-in table for MiningBenchMod and BlueprintUnit, only as a fallback for releases that were published before they had these lines. **It is deprecated and will be removed.** Every new release of a mod that adds block or machine classes must declare its `save_scrub=` lines in its own `mscore.mod`; other mods get no fallback.

Files a mod does not own: Remove deletes every file in the mod's install list and restores nothing from `backup\`. A mod must therefore never overwrite a file it does not own (another mod's file, a vanilla file): that file would be deleted with the mod.

## What Remove does to the saves

Only when the mod is removed (Disable never touches saves). The game must be closed.

1. Every `*_S.sav` in `%LOCALAPPDATA%\MainSequence\Saved\SaveGames` (Autosaves too) that holds one of your class paths is copied, with its `_M.sav`, to `ue4ss\Mods\MSCore\save-backup\<yyyymmdd-hhmmss>\` together with a `RESTORE.txt`. The backup is never deleted.
2. Objects of your class become the stock class, the statistics counts are merged into the stock entry, and the unused class-table slot gets an unused vanilla class. The result is checked by re-reading it. If anything is doubtful the save is left untouched and listed as refused.
3. Saved designs (`SaveGames\Designs\*.sav`) that use your class are rewritten in place, never moved (a ship points at its design, and the game crashes when the file is gone). Only the class path strings change, and only when the stock path has the same length, so no length field of the file changes. The original goes to `save-backup\<time>\Designs\`. A design that cannot be rewritten that way is left alone and listed as refused.

## Choice of the stock class

The stock class must be the block the game itself uses for that job. For the Mining Unit that is `BPO_WorkstationBlock` (default machine `BPO_Workbench`): it is the donor the Mining Unit was cloned from and it is the ship workbench block of the stock game (present in the ship tables of saves that never saw the mod). After the scrub the ship shows the stock block with the donor's footprint, which looks different from your own mesh.

The class-table slot of your class gets an unused vanilla class so the table keeps its length and no path appears twice (the game never writes duplicate table entries). The scrub rewrites every place that names your class: objects, the table, statistics, per-ship tables. Numbers in the save that merely equal the table index are object ids, not class references.

## Other commands

`MSCoreManager.exe --scrub-designs <mod>` fixes only designs and also takes designs an older Remove moved to `DesignsBackup` back into `Designs`. `--restore-saves <save-backup folder>` puts the originals of an earlier scrub back (after a fresh backup of the files it replaces), so a scrub can be run again.

`MSCoreManager.exe --scrub-saves <mod> --dry-run` lists what would change without writing anything. `--saves <dir>` uses another save folder.
