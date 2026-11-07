---
title: Patching paks
parent: Mod authors
nav_order: 5
---

# Patching paks

How to ship a pak mod for Main Sequence Demo: a cooked package that adds or changes blocks, machines, windows or research. Everything here is proven in this repo unless marked **untested**.

{: .important }
**Game closed, back up first.** Tasklist the game yourself before you copy into the Paks folder. Keep a backup of the Paks folder. Use the same pak names when you replace a pak.

## Pak naming and chunks

A pak is an IoStore triple with one base name: `pakchunk<N>-<Name>_P.pak`, `.utoc` and `.ucas`. All three go in the Paks folder together. `N` decides the mount order: a higher chunk number wins when two paks hold the same path.

Chunk numbers in use:

| Chunk | Name | Holds |
|---|---|---|
| 95 | MSMiningMesh | Mining mesh |
| 97 | BPUnitMesh (and MSCube, dev) | Blueprint Unit table mesh and hidden mesh |
| 98 | MiningUnitUI | Mining Unit window and UI batch |
| 99 | ClonedBlock | Cloned block package set (Mining Unit blocks) |
| 100 | BlueprintUnit | Blueprint Unit block, registry and research |
| 101 | MSMiningModel | Mining model |
| 102 | BPUnitUI | **planned**, not built |

Pick a free number for a new pak. Do not reuse a number that another mod ships.

## Retoc pipeline

Tool: `retoc.exe` (repo copy under `F:\MSDemoMods\tools\retoc\`).

- Clone packages of **cooked vanilla assets** (legacy extract, 5.6-style headers):
  `retoc to-zen --version UE5_6 --no-parallel <dir> <out>.utoc`
- **Newly cooked assets** (from the UE 5.7.4 project): `--version UE5_7`.
- One container = one `--version`. Do not mix both in one `to-zen` run. Use two containers.
- Check the output: `retoc verify <out>.utoc`. It checks the container only. The game load is the real test.
- Extract vanilla or a pak to read it: `retoc to-legacy <Paks dir> <out dir>`.
- List: `retoc list --path --size UTOC`.

{: .note }
Mesh cooks disagree in the logs. The Blueprint Unit mesh (pak 97) was cooked with `-unversioned`. The working cube test (pak 97 and 100) packed clone packages with `UE5_6`. Check your own cook flags and let `retoc verify` plus an in-game load decide. Do not guess a flag.

## Cloning a vanilla uasset (same-length rename)

Cloning means copying a vanilla package under a new name without changing any byte count. Rules:

- Rename with **the same length** only: names, paths and class names in the header, export names and the `_C` and `Default__` forms.
- FName instance numbers: a name `BP_Container` with number 3 shows as `BP_Container_2`. To rename it, change the base only: `BP_Container` -> `BP_BPUnitSto` (same length), and `_2` then resolves to `BP_BPUnitSto_2`.
- A rename that is not the same length shifts every offset after it. Do not do it by hand.
- After a rename, stale name-hash bytes are accepted by the game (proven on the Mining Unit clone).

## The one-registry rule

`DAT_BuildingBlockRegistry` is **one asset**. The highest chunk that ships it wins, and that copy replaces the rest. So:

- The winning copy must contain **every** entry from every mod that uses the registry. The Blueprint Unit chunk-100 registry holds the Mining Unit entries and the Blueprint Unit entries.
- A mod that only changes the registry must merge with the existing entries first.
- Same-path registry or RSC edits in pak 99 lost to another pak 99. Use the higher chunk (100) to override (2026-11-07 test).

## Research patching (RSC_* Unlocks)

Research unlocks are arrays in `RSC_*` assets.

- Header offsets shift when the package name length changes. The unlock array offset is `B = 60 + u32(a, 52)`. Recompute it after you append names, imports or preload entries.
- Append new names, imports and preload entries; do not rewrite existing ones.
- An unlock element has the form `80 0d 1e 03 <ref>`.
- Reference script: `patch_research.py` in the Blueprint Unit pak folder.

## Default machines (container and friends)

- `DefaultMachines` is an **unversioned header property at index 24**. Its element is `80 09 0c <class ref> 80 07 06 X Y`: class reference, then a transform with X and Y.
- The **connector array is a different property** (`BPO_LargeConnector_C` entries). Appending a machine there crashes the game. The old crash came from exactly that.
- Patch: `patch_bpo_machine.py <DIR> <CLASS PKG> <CLASS NAME>`. It adds the index, the element, the names, imports and preload entry, and fixes the export size. Then it verifies the connector array moved correctly.
- Container variants (Blueprint Unit): `none` (no container), `vanilla` (vanilla `BP_ContainerObject_2`, 32 slots, try first), `clone` (120 slots, needs pak 97 with the hidden mesh as well).

## Cook gotchas

- **DirectoriesToAlwaysCook**: the cook skips folders it does not see as used. Pass them on the command line, for example `-ini DirectoriesToAlwaysCook /Game/Meshes/Machines` and `/Game/Materials`. You do not need to change the project ini.
- **Stand-in assets must match the class.** A stand-in at a vanilla path must be the same class as the vanilla asset (`MaterialInstanceConstant` vs `Material`). Match the class, or the reference does not load as the vanilla one.
- **Never ship custom materials.** The shader maps for new materials are not in the shipped game. Use vanilla materials only. A glowing stand-in like `M_BasicEmissive` is fine as a material **reference** in the cooked mesh, but do not pack the stand-in itself.
- Import meshes with Nanite off. Nanite in this game is untested.

## Dummy window class (suppress a vanilla window)

To stop a vanilla window from opening for your block, rename its import to a **same-length class that does not exist**. Example: `WBP_StationInventory` -> `WBP_BPUnitNoWindow00` in the block blueprint (ascii and utf16). The game then finds no class and opens nothing. The Mining Unit uses the same trick with `WBP_Workbench` -> `WBP_MiningUnt`.

## Mesh from the UE project (summary)

1. Model at real size: 1 uu = 1 cm, origin at the cell centre, Z up. A block cell is 800 x 800 uu.
2. Import into the UE 5.7.4 project with Nanite off. Use a stand-in material at the game path.
3. Cook, pack, and retarget the blueprint's mesh imports with a same-length rename.
4. The first proof was a cube block (2026-11-07): the block showed in the Designer's Functional Blocks list.

## Install

- Game closed. Check with tasklist yourself.
- Back up the Paks folder. The game folder is `Windows\MainSequence\Content\Paks` under the Steam install.
- Same pak names when you replace a pak. A new name adds a pak.
- Co-op: every player needs the same paks. A client without the pak cannot load the block class (untested as a fallback).
- Saves: a placed custom block keeps its class path. Do not rename a shipped class path. Removing the pak can break a save that holds the block (untested).
