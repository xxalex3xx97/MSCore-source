---
title: Game internals
parent: Mod authors
nav_order: 6
has_children: true
---

# Game internals: what the reverse engineering found

These pages collect what was learned by reading the game's code and by testing mods inside it. They are for mod authors who need to know what the game really does before they send a command, place a machine, move an item or open a window.

Every claim is tagged:

- **OBSERVED**: read in the game's code or seen in a test.
- **INFERRED**: follows from what was read, not tested.
- **UNKNOWN**: not found yet.

The notes apply to one build: `MainSequence-Win64-Shipping.exe`, exe size `149200896`, Unreal Engine 5.7. Addresses and offsets appear only in the "Build-specific reference" table at the end of each page. They change with every game update, so check the build first. MSCore compares the exe size at start (see `verified_build`).

| Page | Read it when |
|---|---|
| [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}) | You move items, change inventories, remove machines, or place machines that cost items. |
| [World, network and simulation]({{ site.baseurl }}{% link mod-authors/game-internals/world-network-simulation.md %}) | You need the object model, lockstep, commands, receivers and co-op rules. |
| [Machines, placement and configs]({{ site.baseurl }}{% link mod-authors/game-internals/machines-and-configs.md %}) | You place machines, set recipes and ports, or read and write machine configs. |
| [Logistics, belts and docks]({{ site.baseurl }}{% link mod-authors/game-internals/logistics.md %}) | You work with belts, splitters, undergrounds, docks, or the simulation tick. |
| [Ships, AI orders and combat]({{ site.baseurl }}{% link mod-authors/game-internals/ships-ai-combat.md %}) | You read or command ships, orders, pilots, weapons or fleets. |
| [Navigation, warp and saves]({{ site.baseurl }}{% link mod-authors/game-internals/navigation-and-saves.md %}) | You move ships between sectors, hold chunks, or touch save data. |
| [UI, widgets, actors and custom blocks]({{ site.baseurl }}{% link mod-authors/game-internals/ui-widgets-and-blocks.md %}) | You build a window, hook a game window, or ship a custom station block. |

## Rules that apply everywhere

{: .warning }
The simulation runs in lockstep. Every change to game state must go through a game command, so that every player applies it. A direct write (`Inventory:AddItems`, a property set, a native call that edits sim state) runs only on your machine and desyncs a co-op session. Reading is always safe. Writing is safe only through commands.

{: .warning }
Lua struct reads are fragile. Reading a field that does not exist on a game struct throws inside UE4SS and can crash the game at random, even inside `pcall`. Read structs through fields you know exist, or read them in native code (MSCore `read_station`, `read_inventory`). More in each page's UE4SS section.

- Call `IsValid` before any method on a `UObject` that may have been freed.
- Do not use `TArray:ForEach` from Lua. Walk arrays with `GetArrayNum` and an index.
- Station data you read (`Blueprint.Machines`) lags 20 to 30 seconds behind a change you just made.
- When something here says UNKNOWN, treat it as a risk and test it in a copy of your save.
