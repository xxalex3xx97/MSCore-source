---
title: Inventories, refunds and builds
parent: Game internals
nav_order: 1
---

# Inventories, refunds and builds

Notes from reading the game and testing mods in it. Each claim is tagged:

- **OBSERVED**: read in the game's code or seen in a test.
- **INFERRED**: follows from what was read, not tested.
- **UNKNOWN**: not found yet.

Build these notes apply to: `MainSequence-Win64-Shipping.exe`, exe size `149200896`, Unreal Engine 5.7. Addresses are listed only in the table at the end. They are build specific and change with every game update.

## 1. Commands and the simulation

- OBSERVED: the simulation runs in lockstep. Every state change goes through a command queue. Every peer applies the same commands in the same order, then runs the world update.
- OBSERVED: a command carries its type id at offset `0x28`. A command with type id `0` is dropped when the queue flushes.
- OBSERVED: each command has a receiver id. The receiver decides which object applies it. `BuildInput` and `DisassembleInput` use receiver `0` (the world). `InventoryInput` is applied by the **player object**, so its receiver must be the player id. Sent with receiver `0` it is silently dropped.
- OBSERVED: the game's inventory UI does not send the command directly. It calls `DMSPlayerController:AddInventoryInteraction` with a struct `{PrimaryInventoryID, SecondaryInventoryID, SlotIndex, Action}`. The controller builds the command and addresses it to the player.
- OBSERVED: from UE4SS Lua you can pass a Lua table as that struct argument. It works the same as the UI.

{: .warning }
Do not change inventories or properties directly from a mod (`Inventory:AddItems`, `RemoveItems`, property writes). Those run only on your machine. Other peers do not run them, so the checksums differ and the session desyncs. Use commands.

{: .warning }
`MSCore_inv_move` and `MSCore.factory_native.inv_move` send the command with receiver `0`. Tests show that does nothing. Use `AddInventoryInteraction` from Lua until the export takes a receiver.

## 2. Inventories and stacks

- OBSERVED: an `Inventory` object holds an inner inventory object, embedded at `+0x68`. Its virtual functions: slot 2 returns the count of an item, slot 3 adds, slot 4 removes.
- OBSERVED: **add returns the leftover**, the amount that did not fit. It returns `0` when everything fit. It does not return the amount accepted.
- OBSERVED: the game has an `InventoryGroup`, an inventory-like object with a list of member inventories. Count sums the members. Add and remove go through the members in order, first member first, passing the remainder on.
- OBSERVED: capacity of one slot is `item max stack x StackMultiplier / 2`. A StackMultiplier of `2` means normal stacks. The UI shows `(StackMultiplier - 2) x 50` percent.
- OBSERVED: the player's multiplier is `2 + sum(count x bonus of each equipped item)`. It is derived, and no command sets it.
- OBSERVED: a container's multiplier is `2 + StackSizeBonusMultiplier`, fixed when the container is spawned. A container that is already placed keeps its old size after you change the class default.

### InventoryInput actions

`InventoryInput` has the fields Primary id, Secondary id, SlotIndex and Action. It has **no amount field**.

| Action | Name | What it does |
|---|---|---|
| 0 | TAKE | Cursor empty: picks up the slot, up to one stack. Cursor holding: puts the whole cursor into the slot (merges, or swaps a different item). |
| 1 | PLACE_SINGLE | Cursor empty: takes half the slot. Cursor holding: places exactly one unit. |
| 2 | TRANSFER | Moves the whole slot from Primary to Secondary. Partial when the destination is full. No cursor. |
| 3 | RELEASE | Like TAKE. Does nothing when the cursor is empty or the target is the origin slot. |
| 4 | DROP_CURSOR | Puts the cursor into the player's own inventory. Whatever does not fit stays in the cursor. |
| 5 | SORT | Sorts the Primary inventory. |

- OBSERVED: the UI sends a press and a release. A shift-click sends `2` with the destination, then `3` with Secondary `0`. A left press sends `0`, a right press sends `1`, mouse-up always sends `3`.
- OBSERVED: the cursor is per player (a held stack and its origin slot). Only commands change it.
- INFERRED, untested: to move an exact amount, `TAKE` the stack, `PLACE_SINGLE` N times into the destination, then `TAKE` the source slot again to put the rest back (N + 2 commands). The cursor holds at most two stacks.
- UNKNOWN: whether a stack held in the cursor survives a save and load.

## 3. Deconstruction refunds

- OBSERVED: `DisassembleInput` has the fields `TargetId` and `bMass`. It has no inventory field.
- OBSERVED: the refund is the machine's build cost **plus the contents** of the machine (container items, one item per belt). It goes into the **sender's own inventory**. What does not fit is collected into **one drop object** at the machine.
- OBSERVED: `bMass` only matters for conveyor belts, where it removes the connected chain.
- OBSERVED: there is no creative-mode check on a refund.
- INFERRED: the refund target is read once at the start of the refund function. A native hook can replace it.
- UNKNOWN: whether a refund into another inventory is safe in co-op. A peer without the same change would put the items somewhere else.

## 4. Build charge

- OBSERVED: a machine's cost is charged when the sim applies the command, not when you confirm. A cost that cannot be paid means the machine is silently not built.
- OBSERVED: the charge uses the inventory of the piloted ship when the player is piloting, otherwise the player's inventory. Creative players skip the charge.
- OBSERVED: a placement command does not check floors, parity or overlap. A mod that places machines must check those itself.

## 5. Designer and blueprints

- OBSERVED: confirming the station designer sends `PendingBlueprintInput` (type id 33) with `bApply`, `bReset`, `PrimaryInventoryID`, `PlayerInventoryID` and `EditingFromBlockID`. The pending blueprint itself is state on the building frame.
- OBSERVED: it works on **blocks only**. Machines never go through it.
- OBSERVED: the cost is the net of added minus removed blocks, charged through an `InventoryGroup` of the primary and player inventory. Costs are taken from the primary first. Refunds go to the primary first. If both are full the leftover refund is lost, with no drop object.
- OBSERVED: the game does not check what the two ids are. They must be real inventory ids.
- OBSERVED: `Blueprint.Machines` on a station is a snapshot. After you place or remove machines it takes about 20 to 30 seconds to show the change.

## 6. Machine configs

- OBSERVED: machines serialize and apply their config as bytes (`SerializedMachineConfig`). A recipe machine uses `01 00`, the recipe name as a string, a count, then one byte per port.
- OBSERVED: undergrounds are one machine at the entrance with `01 00` plus the length as an `int32`. Nothing exists at the exit cell. In the station snapshot an underground's stored config is empty, so its length cannot be read from there.
- OBSERVED: port order for refineries and assemblers: clockwise from the east side, the top-most port first.
- UNKNOWN: port order for the advanced assembler, enrichment chamber, growth chamber, relic synthesizer and supercomputer.

## 7. UE4SS Lua pitfalls

- OBSERVED: reading a property that does not exist on a struct (`UScriptStruct`) throws inside UE4SS. UE4SS then builds a traceback, and that can crash the game at random. A `pcall` does not help. Read structs only through fields you know exist, or read them in native code.
- OBSERVED: reading thousands of struct entries in Lua (`Blueprint.Machines`) crashed the game at random positions. The same read in native code (MSCore `read_station`, `read_inventory`) was stable.
- OBSERVED: callbacks passed to `TArray:ForEach` can leave the Lua stack inconsistent. Later code then fails with errors that make no sense. Walk arrays with `GetArrayNum` and an index.
- OBSERVED: call `IsValid` before any method on a `UObject` that may have been freed. A call on a freed object crashes the game.
- OBSERVED: a Lua hook on `AddInputCommand` cannot see commands that the UI sends from native code. Hook `AddInventoryInteraction` to see inventory clicks.
- OBSERVED: a class that is not loaded yet cannot be hooked. Register again later, for example each time your window opens.
- OBSERVED: the Buildable's own `Class` property is the machine class it builds. `GetClass()` on a Buildable returns `Buildable`.

## Build-specific reference

| Item | Value |
|---|---|
| Inventory input handler (player object, vtable slot 89) | `0x14427f8e0` |
| Machine refund (vtable slot 123) | `0x144226170` |
| Build charge inventory helper | `0x14427f120` |
| `InventoryGroup` constructor | `0x14421a0b0` |
| Designer apply function | `0x144274f00` |
| Player multiplier function | `0x144281180` |
| Container spawn | `0x14422e5d0` |

## Unknown

- Whether a stack held in the inventory cursor survives a save and load.
- Whether a refund can be redirected natively, and whether that is safe in co-op.
- Port order for five machine classes (section 6).
- What the game writes for an underground's exit config.
- Why struct reads crash in UE4SS Lua, and why the station snapshot lags.
- What the lockstep determinism check does when one peer writes locally.
- Stack sizes of some items.
