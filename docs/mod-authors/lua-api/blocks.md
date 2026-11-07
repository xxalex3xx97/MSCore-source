---
title: Blocks and research
parent: Lua API
grand_parent: Mod authors
nav_order: 11
---

# Blocks and research

`blocks` adds a block to the game's building block list (the `BuildingBlockRegistry` asset) and, optionally, to a research unlock list, at runtime from Lua. No registry or `RSC_*` pak is needed. API 3.

{: .warning }
**Untested in game.** The module is unit-tested outside the game only (`mods/MSCore/tests/test_blocks.lua`). The probes P1-P4 below are not run yet. Unproven in game: the registry TArray append and the research append. Before relying on either, call `MSCore.blocks.debug_dump()` and do one trial append in a dev build.

{: .warning }
**Research append is opt-in and co-op unsafe until proven.** It edits the research data locally. If the simulation reads `Unlocks`, a player without the mod diverges from one with it. Every player in a session needs the mod (see the section below).

## Register a block

```lua
local MSCore = require("mscore")
local ok, err = MSCore.blocks.register({
    class = "/Game/Blueprints/BPO_MyBlock.BPO_MyBlock_C",   -- required: /Game/.../<Name>.<Name>_C
    research = "RSC_ComputationScience",                   -- optional: bare name or full object path. Only with allow_research (below)
    category = "Functional Blocks",                        -- optional, default "Functional Blocks"
})
-- ok == true, or nil and err (string)
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `class` | string | yes | Class path. Must match `^/Game/.+%.[%w_]+_C$`. |
| `research` | string | no | Research asset. A bare name `RSC_X` means `/Game/Data/Research/RSC_X.RSC_X`. A full object path also works. |
| `category` | string | no | Name of the registry category (`Name` as shown in the asset). Default `Functional Blocks`. |

- Registering the same `class` again returns `true` and changes nothing. The first call wins (including its `research` and `category`).
- `register` never throws. Bad input returns `nil, err`. It returns `nil, err` when `MSCore.events` is missing (install not run).

## Research opt-in

`research=` is ignored unless the mod sets this before it registers:

```lua
MSCore.blocks.allow_research = true   -- default false
```

Without it, the spec does the block part only and logs once: `research append disabled (set MSCore.blocks.allow_research=true; every player needs the mod)`. Status is `block`.

Set it only when every player in the session runs the mod and its pak. Co-op: a player without the mod does not get the research unlock, so the two can desync. Untested.

## Timing

- `install` only defines the API. The 250 ms tick starts on the first successful `register`. A mod that never registers costs nothing.
- Registration only stores the request. The work happens on the 250 ms tick, on the game thread.
- It runs every tick while a class is still missing. The main menu is fine: the appends wait until the class is loaded.
- After everything is appended, it re-checks every 5 s. If the game reloads the registry (for example after a level change), the block is appended again.
- **Failure is sticky.** A write that throws, or does not grow the array by exactly one element, is never retried. The part is marked dead and logs once. The block part and the research part are independent: a dead research part does not stop the block part, and the reverse.

## Status

```lua
for class, st in pairs(MSCore.blocks.status()) do print(class, st) end
```

| Status | Meaning |
|---|---|
| `pending` | Class or registry not loaded yet, or not appended yet. |
| `block` | Block is in the category. No research requested, or research not appended yet. |
| `block+research` | Block is in the category and the research unlock is in place. |
| `failed:<why>` | A step failed, for example `failed:no category`, `failed:append (count 1, expected 2)`, `failed:append threw (array may have grown)`, `failed:research enum unknown (need P2)`. Write failures are final (no retry). The block and research steps are independent: a research failure does not remove the block. |

## Research append (UNPROVEN, opt-in)

The research step copies the `UnlockType` value from an existing research unlock that already has a `Block` set, and appends `{UnlockType, bHidden = false, Block = <class>}` to the research asset's `Unlocks`.

Known risks, all untested:

- `UnlockType` is copied, not known. If no unlock with a `Block` exists in any research, the step fails with `need P2`.
- `ResearchState.UnlockedBlocks` may not recompute on a save where the research is already complete. Workaround to try: reload from the main menu, or complete the research after the mod is installed.
- Only the Designer and Codex lists are affected by the appends. Nothing else in the game is changed by the registry step.
- The research append is a local data edit. It is not replicated. See "Every player needs the mod" below.
- The `UnlockType` search gives up after 5 tries (one try per 12 research checks). It then logs once and stays off until the game reloads.

## Probes (not run yet)

| Probe | Question | Status |
|---|---|---|
| P1 | Does the block append show in the Designer and Codex lists? | untested |
| P2 | What are the `Unlocks` element fields and the `UnlockType` value (`MSCore.blocks.debug_dump()` in the console)? | untested |
| P3 | Does the research append unlock the block on an existing save (after reload or research completion)? | untested |
| P4 | Does a clean install work with no registry or `RSC_*` pak in the mod? | untested |

Probe script for P0-P2: `C:\Users\SFidelis\.claude\jobs\blocks_probe.lua` (paste into a temp Lua mod or the console).

## Every player needs the mod and the pak

Blocks appended at runtime exist only on a machine that runs the mod. Every player in a co-op session needs MSCore and the mod's pak, or the block is missing for them.

The research append is also local. If the simulation reads research unlocks, a player without the mod can desync. Untested: do not enable `allow_research` in a co-op release until this is checked in game with two machines.

## Verify in game first

The TArray append (`BuildingBlockRegistry` category `Classes`) is unproven: it may need a native push. Before relying on it, run `MSCore.blocks.debug_dump()` in a dev build and do one trial append. Check the block shows in the Designer.

## Migration

- Mining Unit and Blueprint Unit still ship their own registry and research overrides (chunk 100 registry). They are **not migrated yet**.
- After P1-P4 pass: remove their `DAT_BuildingBlockRegistry` and `RSC_*` entries from the pak and call `MSCore.blocks.register` from their Lua instead. Set `allow_research` only if the research step is needed and the co-op rule above is met.
- Until then, keep the legacy one-registry rule in [Patching paks]({{ site.baseurl }}{% link mod-authors/patching-paks.md %}).
