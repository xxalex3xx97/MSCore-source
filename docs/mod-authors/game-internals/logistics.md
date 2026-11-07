---
title: Logistics, belts and docks
parent: Game internals
nav_order: 4
---

# Logistics, belts and docks

Notes from reading the game's code. The game was not run for most of this, so a lot is read from code and not tested. Each claim is tagged:

- **OBSERVED**: read in the game's code, its reflection data or its Blueprint dumps.
- **INFERRED**: follows from what was read, not tested.
- **UNKNOWN**: not found yet.

Build these notes apply to: `MainSequence-Win64-Shipping.exe`, exe size `149200896`, Unreal Engine 5.7. Addresses are listed only in the tables marked as build specific and in the table at the end. They change with every game update. Offsets written `+0xNN` are byte offsets into the simulation object.

For inventories, refunds and build charges see [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}). This page does not repeat them.

{: .warning }
Docks are only partly covered here. The notes show where the dock update runs and that docks push items like other machines. They do not show the dock filter fields or loading rules. See the Unknown list.

## 1. The object model

- OBSERVED: simulation objects are `UObject`s (`SaveableObject` > `DMSObjectBase` > `MachineObject` > ...). The simulation holds items as `Item*` pointers. There are no per-item objects.
- OBSERVED: every simulation object has an owning world (+0x28), an integer object id (+0x30), an `AttachTargetID` (+0x34, the frame or platform the machine sits on) and a pointer to its presentation actor (+0x70, a `BP_X_C`). The actor pointer can be null or stale.
- OBSERVED: the world object keeps the frame number, an id counter, an id to object map and the build and disassemble queues. The world id counter goes up by one on every spawn.
- OBSERVED: belts, splitters, undergrounds and balancers are all `MachineObject`s. Their links are stored twice: as pointer arrays (rebuilt on load) and as id arrays (saved).

### MachineObject fields that logistics uses

| Offset | Name | Notes |
|---|---|---|
| `0x100` | MachineDimensions | Box min and max in fixed-point grid ints. |
| `0x108` | GridTransform | 16 bytes: X, Y, Height, Rotation. |
| `0x118` / `0x120` | Inputs | Port definitions. Count at `+0x120`. |
| `0x128` / `0x130` | Outputs | Port definitions. Count at `+0x130`. |
| `0x138` | InputMachines | `MachineObject*` array of linked upstream machines. Count at `+0x140`. |
| `0x148` | InputMachineIDs | Saved ids. Pointers are rebuilt on load. Count at `+0x150`. |
| `0x158` | OutputMachines | `MachineObject*` array of linked downstream machines. Count at `+0x160`. |
| `0x168` | OutputMachineIDs | Saved ids. Count at `+0x170`. |
| `0x180` | Buildable | Data asset reference. |
| `0x198` | bPlayerDisassembly | Read by `MachineObject:CanPlayerDisassemble`. |
| `0x199` | bAcceptsPushItems | Checked on the **target** by every pusher before it hands over an item. |
| `0x19c` / `0x1a0` | PowerGridID / PaletteID | |

### Belts, splitters and undergrounds

| Class | Fields (all `+0x...` after the MachineObject fields) |
|---|---|
| `ConveyorBeltObject` | `0x1a8` MaxProgress (int, ticks an item needs to cross the belt). `0x1ac` bFirstUpdate. `0x1b0` NextUpdateIDs. `0x1c0` previous update id (not reflected). `0x1c4` bProgressComplete. `0x1c5` ConveyorRotation (byte 0 to 6, the belt shape). `0x1c8` Progress. `0x1d0` CurrentItem. `0x1d8` ItemTrack (0 to 2). `0x1e0` NextUpdate array, count at `+0x1e8`. `0x1f0` PreviousUpdate. `0x1f8` bActorDirty (not reflected). |
| `SplitterObject` | `0x1a8` MaxProgress. `0x1ac` MaxParallelItems. `0x1b0` PreviousOutputIndex. `0x1b8` Filters (one `Item*` per output, null means no filter), count at `+0x1c0`. `0x1c8` Items (`{Item*, int Progress}`, 16 bytes each), count at `+0x1d0`. `0x1d8` PortStates (one bool per output). `0x1e8` NextInputMachine. `0x1f0` LastInputIndex. `0x1f4` PriorityInputIndex. `0x1f8` PriorityOutputIndex. |
| `UndergroundBeltObject` | `0x1a8` ProgressPerLength. `0x1ac` MaxLength. `0x1b0` Items (`{Item*, int Progress}`), count at `+0x1b8`. `0x1c0` total progress (not reflected). `0x1c4` Length. |

- OBSERVED: a belt tile has 3 input slots (0 right, 1 straight, 2 left) and 1 output. A belt takes at most 3 upstream belts.
- OBSERVED: a belt tile holds **one** item.
- OBSERVED: the large conveyor `BP_LargeConveyorBeltObject` uses the same native class. Only `MachineDimensions` differ in the Blueprint.
- OBSERVED: the belt tier Blueprints `BP_ConveyorBeltObject` and `_3` are byte-identical in their default object. Belt speed (`MaxProgress`) is not set in the Blueprint.
- OBSERVED: `BeltBalancer_01` to `_03` and `Splitter`, `_2`, `_3` all use `SplitterObject`. They differ only in Blueprint defaults.
- OBSERVED: an underground belt is one machine at the entrance. See [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}) section 6 for its config bytes.
- OBSERVED: `ItemElevatorObject` is based on `InventoryMachine`. It registers through the generic system registry and not through a per-class world list. Its fields were not decoded.
- UNKNOWN: the numeric values of `MaxProgress`, `MaxParallelItems`, `ProgressPerLength` and `MaxLength` for each tier. Read them from a live object (for example `obj.MaxProgress` in UE4SS).

### Machine virtual slots that logistics uses

Slot `n` is at `vtable + 8n`. The first 88 slots are the `UObject` slots.

| Slot | What it does |
|---|---|
| 86 | `OnRegister(world)`. Called right after spawn and on load. Belts, splitters and undergrounds add themselves to a world update list here. |
| 87 | Post-load relink. Rebuilds the pointer arrays from the saved id arrays. |
| 88 | Destroy. Unregisters from the frame, destroys the actor and erases the id from the world map. |
| 89 | `ReceiveCommand`. For a belt this is the "player takes the item" command (section 3.6). |
| 105 to 108 | `LinkInput`, `LinkOutput`, `UnlinkInput`, `UnlinkOutput`. |
| 110 | `OnRemoveFromUpdateList`. |
| 114 | **`TryAcceptItem(item, source)`** returns bool. This is the push. |
| 115 | **`HasItemReady(requester)`** returns bool. This is the pull side. For a belt it is `bProgressComplete`. |
| 123 | `DisassembleSelf(player)`. Drops contained items, unlinks all neighbours, then destroys. |
| 125 | `GetContainedItems()`. A belt returns its `CurrentItem` once. |
| 128 | `CanPlayerDisassemble()`. Returns `bPlayerDisassembly`. |

{: .warning }
Check every pointer you read from a simulation array before you use it: not null, and not marked pending-kill. The game tests the flag in the object's flags field (bit 30). A call on a freed object is a native crash that `pcall` cannot catch.

## 2. The simulation tick

- OBSERVED: the simulation is single threaded, fixed step and in lockstep. A tick driver runs once per rendered frame. It adds the frame time to an accumulator and runs one simulation frame when the accumulator passes the step.
- OBSERVED: when more than 4 frames are queued, the step becomes `0.02222` s (1/45 s). This is catch-up mode. When more than 2 frames are queued and the driver is not in single mode, it runs a second simulation frame in the same tick.
- OBSERVED: nothing runs when the world is not running or is paused.
- UNKNOWN: the normal step value, so the number of simulation frames per second. Log the frame counter against wall time to measure it (see Unknown).

### One simulation frame

1. OBSERVED: if input bundles from every peer for this frame are not all here, the frame does not run. A stall counter goes up. This is the lockstep wait.
2. OBSERVED: each input bundle is deserialized and its commands are processed. The receiver is looked up by id and the command ends in the receiver's slot 89.
3. OBSERVED: the world update runs. All simulation happens here.
4. OBSERVED: afterwards the host computes the world checksum and reports it. A client replays the per-player history instead.

### World update order

OBSERVED: the world update runs these steps in order.

1. First-update initialization of newly registered objects.
2. The disassemble queue.
3. The build queue.
4. The update phases below.
5. The frame is pushed into the history ring and the frame counter goes up by one.

Update phases that matter for logistics, in execution order:

| Phase | What runs | Tag |
|---|---|---|
| 1 | Power grid update | INFERRED |
| 2 | **Splitters and balancers** | OBSERVED |
| 3 | **Conveyor chains**, started from each chain head | OBSERVED |
| 4 | **Docks** | OBSERVED |
| 6 | Assembler-type crafting, pushes outputs | OBSERVED |
| 7 | A pusher (checks `bAcceptsPushItems` on its target) | INFERRED class |
| 8 | **Undergrounds** | OBSERVED |
| 11 | People elevators | OBSERVED |
| 13 | A second pusher (checks `bAcceptsPushItems`) | INFERRED class |
| 14 | Miners | OBSERVED |
| 20 | Per-class system objects (item elevator, lift, workbench, researcher, shield generator, stellar collector, drone bay, weapon, missile) | OBSERVED mechanism, INFERRED mapping |
| 22 | One-shot deferred updates, then the list is cleared | OBSERVED |

Phases 5, 9, 10, 12, 15 to 19 and 21 exist. Their classes are not identified (12 is an empty stub).

- INFERRED: the order is fixed. Splitters (2) run before belts (3). Undergrounds (8) run after belts. Assemblers (6) are in between.
- INFERRED: an item can go from a splitter to a belt in the same frame. An item can go from a belt to an underground only on the next frame at the earliest.
- INFERRED: `DMSWorld:AddFixedUpdate` and `RemoveFixedUpdate` feed the deferred-update and system lists. Not traced.

## 3. How items flow

OBSERVED: movement is a **push**. The upstream object calls the downstream object's `TryAcceptItem(item, this)` (slot 114). The downstream object decides. Only the splitter also pulls, by asking its inputs `HasItemReady` (slot 115). The belt item actors are driven afterwards from simulation state.

### 3.1 Conveyor belts

OBSERVED: belts update **chain heads first**. A head is a belt with `bFirstUpdate` set, which means its output is not another belt in this update tree. The output of a head can be a machine, a splitter, an underground or nothing. Heads live in a world list. Each head steps itself, then the belts that feed it, recursively.

- OBSERVED: upstream belts therefore move after the belt they feed. The freed tile is refilled in the same frame. There are no gaps.

Belt step, once per belt per frame:

| Situation | What happens |
|---|---|
| `bProgressComplete` is set (item waiting at the end) | The belt calls `TryAcceptItem` on `OutputMachines[0]`. When it returns true, the belt clears its item and `bProgressComplete`. |
| Not complete and holding an item | `Progress` goes up by 1. At `MaxProgress - 1` or more, `bProgressComplete` is set. The actor is told the new progress. |
| `bActorDirty` is set | If the belt is now empty, the actor is told to clear its item. The dirty flag is cleared. |

Belt accept:

| Condition | Result |
|---|---|
| The belt already holds an item | Returns false. |
| Otherwise | Stores the item, sets `Progress` to 0, sets `ItemTrack` to the index of the source in `InputMachines` (only written when found), updates the actor, returns true. |

- INFERRED: an item that was just accepted is not stepped in the same call. Transit time of one tile is `MaxProgress - 1` frames after accept, plus one frame to hand over.
- INFERRED: throughput is one item per `MaxProgress` frames per belt path.

### 3.2 Belt links and shape

OBSERVED: link calls (slots 105 to 108) work like this.

- `LinkOutput(target, 0)`: fails when the output slot is out of range. When the target is a belt and this belt is a head and the target has no previous update, this belt stops being a head and is removed from the head list. The target records this belt in its `NextUpdate` and `NextUpdateIDs`. This belt records the target as its `PreviousUpdate`.
- `LinkInput(from, slot)`: needs 3 input slots. Stores the pointer and id, then recomputes the shape.
- `UnlinkOutput(target)`: clears the output. A belt that was not a head becomes a head again and is added back to the head list.
- `UnlinkInput(from)`: clears the entry, removes it from `NextUpdate` and `NextUpdateIDs`, recomputes the shape.

`ConveyorRotation` (the belt shape) comes from which of the three input slots are linked:

| Linked input slots | ConveyorRotation |
|---|---|
| none | 0 |
| `[0]` only | 1 |
| `[2]` only | 2 |
| `[0]` and `[1]` | 3 |
| `[1]` and `[2]` without `[0]` | 4 |
| `[0]` and `[2]` | 5 |
| `[0]`, `[1]` and `[2]` | 6 |

- INFERRED: when the target already has a previous update (a side merge), this belt stays a head. Side feeders then step in head-list order (registration order), not in topological order. Side-loading order can depend on build order.
- OBSERVED: `LinkOutput` has a loop guard of up to 10000 iterations over `PreviousUpdate`. It does not reject a cyclic link.
- INFERRED: a ring of belts with no head is never stepped.

### 3.3 Splitters and balancers

OBSERVED accept: when `NextInputMachine` is set and the source is a different machine, the splitter refuses. When it already holds `MaxParallelItems` items, it refuses. Otherwise it records the source in `LastInputIndex`, adds `{item, 0}` and returns true.

OBSERVED update, in this order:

1. **Choose the claimed input.** Start at `PriorityInputIndex`, then go round-robin from `LastInputIndex + 1`. Take the first input that answers `HasItemReady` with true. That input becomes `NextInputMachine`.
2. **Move each held item.** If its progress is below `MaxProgress`, add 1. Otherwise try the outputs. The priority output (`PriorityOutputIndex`) is tried first, when its port is enabled and the filter allows. Then the outputs go round-robin from `PreviousOutputIndex + 1`. On success the item leaves the list and `PreviousOutputIndex` is set.
3. **Compact the list.** Entries whose item was cleared are removed.

An output is a valid target when its port is enabled and the filter allows and the target's `TryAcceptItem` returns true. The filter rule is: the filter equals the item, or the filter is null and **no other output filter claims that item**.

- INFERRED: that last rule means an unfiltered output takes only items that no filtered output wants.
- UNKNOWN: the filter-null rule was only skimmed in the code. Confirm by test with two filters and one empty output.

Splitter configuration reaches the simulation as commands:

| Command | Type id | Sets |
|---|---|---|
| `MachinePortInput` (`TogglePortEnabled`) | 17 | PortStates |
| `SetItemInput` (`SetItem`) | 19 | Filters |
| `SetConfigInput` (`SetConfig2`) | 21 | Priorities |
| `LogisticFilterInput` | 13 | Not traced |

The splitter and balancer widgets (`WBP_Splitter`, `WBP_BeltBalancer`) send these through `AddInputCommand`.

### 3.4 Underground belts

- OBSERVED accept: a null item returns true (a quirk). Otherwise it refuses when the held count is at or over `Length`, and else adds `{item, 0}`. It does not check the source or the direction.
- OBSERVED update: items are processed from the last index to the first. Each item has a limit of `TotalProgress - i x ProgressPerLength`. Progress goes up by 1 while below the limit, so items keep spacing. When progress reaches `TotalProgress` and the output is valid, the underground calls `TryAcceptItem` on `OutputMachines[0]` and removes the item on true.
- INFERRED: index 0 is the oldest item.
- INFERRED: it is a pure delay line with no item visuals on the actor.

### 3.5 Machines that push and accept

Pushers use one pattern: check the target's `bAcceptsPushItems` (`+0x199`), then call its slot 114.

- OBSERVED: the assembler-type update pushes one item per output port per frame, round-robin over the ports. It takes the first non-empty stack of its output inventory and removes one unit on success. It skips this while its busy flag is set.
- OBSERVED: the same pattern is used by the pushers in phases 7, 13 and 4 (docks).
- UNKNOWN: how miners hand over items.

Accepting:

| Machine | Rule |
|---|---|
| Container | If two internal flags (`+0x1bc` and `+0x1c8`) are both set, it returns true without storing (INFERRED: a voiding or infinite mode, flags UNKNOWN). Otherwise it checks a per-slot limit array (`+0x1d0`, 24 bytes each). It rejects when the count of that item is at or above a set limit. Then it checks for space and adds. |
| Assembler | Needs an input inventory and a recipe. Checks for space, adds to the input inventory, then tries to start crafting. There is no buffer. INFERRED: the space check limits it to the recipe inputs. |
| Item elevator and others | Not decoded. |

The inner inventory interface (the object at inventory `+0x68`) has count at slot 2, add at slot 3 and remove at slot 4. See [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}) section 2.

### 3.6 A player takes an item off a belt

- OBSERVED: `BPC_ConveyorItemInteraction.Interact` calls `DMSPlayerController:TakeItemFromConveyor(ConveyorId)`. That creates an `InteractInput` command (type id `0x12`, 18) addressed to the belt id. It is flushed at the end of the tick like every command.
- OBSERVED: in the simulation the belt's slot 89 finds the sender, and when the belt has an item and the player inventory has space, it adds the item to the player and clears the belt (`bProgressComplete`, `CurrentItem`, `Progress`, and sets `bActorDirty`).
- OBSERVED: this goes through the command queue, so it is safe in co-op. The Blueprint side only gates the prompt (`CanInteract` is always true).

## 4. Placement and disassembly

### Placement check

- OBSERVED: for machines, the world update asks the machine class's buildable interface whether the placement is blocked. The interface pointer sits at machine `+0xf8`. Slot 5 of the interface is `IsPlacementBlocked`. It is the same function for belts, splitters, undergrounds, the item elevator, inventory machines and containers.
- OBSERVED: `IsPlacementBlocked` looks up the attach target by `AttachTargetID`. It then takes the class default dimensions, rotates and offsets them by the command's `GridTransform` (fixed point, shifted by 15 bits), and tests for overlap against the machines and blocks registered on that frame. If none overlaps it falls through to the frame's static collision.
- OBSERVED: if the attach target cannot be found, it returns **not blocked**.
- OBSERVED: the simulation does not check power, cost, tech unlock or range here. A blocked command is dropped silently, with no message. The client preview (red when invalid) is not authoritative.
- INFERRED, untested: a command with a wrong attach id may spawn a machine that is registered on no frame, instead of being rejected. Always pass the real frame id.
- OBSERVED: after the check the game charges the cost, spawns the object, registers it, attaches it (copies `GridTransform`, sets `AttachTargetID`, registers the box) and runs slot 86. See [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}) section 4 for the charge.
- OBSERVED: belt links are not made by a separate simulation command. `ConveyorBuilder.ConfirmBuild` sends build commands, and the link calls (slots 105 and 106) run in the registration path.
- UNKNOWN: the exact caller of the link calls.
- OBSERVED: the belt upgrade path (`bUpgrading` with an `UpgradeTarget`) sends `BuildUpgradeInput` (type id `0x2d`) with the ids of the belts under the preview.

### Disassembly

- OBSERVED: `AddDisassembleCommand(TargetID, bMass)` queues a `DisassembleInput` (type id `0x2f`, receiver `0`). The world update applies it from the disassemble queue before the build queue.
- OBSERVED: with `bMass` set and a valid target and player, the game walks all adjacent conveyors (`GetAdjacentConveyors`) and calls slot 123 on each, then removes the target.
- OBSERVED: slot 123 on a belt drops its `CurrentItem` into a drop object, unlinks both sides (slots 107 and 108, so neighbours fix their head state) and destroys the belt.
- OBSERVED: the refund is the build cost plus the contents. The refund rules are in [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}) section 3. The machine refund is in slot 123.
- OBSERVED: there is no `bPlayerDisassembly` test in the simulation's apply loop.
- INFERRED: the flag is enforced on the client (`BP_PlayerController` and the disassembly mode UI). A modded client, or a call to `AddDisassembleCommand`, may remove machines whose flag is false. Other co-op peers apply the command without that check. Needs a runtime test.
- OBSERVED: the frame query `CanDisassembleFrame` is true when the frame has no registered machines or children (INFERRED).

## 5. Determinism and checksum

- OBSERVED: every peer runs the same simulation frame with the same commands in the same order.
- OBSERVED: the host computes a checksum of the world after each frame and reports it. A difference between peers produces a desync report (`Client_ValidateDeterminism`).
- OBSERVED: the update lists hold raw pointers and their order matters. Changing a list, or the arrays `NextUpdate` and `InputMachines`, changes the result on one machine only.
- UNKNOWN: what the determinism check does when one peer writes a property locally (also listed on the inventory page).

## 6. What mods may and may not do

Rules for co-op:

- Every logistics change must go through a command: `AddInputCommand`, `AddDisassembleCommand` or the build command. Never write properties or lists directly.
- Reading is fine. Writing desyncs co-op and can corrupt the update lists.
- A speed change by writing `MaxProgress` at world load is **solo only**. INFERRED, untested.

Safe reads from Lua or native code:

- `ConveyorBeltObject`: `Progress`, `CurrentItem`, `MaxProgress`, `NextUpdate`, `PreviousUpdate`.
- `SplitterObject`: `Items`, `Filters`, `PortStates`.
- `UndergroundBeltObject`: `Items`, `Length`.
- `MachineObject`: `InputMachines`, `OutputMachines`, `bAcceptsPushItems`.

Ideas, INFERRED and untested:

- A throughput meter: count `TryAcceptItem` calls per second on a chosen belt.
- An item sink or source: a `ContainerObject` plus `bAcceptsPushItems`.
- Auto-configuring splitters with `SetConfig2` and `SetItem` commands.
- An auto-collect: call `TakeItemFromConveyor` once per belt id.

## 7. Hook points

### UE4SS (UFunction level)

UE4SS sees `UFunction` and `ProcessEvent` calls. It does not see native-to-native calls.

| Hook | Kind | Use |
|---|---|---|
| `ConveyorBelt:SetItem`, `SetItemLocation`, `SetItemMeshLocation`, `SetConfiguration` (`BP_ConveyorBelt_C` overrides) | Blueprint events called from the simulation | Observe every item entering a tile and every per-frame progress update. Very hot: one call per moving item per frame. Parameters: `Item`, `PreviousConveyorActor`; `Progress`, `MaxProgress`, `ItemTrack`. |
| `BP_ConveyorBelt_C:BP_SetItem(Item, PreviousConveyorActor)` | Blueprint function | The item visual spawn point. A post-hook gives the actor and its `ConveyorBeltObject`. |
| `ConveyorBeltObject:GetAdjacentConveyors(out OutConveyors)` | Native `UFunction` | Read-only neighbour query. A safe way to walk a belt line. |
| `DMSPlayerController:TakeItemFromConveyor(ConveyorId)` | `UFunction`, queues type 18 | Co-op safe item grab. |
| `AddInputCommand` with types 13, 17, 19, 21 | Command path | Configure splitters and ports. Co-op safe. |
| `DMSPlayerController:AddDisassembleCommand(TargetID, bMass)` | `UFunction` | Remove belts and machines. See the validation gaps in section 4. |
| `MachineObject:CanPlayerDisassemble`, `BuildingFrame:CanDisassembleFrame`, `MachinePortInput:TogglePortEnabled`, `Machine:SetPortEnabled` | `UFunction` | Read state or observe. |
| `WBP_Splitter`, `WBP_BeltBalancer` (`SetConfig2`, `SetItem`) | Widget Blueprint | Observe filter edits at UI level. |

### Native

A C++ mod should guard on the exe size and a hash of the code bytes before it patches. Addresses are in the table at the end.

- OBSERVED: `SimFrame` runs once per simulation frame and is identical on every peer. A pre-hook is a safe point for reads. A post-hook runs after the whole step.
- OBSERVED: the phases inside the world update are inline loops, not separate calls.
- INFERRED: patching a vtable slot (for example slot 114 per class) is cheaper than an inline hook.
- OBSERVED: the machine refund and the disassemble logic live in slot 123.

## 8. Gotchas

1. OBSERVED: do not add or remove objects in the world lists, or in `NextUpdate` or `InputMachines`. A duplicate in the head list makes a belt step twice per frame (double speed). A missing entry freezes a chain. The checksum then differs between peers.
2. OBSERVED: the actor pointer (`+0x70`) can be null or freed by chunk streaming. Apply the same checks the game does (section 1).
3. OBSERVED: the item calls on the actor run inside the simulation step on the game thread. A slow or failing Lua hook stalls the whole lockstep: peers wait and the stall counter grows. Keep hooks tiny. These calls happen once per moving item per frame.
4. OBSERVED: a native hook on `Step` or `TryAcceptItem` returns a bool in `al`. A hook that returns true without moving the item deletes or duplicates the item, because the caller clears its own `CurrentItem` on true. Call the original and only observe.
5. OBSERVED: underground accept returns true for a null item. Splitter accept requires the source to equal `NextInputMachine` when that is set. Do not call `TryAcceptItem` from a mod with a fake source.
6. OBSERVED: `LinkOutput` does not reject cyclic links. INFERRED: a ring with no head is never stepped. Do not build rings from code.
7. INFERRED, untested: `IsPlacementBlocked` returns not blocked for an unresolved attach id. Pass the real frame id.
8. OBSERVED: a mass disassemble walks every adjacent belt in one frame. A big belt network makes a spike.
9. OBSERVED: the refund goes to the sender player's inventory. What the game does with a full inventory is covered in the inventory page.
10. OBSERVED: `TakeItemFromConveyor` with an id that is not a belt sends `InteractInput` to slot 89 of that object. Most classes have an empty stub there. Pass belt ids only.
11. OBSERVED: the older note that the refund is made by `0x144217610` is wrong for machines. That function is for decoration objects. The machine refund is slot 123.

## Build-specific reference

Addresses are virtual addresses. Subtract `0x140000000` for the RVA. Image base `0x140000000`.

| Item | Value |
|---|---|
| Tick driver (per rendered frame) | `0x14426df60` |
| Step constant 1/45 s (`0.02222`) | `0x14739f5d8` |
| `SimFrame` | `0x14426c7c0` |
| `WorldUpdate` | `0x144267910` |
| World checksum | `0x14426b940` |
| Checksum report | `0x144210fb0` |
| Inputs-ready check | `0x14426e3d0` |
| Command processing | `0x144269b40` |
| Find object by id | `0x144221040` (and `0x144220ee0` for the world id map) |
| System registry (finds or creates the per-class system) | `0x1442670c0` |
| Belt step | `0x144230000` |
| Belt chain head update | `0x144236df0` |
| Belt recursion helper | `0x144238010` |
| Belt `TryAcceptItem` | `0x1442378f0` |
| Belt `HasItemReady` | `0x144230510` |
| Belt shape recompute | `0x1442353c0` |
| Belt take-item handler (slot 89) | `0x14422d450` |
| Belt `LinkInput` / `LinkOutput` | `0x144222890` / `0x144222a60` |
| Belt `UnlinkInput` / `UnlinkOutput` | `0x144233c30` / `0x144234060` |
| Belt `OnRegister` (slot 86) | `0x14422e840` |
| Head list erase helper | `0x1442335a0` |
| Belt actor `SetItem` stub | `0x1441b54d0` |
| Belt actor `SetItemLocation` | `0x14421db60` |
| `GetAdjacentConveyors` | exec `0x1441b6460`, native `0x14422b300` |
| `TakeItemFromConveyor` | exec `0x144213ef0`, native `0x144263fc0` |
| Splitter update / accept | `0x144236a00` / `0x144237be0` |
| Splitter `OnRegister` | `0x14422fc90` |
| Underground update / accept | `0x144237e70` / `0x144237cb0` |
| Underground `OnRegister` | `0x14422fe20` |
| Dock update (phase 4) | `0x14428de60` |
| Assembler update | `0x1442a2fd0` |
| Assembler accept | `0x1442a4c90` |
| Container accept | `0x1442377f0` |
| Item elevator accept | `0x1442a5e40` |
| Miner update | `0x144238060` |
| `IsPlacementBlocked` (interface slot 5) | `0x1442302c0` |
| `Frame_GetOverlaps` | `0x144276200` |
| Machine spawn | `0x144265d10` |
| Machine base `DisassembleSelf` (slot 123) | `0x144226170` |
| Mass disassemble walk | `0x144230790` |
| `AddDisassembleCommand` | exec `0x144211850`, native `0x1442608c0` |
| `CanPlayerDisassemble` | exec `0x1441e16b0` |
| Machine base vtable | `0x147353220` |
| Belt vtable | `0x14730d068` |
| Splitter vtable | `0x147386fa0` |
| Underground vtable | `0x14738f780` |
| Item elevator vtable | `0x147353f48` |
| Container vtable | `0x14730b870` |
| Assembler vtable | `0x1472f0198` |
| Miner vtable | `0x14735d328` |
| Belt constructor | `0x1441b3cd0` |
| Splitter constructor | `0x1441ffcc0` |
| Underground constructor | `0x144207380` |
| Item elevator constructor | `0x1441e02c0` |
| Buildable interface vtables (belt / splitter / underground / item elevator / inventory machine / container) | `0x14730d478` / `0x1473873b0` / `0x14738fb90` / `0x147354358` / `0x147353a70` / `0x14730bc80` |

Simulation list offsets in the world object: power `0xa8`, splitters `0x128`, belt heads `0xe8`, docks `0xb8`, assemblers `0x108`, undergrounds `0x138`, people elevators `0x178`, miners `0xc8`, system lists `0x2b0`, `0x2c0`, `0x2d0`, deferred `0x208`. Build queue `0x218` (stride `0x68`, count `0x220`). Disassemble queue `0x288` (stride 12, count `0x290`). Frame counter at `[[world+0x30]+0xb4]`. Stall counter at `+0x89c` of the player controller.

## Unknown

- The normal simulation step and so the frames per second. To measure it, log the frame counter over 10 seconds from a hook on `SimFrame`.
- The numeric `MaxProgress`, `MaxParallelItems`, `ProgressPerLength` and `MaxLength` per tier. The Blueprint defaults do not hold them.
- Dock filter fields, dock loading and unloading rules, and which dock fields the dock update reads. The notes only show that docks update in phase 4 and push through `TryAcceptItem`.
- Who calls the link slots (105 and 106) after a build.
- Which classes run in update phases 5, 7, 9, 10, 13 and 15 to 19, and which run in the system lists.
- Whether the simulation enforces `bPlayerDisassembly`. Test: send `AddDisassembleCommand` for a machine whose flag is false, solo.
- How the item elevator and its update work, and what the container flags (`+0x1bc`, `+0x1c8`) mean.
- The class of the refund drop object, and whether its items also reach the inventory.
- The splitter filter-null rule (section 3.3).
- How miners hand over items.
