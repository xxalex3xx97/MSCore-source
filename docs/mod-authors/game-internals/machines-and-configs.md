---
title: Machines, placement and configs
parent: Game internals
nav_order: 3
---

# Machines, placement and configs

Notes from reading the game and testing mods in it. Each claim is tagged:

- **OBSERVED**: read in the game's code, saves or probe logs.
- **INFERRED**: follows from what was read, not tested.
- **UNKNOWN**: not found yet.

Build these notes apply to: `MainSequence-Win64-Shipping.exe`, exe size `149200896`, Unreal Engine 5.7. Addresses and offsets into game objects are listed in the table at the end. They are build specific and change with every game update. Offsets inside the text are native C++ offsets. They are given where a mod would read or write them.

Inventories, refunds and the build charge are covered in [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}). They are not repeated here.

## 1. Number format and object basics

- OBSERVED: every numeric field on a machine (progress, efficiency, power, fuel) is a signed 16.16 fixed point `int32`. One is `0x10000`. Multiply is `(a * b) >> 16`. Divide is `(a << 16) / b`.
- OBSERVED: the global "zero" and "one" fixed values are variables set at startup, not file constants. Do not compare against the value stored in the exe file.
- OBSERVED: the class chain is `SaveableObject` > `DMSObjectBase` > `MachineObject`. Every machine has an object id (the machine id) and a refresh delegate that its window binds to.
- OBSERVED: to check that an object is still valid, test that the pointer is not null and that bit 30 of the UObject flags (at `+8`) is clear. Copy that test in native hooks.
- OBSERVED: an item stack is 16 bytes: item pointer, `int32` amount, padding. Recipe arrays are `TArray` of these.

## 2. Machine classes

### MachineObject (all machines)

| Field | Meaning |
|---|---|
| `OutputMachines` | Machines this one pushes items into directly. Saved as ids. |
| `Efficiency` | Fixed. **Written every step by the power grid**, so a write from a mod is overwritten. |
| `Buildable` | The Buildable asset the machine was built from. |
| `PowerGridID`, `PaletteID` | Saved. |
| `bPlayerDisassembly`, `bAcceptsPushItems` | Flags. |

- OBSERVED: UFunctions you can reach with ProcessEvent: `CanInteract`, `CanPlayerDisassemble`, `GetDisplayMaxPower`, `GetDisplayPower`, `GetFrame`, `GetPaletteID`, `GetPowerGrid`.
- OBSERVED: the building frame (`BuildingFrame`) owns one power grid, its machines list and its multiblock machines.

### Recipe machines (AssemblerObject)

- OBSERVED: **one native class** backs all crafting machines: Assembler, AdvancedAssembler, Refinery, EnrichmentChamber, GrowthChamber, Supercomputer, RelicSynthesizer and the Workstation variants. The `Crafter` data asset (`CrafterType`) decides which recipes a machine offers.
- OBSERVED: fields a mod may care about:

| Field | Meaning |
|---|---|
| `DisplayName` | Name shown in the UI. |
| `InputInventorySize`, `OutputInventorySize` | Slot counts. |
| `PollutionProduction` | Integer. |
| `PowerConsumptionRate` | Integer. Draw is `rate x 65536` only while working. Defaults live in each machine's class default object. The per-machine numbers are UNKNOWN. |
| `InputInventory`, `OutputInventory` and their ids | The ids are saved. |
| `CrafterType` | Crafter asset. |
| `Recipe`, `Progress`, `MaxProgress` | `MaxProgress = CraftingSeconds x 65536`. |
| `bWorking`, `bProgressComplete` | `bProgressComplete` means finished but the output is blocked. |
| `bManualMode` | Hand crafting. |
| `PortStates` | `TArray<bool>`, one entry per port. |

- OBSERVED: other machine classes seen: `ManualAssemblerObject` (hand fabricator on ships, no command handler), `WorkbenchObject` (Armoury and Workbench, takes `SetRecipeInput` and `SetConfigInput`), `ReactorObject`, `SolarPanelObject`, `ResearcherObject`, `ContainerObject`, `ConveyorBeltObject`, `SplitterObject`, `UndergroundBeltObject`, docks, drone bays, `TeleporterObject`, `WarpDriveObject`, `SignpostObject`, `ShieldGeneratorObject`, `TurretObject`, `ShipThruster`.
- OBSERVED: classes with **no** command handler: Researcher, ShieldGenerator, SolarPanel, UndergroundBelt, ManualAssembler, InventoryMachine, ItemCrate. Nothing can be sent to them after placement.

### Item and recipe assets

- OBSERVED: a `Recipe` data asset holds `Output` (one stack), `ExtraOutputs`, `Inputs`, `CraftingSeconds` (integer) and the `Crafter`.
- OBSERVED: an `Item` data asset has `bIsFuel` and an integer `FuelValue`. Reactors use these.
- INFERRED: `CraftingSeconds` counts simulation steps, because machine code adds exactly `Efficiency` per step and never reads a time delta. If one step is not one second, the label is scaled.
- UNKNOWN: simulation steps per real second.

## 3. How the world runs machines

- OBSERVED: machine updates are direct native calls from one world step. They are not UFunctions and not virtual calls, so ProcessEvent hooks do not see them.
- OBSERVED: the order inside one step is: power grids, splitters, conveyors, docks, manual assemblers, assemblers, containers, underground belts, reactors, researchers, elevators, then drone bays, miners, ships and the other lists, then three update lists.
- OBSERVED: solar panels have no per-step update. They only answer a power query.
- OBSERVED: a recipe machine works like this each step: push one finished item to the next output machine (round robin), then add `Efficiency` to `Progress`, then at `MaxProgress` put the outputs in the output inventory if **all** fit.
- OBSERVED: **inputs are consumed when a cycle starts**, outputs appear at the end.
- OBSERVED: only the primary output is checked for space when a cycle starts. Extra outputs are checked at the end. A full output inventory can therefore block a machine at the end of a cycle after its inputs were already consumed. A blocked machine draws no power and waits until the output inventory is emptied. There is no timer.
- OBSERVED: a machine accepts a pushed item only if it has a recipe, the item fits an input slot and there is space. Input slots are filtered to the recipe inputs when the recipe is set.
- OBSERVED: changing the recipe moves the whole input inventory to the inventory passed with the change. A null destination is allowed in the code.
- INFERRED: the code does not check space in that destination. A native call with a full destination can lose items.

### Power

- OBSERVED: there is one power grid per building frame (ship or station).
- OBSERVED: each step the grid sums consumer draw and producer output, then sets one efficiency for all consumers and one for all producers. Consumer efficiency is `production / consumption`, clamped to 0 to 1.
- OBSERVED: if consumers are less than 20 percent satisfied (`0x3333`), the grid blacks out. Consumer and producer efficiency are both set to 0. A grid with no production counts as low power.
- OBSERVED: consumers draw only while working. Idle and output-blocked machines do not load the grid.
- OBSERVED: a reactor burns `PowerGenRate x producerEfficiency` per step. With zero consumption the efficiency is 0, so a reactor burns **no fuel** while nothing draws. Fuel not dropping is not a bug.
- OBSERVED: a solar panel's output is `PowerGenRate x (1 + 0.1 x SolarBonus) x DistanceEfficiency`. Distance efficiency is computed once from the distance to the sun and cached.
- INFERRED: the grid runs first in the step, so a machine that starts working is seen by the grid one step later.
- INFERRED: the cached solar distance efficiency can be stale after moving to another star.

{: .warning }
Do not write `Efficiency` on a machine to change its speed. The grid overwrites it every step. Hook the grid update, or the power query, instead.

## 4. Footprints, grid and rotation

- OBSERVED: placement uses a grid transform: grid location (X, Y as integer cells), height and a grid rotation. See section 5.
- OBSERVED: a machine builder exposes `MachineDimensions` (its footprint). The code that uses it for validation was not read.
- OBSERVED: in saves, a machine record stores X and Y as 16.16 fixed values equal to grid cell divided by 2, then separately the integer grid X and Y.
- OBSERVED: an underground belt entrance and exit are the same class, each a 1x1 object.
- OBSERVED: in one old save, two underground entrances 14 units (7 cells) apart were separate tunnels with belts between them, not one tunnel.
- UNKNOWN: footprint sizes per machine class, which rotation value is which direction, and how multi-cell machines anchor to the grid location.
- UNKNOWN: what the machine class check does (see "Validation" in section 5).

## 5. Placement

### The build command

- OBSERVED: a hand build ends in the player controller, which queues a `BuildInput` (command type id `0x2e`) with receiver `0`. The sim applies it on its next step.
- OBSERVED: queueing costs nothing. The charge happens when the sim applies the command.
- OBSERVED: the callable UFunction is `DMSPlayerController:AddBuildCommand(BuildCommand)`. It builds a `BuildInput`, sets the type id and receiver, and queues it. Anything sent this way is co-op safe, because it is the same path as hand building.
- OBSERVED: these UFunctions do nothing in this build (they parse arguments and return): `AddBuildMachineCommand`, `AddBuildShipMachineCommand`, `AddBuildMinerCommand`, `AddBuildAttachedStationCommand`, `AddBuildDecorationCommand`, `AddBuildNewStationCommand`.
- OBSERVED: `AddInputCommand(Receiver, Command)` sets the receiver and queues the command. A `BuildInput` sent with a station id as receiver goes to the station object, which has no build handler, and is **silently dropped**. The receiver of a build must be `0`.
- OBSERVED: `AddDisassembleCommand(TargetID, bMass)` queues a `DisassembleInput` (type `0x2f`, receiver `0`).

### BuildInput fields

| Field | Notes |
|---|---|
| `Buildable` | A **Buildable data asset** (`DAT_*`), not a machine class. A class passed here does not work. Capture it from a hand build or from the builder. |
| `Location`, `Quat`, `Chunk`, `SolarSystemID` | Stay at their defaults for a normal build: zeros, identity quaternion (`W = 65536`), 0. The game itself sends `SolarSystemID` 0. |
| `AttachTargetID` | Object id of the thing the preview was aimed at (the floor the preview belongs to). |
| `GridTransform` | Grid location X, Y, height, grid rotation. |
| `Config` | Integer. Stored on the new machine only. A hand build leaves it 0. No reader was found. Leave it 0. |
| `MachineConfigData` | Byte array. Exactly the machine's `SerializedMachineConfig` bytes. See section 6. |

- OBSERVED: `BuildInput` is `0xa0` bytes. `BuildCommand` (the struct passed to `AddBuildCommand`) is `0x68` bytes. A `BuildInput` made by hand (NewObject) needs the type id `0x2e` at `+0x28` written by hand. `AddBuildCommand` does that for you.
- OBSERVED: the Buildable asset has a switch `bUseBlueprint`. When true the build takes the blueprint (free-form) path. When false it takes the machine path described here. It also has `bUseItemCost` and an item cost list. The machine class comes from the Buildable's soft class property.

### What the sim does with it

1. OBSERVED: for a machine, the attach target is looked up by `AttachTargetID`. If that is not an object, the sim tries `SolarSystemID` as a solar system.
2. OBSERVED: the machine class default object runs a **placement validation** call. A non-zero result drops the command silently.
3. OBSERVED: the build cost is checked and charged (see the other page). Not enough items means the command is skipped silently.
4. OBSERVED: the machine object is spawned and its fields are set: Buildable, location, solar system, attach target, grid transform, `Config`, the config bytes and the owner (the sender). Then it is initialized and registered.
5. OBSERVED: if `MachineConfigData` is not empty and the object is a machine, the machine's apply-config function runs with those bytes. Then the bytes are stored in the building frame's blueprint.

- OBSERVED: **insufficient items or blocked placement give no build and no message.**
- OBSERVED: the game does not validate floor bounds. A placement command does not check floors, parity or overlap (also stated on the inventories page).
- UNKNOWN: what the machine placement validation checks. Grid occupancy, bounds and power are candidates, none confirmed.
- UNKNOWN: what a value of `AttachTargetID` is for floors and platforms. Candidates are a station id, a platform id or a frame id.

{: .warning }
A mod that places machines must check floor bounds, parity and overlap itself. If it does not, the game places machines outside floors or on top of each other.

### Disassembly

- OBSERVED: `DisassembleInput` has `TargetId` (`int32`) and `bMass` (byte). The sim looks up the target and the sending player, then runs the machine's disassembly and refund steps. The refund rules are on the [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}) page.
- OBSERVED: upgrading an existing machine uses `BuildUpgradeInput` (type `0x2d`: upgrade Buildable and machine ids). It keeps the machine's config by serializing it and applying it again.

## 6. Config bytes (`SerializedMachineConfig`)

### Where config goes

- OBSERVED: every machine has two virtual functions for config: **serialize** (machine to byte array) and **apply** (byte array to machine). The default for both does nothing.
- OBSERVED: `BuildInput.MachineConfigData` is passed to apply at placement. It configures the machine in the same step it is built. No second command is needed for the classes in the table below.
- OBSERVED: serialize runs after a config command changes a machine, and the result is stored in the building frame's blueprint.
- INFERRED: that is why station snapshots show an **empty** config for machines that were never reconfigured. The stored config is refreshed only by a config command.
- OBSERVED: classes whose config is always 0 bytes: Container, conveyor belts, Signpost, Elevator, Reactor, Solar panel, Turret.

### Format rules

- OBSERVED: little endian. A string (`FString`) is an `int32` length that includes the terminating zero, then the bytes. Length 0 is an empty string. A negative length means UTF-16.
- OBSERVED: item, recipe and resource strings are the data asset **name**, lowercase and without spaces: `titaniumplates`, `hydrogenfuel`, `silica`, `waterice`.
- OBSERVED: every config starts with a `u16` version `1`, so the bytes `01 00`.
- OBSERVED: every non-empty sample from one test save decoded with no bytes left over: 17 assemblers, 78 refineries, 6 enrichment chambers, 37 splitters, 24 or more docks, 4 drone bays.

### Layouts

| Class | Layout | Example (hex) |
|---|---|---|
| Assembler (8 ports), Refinery (4), EnrichmentChamber (12) | `u16 1`, string recipe, `i32 n`, `n x u8` port enabled | Refinery: `0100 0f000000 "titaniumplates"+NUL 04000000 01010101`. Assembler: `0100 0b000000 "hullplates"+NUL 08000000 0101010101010101`. A disabled port is `00`. |
| AdvancedAssembler (12), Growth, Supercomputer, RelicSynthesizer | INFERRED same as above. | none |
| Splitter (and BeltBalancer classes, INFERRED same) | `u16 1`, `i32 n`, `n x` string filter item (empty = none), `i32 A`, `i32 B`, `i32 n`, `n x u8` port enabled | `0100 03000000 00000000 00000000 00000000 ffffffff ffffffff 03000000 010101`: 3 empty filters, priorities -1 and -1, 3 ports on |
| StationBlockDock (2 filters), IndustrialDock (8) | `u16 1`, string dock name, `i32 n`, `n x {string item, i32 bLoading}` | `0100 0f000000 "Station Dock 1"+NUL 02000000` then two entries of 8 zero bytes. A set entry is `09000000 "waterice"+NUL 00000000`. |
| ShipyardBlockDock, TradeDock | Same as a dock with `n = 0`. | `0100 10000000 "Shipyard Dock 1"+NUL 00000000` (26 bytes) |
| MiningDroneBay | `u16 1`, string resource | `0100 07000000 "silica"+NUL` |
| Underground belt (all three tiers) | `u16 1`, `i32 Length`. See section 7. | none ever seen in a save |
| Container, belts, Signpost, Reactor, Solar, Elevator, Turret | none | always 0 bytes |

- OBSERVED: for splitters, `A` and `B` are the two priority integers. Both `-1` is the default.
- OBSERVED: the splitter's third `i32` group at the end (`n` then bytes) is the port enabled array.
- INFERRED: the last hex digit pair in a port array is one byte per port, in the order of the port components (see below).
- UNKNOWN: which of `A` and `B` is which priority in the UI.

### Port order

- OBSERVED: the port array is indexed by port component order: Assembler `Port0` to `Port7` (8), Refinery `Port0` to `Port3` (4), EnrichmentChamber `Port1` to `Port12` (12 entries, so array index is name index minus 1; INFERRED).
- OBSERVED: **verified order for refineries and assemblers: clockwise from the east side, the top-most port first.** This matches the array position.
- UNKNOWN: port order for the advanced assembler, enrichment chamber, growth chamber, relic synthesizer and supercomputer.
- UNKNOWN: how an exported (face, index) pair maps to an array position. One toggle test would settle it.

### Reading config back

- OBSERVED: a station's `Blueprint.Machines[].SerializedMachineConfig` can be read after placement to compare bytes, but it lags by about 20 to 30 seconds and is empty for machines that were never reconfigured.

## 7. Underground belts

- OBSERVED: entrance and exit are the same class and each is an independent 1x1 object. Class values: `ProgressPerLength` 15, `MaxLength` 10 on Mk2 (Mk1 8, Mk3 12), `MaxProgress` 90, and `Length` 0 by default.
- OBSERVED: the partner link is **not** stored in config. It is set at runtime when the conveyor segment is built. Entrance and exit must face each other in line within `MaxLength`.
- OBSERVED: an underground has no command handler. Nothing can be sent to it after placement.
- OBSERVED: the config bytes the game writes and reads are `01 00` plus `int32 Length` (from the serialize and apply code, no save sample).

### Length encoding

- OBSERVED: **Length is the number of cells from the entrance to the exit, both counted.** An entrance with Length 3 has its exit at entrance plus 2 cells along its rotation. (Verified against a live read for one tunnel. Other values were checked by consistency with saved belts.)
- OBSERVED: the save stores Length as a plain field in the **live machine record** of the world tree, not in the blueprint config. Stored before it is the total progress, `ProgressPerLength x Length` (45 for Length 3 on Mk2).
- OBSERVED: the ratio of total to Length is 15 for Mk2 class records and 30 for Mk1 class records. Read the Length field and not the total.
- OBSERVED: in a game-built save there is **only an entrance record**. The exit cell has no machine. The exit is virtual and Length gives it.
- OBSERVED: tunnels built by a mod had a second record for the exit, with the same Length. The exit record had an empty output list.
- OBSERVED: a tunnel built by a mod with an empty config still saved Length 2.
- OBSERVED: one old save (Mk1 class, 11 records, all entrances) had Lengths 4, 4, 5, 4, 6, 6, 3, 4, 4, 4, 4.
- INFERRED: an empty config at placement leaves Length at 0.
- INFERRED: the hand builder fills in `01 00` plus Length itself. Its code was not located.
- UNKNOWN: what the game writes for an underground **exit** config.
- UNKNOWN: how Length is restored on load. Apply-config sets it at build time. The world record is restored directly on load.
- Candidate to test: `MachineConfigData = 01 00` plus `int32 L` (1 to `MaxLength`) on the entrance, `Config = 0`, same rotation, in line. Exit empty, or the same bytes.

## 8. Config and machine commands

All of these are `InputCommand` subclasses. The base is `0x40` bytes: type id at `+0x28`, sender at `+0x30`, receiver at `+0x38`. The receiver applies the command through its command handler (vtable offset `0x2c8`, see the reference table). The handler for recipe machines is a different function from the grid and world code.

{: .warning }
Commands you create with `NewObject` need the type id at `+0x28` written by hand. Commands made by the game's factory functions (`CreateInput*`) already have it. A type id of 0 is dropped.

### Command type ids

Type id is decimal then hex. The ids that matter for machines:

| Id | Class |
|---|---|
| 12 (`0xc`) | InventoryInput |
| 13 (`0xd`) | LogisticFilterInput |
| 17 (`0x11`) | MachinePortInput |
| 18 (`0x12`) | InteractInput |
| 19 (`0x13`) | SetItemInput |
| 20 (`0x14`) | SetRecipeInput |
| 21 (`0x15`) | SetConfigInput |
| 22 (`0x16`) | SetWorkingInput |
| 23 (`0x17`) | AssemblerInput |
| 45 (`0x2d`) | BuildUpgradeInput |
| 46 (`0x2e`) | BuildInput |
| 47 (`0x2f`) | DisassembleInput |
| 57 (`0x39`) | SetTextInputCommand |
| 63 (`0x3f`) | StationConsoleInput |

- OBSERVED: the full registry has 65 entries, ids 1 to 65. Others seen: 1 NewShipOrderInput, 4 ClipboardInput, 10 CreativeContainerInput, 16 DisassembleShipInput, 26 BuildBlueprintInput, 33 PendingBlueprintInput, 37 MovementInput, 40 ViewInput, 44 ResearchInput, 50 WarpDriveInput, 65 DockingInput. See the table at the end for where the registry is.
- OBSERVED: 63 is `StationConsoleInput` and 64 is `UnlockTriggerObjective`. Earlier notes had these wrong.
- OBSERVED: `ElevatorInput`, `HUDMarkerInput` and `DroneTaskInput` are not in the registry. The factory sets their id internally, or they are not network commands.

### Fields

| Command (id) | Fields (offset) | Receiver and use |
|---|---|---|
| `SetRecipeInput` (20) | `Recipe` (object) `0x40`, `Amount` `0x48`, `Config` `0x4c`. Factories `CreateInputSetRecipe(Recipe)` and `CreateInputSetRecipeStack(Recipe, Amount, Config)`. | Machine id of an Assembler, Refinery or EnrichmentChamber. Workbench and Armoury use the stack form (INFERRED: Amount is the count, Config the slot). `Recipe` is an `REC_*` asset. |
| `MachinePortInput` (17) | `PortIndex` `0x40` | Machine id. Toggles port `PortIndex` on or off. Handled by recipe machines and splitters. |
| `SetConfigInput` (21) | `Config` `0x40`, `Config2` `0x44`, `PlayerObjectID` `0x48`. Factories `CreateInputSetConfig(c, player)` and `CreateInputSetConfig2(c, c2, player)`. | Machine id. The meaning of the integers is per machine class (see below). |
| `SetItemInput` (19) | Item stack: item `0x40`, amount `0x48`; `Config` `0x50`. Factory `CreateInputSetItem(stack, config)`. | Splitter or balancer filter (Config is the slot, INFERRED), drone bay, mining drone bay resource, warp drive fuel. |
| `InventoryInput` (12) | `PrimaryInventoryID` `0x40`, `SecondaryInventoryID` `0x44`, `SlotIndex` `0x48`, `Action` (byte) `0x4c` | Applied by the **player object**. See the inventories page for the actions. |
| `LogisticFilterInput` (13) | `Index` `0x40`, item stack `0x48`, `Action` (byte enum) `0x58`, `bRemove` `0x59`. Factory `CreateInputLogisticFilter(index, stack, action, remove)`. | Container filters (container handler) and ship filters. A container ignores `MachineConfigData`, so filters are only set this way. |
| `SetTextInputCommand` (57) | `Text` (FText) `0x40`, `Config` `0x50`. Factory `CreateInputSetText(Text, Config)`. | Signpost text, elevator floor name, station and dock rename. `Config` is the text index (the sign window's `SetIndex`). |
| `SetWorkingInput` (22) | `bNewWorking` `0x40` | MiningDroneBay on or off. |
| `StationConsoleInput` (63) | `SetAutoCollect` `0x40`, `FilterIndex` `0x44`, `FilterItem` `0x48`, `bLoading` `0x50`. | Dock filters. Same data as the dock config item and `bLoading`. |
| `AssemblerInput` (23) | `Recipe` `0x40`, `SetMode` `0x48`, `bCancel` `0x4c`, `Amount` `0x50` | Manual crafting on a ship assembler. **Not** factory config. |
| `DisassembleInput` (47) | `TargetId` `0x40`, `bMass` `0x44` | Receiver `0`. |
| `BuildUpgradeInput` (45) | `UpgradeBuildable` `0x40`, `MachineIDs` `0x48` | Receiver `0`. |
| `BuildInput` (46) | See section 5. | Receiver `0`. |

- OBSERVED: `SetRecipeInput` handling on a recipe machine also fires a tutorial stat if the recipe is the machine's tutorial recipe. That is internal.
- OBSERVED: `AssemblerInput` uses mode 1 to enter manual mode and 2 to leave it.
- OBSERVED: `ClipboardInput` (4) is for ship autopilot order copy and paste. It is not a machine config paste.

### Which classes handle which commands

| Class | Commands |
|---|---|
| Assembler (all recipe machines) | AssemblerInput, SetRecipeInput, MachinePortInput |
| Reactor | SetConfigInput. A non-zero `Config` sets auto fuel. |
| Workbench | SetRecipeInput, SetConfigInput |
| WarpDrive, Teleporter | SetItemInput, SetRecipeInput, SetConfigInput |
| Splitter, Signpost | SetItemInput, MachinePortInput, SetConfigInput |
| PilotConsole | ViewInput, SetConfigInput, SetItemInput, MachinePortInput |
| Container | LogisticFilterInput, CreativeContainerInput, InteractInput, SetConfigInput |
| ConveyorBelt | InteractInput, SetConfigInput, DroneTaskInput |
| ItemElevator, StellarCollector | SetConfigInput |
| Vendor | SetConfigInput, InteractInput |
| Door, Ladder, Lift, Elevator, DroneBay, MiningDroneBay | SetConfigInput (and SetWorkingInput, SetItemInput, ViewInput, DroneTaskInput, ElevatorInput for some) |
| Researcher, ShieldGenerator, SolarPanel, UndergroundBelt, ManualAssembler, InventoryMachine, ItemCrate | none |

- OBSERVED: for splitters, `SetConfigInput.Config2` carries the priority change.
- UNKNOWN: the meaning of `Config` for classes other than the Reactor.
- INFERRED: container contents have no config path. In creative worlds they come from `CreativeContainerInput`. Otherwise they come from inventory moves. Not tested.

### Fallbacks when config bytes are not enough

- INFERRED: a recipe can be set after placement with `SetRecipeInput` sent to the machine id. A disabled port can be set with `MachinePortInput`. Both are second choices. One `MachineConfigData` at placement does the same in one step.
- OBSERVED: to send a command to a machine you just placed, you need its machine id. The placement result does not return it. A mod has to find the new machine (match class and grid location) before sending.

## 9. Co-op and gotchas

- OBSERVED: every peer simulates every machine and only commands travel. Change machine state through commands. A direct write that a peer does not make desyncs the session (the game has a checksum report for this).
- OBSERVED: ProcessEvent hooks cannot see the production loop. It is native to native. Hooks on the native functions need the exe size check below.
- OBSERVED: the world loops hold a raw array pointer and a cached end. Destroying or adding a machine from inside a hooked update corrupts the loop. Defer it to your own end-of-step. The game has a destroy-at-end-of-update list for this.
- OBSERVED: sim object classes are saved by class path. A mod that adds machine classes must keep them mounted, or the save will not load.
- OBSERVED: `Recipe` can be null or flagged as garbage. Check bit 30 at `+8` before reading it.
- OBSERVED: `Recipe.CraftingSeconds` and the item stack amounts can be changed in memory, since recipes are data assets. Do it identically on every peer. `PowerConsumptionRate` and the reactor's `PowerGenRate` are plain integer properties on the sim object, with the same rule.
- INFERRED: a new crafting machine can reuse the assembler class by cloning an existing machine's class default object and its Blueprint actor. The Crafter asset decides its recipe list.
- OBSERVED: do not call `FindFirstOf` from chained delayed callbacks. It crashes.
- INFERRED: after placing an underground, read back its Length property to confirm it took the value you sent.

## Build-specific reference

Addresses are virtual addresses. Subtract `0x140000000` to get the RVA.

| Item | Value |
|---|---|
| `DMSPlayerController` AddBuildCommand native (hook for capture; BuildCommand pointer in `rdx`, config bytes at `+0x50`) | `0x144260760` |
| AddInputCommand native | `0x1442609d0` |
| AddDisassembleCommand native | `0x1442608c0` |
| ConfirmBuild native | `0x144261450` |
| MachineBuilder ConfirmBuild | `0x14423ede0` |
| ConveyorBuilder ConfirmBuild | `0x14423e100` |
| StationBuilder ConfirmBuild | `0x14423f090` |
| Sim command processing | `0x144269b40` |
| Sim consumer per tick (disassemble queue, build commands, world step) | `0x144267910` |
| Machine spawn | `0x144265d10` |
| Machine register | `0x14423bb60` |
| Placement validation (class default object virtual, offset `+0x28`) | `0x1442680c6` call site |
| Config apply call site | `0x1442686a9` |
| Config store in frame blueprint | `0x14427b350` |
| `MachineObject::UpdateBlueprintConfig` | `0x144237fd1` |
| Command registry function | `0x144264b40` (65 getters at `0x14893a350`) |
| `BuildInput` class (size `0xa0`, ctor, vtable) | `0x1441ef770`, `0x14736afd0` |
| `BuildInput` fields | `Buildable 0x40`, `Location 0x48`, `Quat 0x54`, `Chunk 0x64`, `SolarSystemID 0x70`, `AttachTargetID 0x74`, `GridTransform 0x78`, `Config 0x88`, `MachineConfigData 0x90` |
| `BuildCommand` fields (104 bytes) | `Buildable 0`, `Location 8`, `Quat 20`, `ChunkLocation 36`, `SolarSystemID 48`, `AttachTargetID 52`, `GridTransform 56`, `Config 72`, `MachineConfigData 80`, `Sender 0x60` |
| `MachineBuilder` fields | `Buildable 0x50`, `bValid 0x78`, `bShipMachine 0x79`, attach id `0x7c`, grid transform `0x80`, `MachineDimensions 0xa0`, `MachineConfigData 0x130` |
| Machine object fields set at spawn | `Buildable 0xf8`, `Location 0x100`, `SolarSystemID 0x128`, `AttachTargetID 0x12c`, `GridTransform 0x130`, `Config 0x140`, config bytes `0x148`, owner `0x158` |
| `MachineObject` fields | `OutputMachines 0x158`, `Efficiency 0x178`, `Buildable 0x180`, `PowerGridID 0x19c`, `PaletteID 0x1a0`, flags `0x1a8` |
| `AssemblerObject` fields | `Recipe 0x1f8`, `Progress 0x200`, `MaxProgress 0x204`, `bWorking 0x208`, `bProgressComplete 0x209`, `bManualMode 0x218`, `PortStates 0x248`, `PowerConsumptionRate 0x1d8`, inventories `0x1c8`/`0x1d0` |
| `ReactorObject` fields | `PowerGenRate 0x1b0`, `RemainingFuelDouble 0x1e8` (int64, value x 65536), `bAutoInputFuel 0x1f1` |
| `UndergroundBeltObject` fields | `Length 0x1c4`, total progress `0x1c0`, partner pointer `0x1f0` |
| Splitter config ints | `A` at `+0x1f8`, `B` at `+0x1f4` |
| Machine vtable (byte offsets) | command handler `0x2c8`, init `0x350`, serialize config `0x3a8`, apply config `0x3b0`, TryPushItem `0x390`, GetPower `0x3b8`, IsConstantProduction `0x3c8` |
| Serialize / apply config overrides | Splitter `0x144234f20` / `0x144225060`, UndergroundBelt `0x1442351a0` / `0x144225350`, Dock `0x144234dd0` / `0x144224f20` |
| `SetRecipeInput` handler (Workbench) | `0x14422e4f0` |
| Splitter command handler | `0x14422e110` |
| Container command handler | `0x14422d2a0` |
| Signpost command handler | `0x14422e090` |
| MiningDroneBay command handler | `0x14422dc90` |
| Reactor command handler | `0x14422e030` |
| Assembler command handler | `0x1442a3d20` |
| Assembler update, TryStart, SetWorking, SetRecipe, TryPushItem, GetPower | `0x1442a2fd0`, `0x1442a4d40`, `0x1442a4bc0`, `0x1442a48b0`, `0x1442a4c90`, `0x1442a3cf0` |
| PowerGrid update | `0x144238120` |
| Reactor update, GetPower, Refuel, TryPush | `0x144231040`, `0x14422c710`, `0x144237710`, `0x144237b60` |
| Solar panel GetPower | `0x14422c790` |
| Underground link function (SetLinkTarget) / segment builder | `0x144235330` / `0x144271370` |
| Save serializer for machine records | `0x144208b00` |
| World step order | PowerGrids, Splitters, Conveyors, Docks, ManualAssemblers, Assemblers, Containers, UndergroundBelts, Reactors, Researchers, Elevators (world array offsets `+0xa8`, `+0x128`, `+0xe8`, `+0xb8`, `+0xf8`, `+0x108`, `+0x118`, `+0x138`, `+0x148`, `+0x158`, `+0x178`) |
| World step function | `0x144267910`; machine update functions are the entries above plus Splitter `0x144236a00`, Conveyor `0x144236df0`, Dock `0x14428de60`, Container `0x144224ad0`, UndergroundBelt `0x144237e70`, ManualAssembler `0x1442a6140`, Researcher `0x1442a6e30`, Elevator `0x144227740` |
| `DMSPlayerController` pending command list | `+0x768` (count `+0x770`) |
| Native fixed-point multiply helper | `0x144259410` |

## Unknown

- Footprint sizes per machine class, rotation direction values and how multi-cell machines anchor to the grid location.
- What the machine placement validation checks.
- What `AttachTargetID` is for floors and platforms.
- What the game writes for an underground's exit config, and how Length is restored on load.
- Port order for five machine classes (advanced assembler, enrichment chamber, growth chamber, relic synthesizer, supercomputer), and how an exported (face, index) pair maps to an array position.
- The meaning of `Config` and `Config2` on `SetConfigInput` for classes other than the Reactor.
- Which splitter priority integer is which.
- Simulation steps per real second.
- Per-machine `PowerConsumptionRate`, `PowerGenRate` and item `FuelValue` numbers.
- Where machines are registered into the world arrays and into the grid's producer and consumer lists.
- What the machine at the target `+0x199` check means in the output push loop.
- Whether a stack of container contents can be set by any command outside creative worlds.
