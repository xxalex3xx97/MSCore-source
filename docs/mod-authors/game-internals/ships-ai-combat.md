---
title: Ships, AI orders and combat
parent: Game internals
nav_order: 5
---

# Ships, AI orders and combat

Notes from reading the game's code. The game was not run for most of this, so most claims come from the code and data, not from live tests. Each claim is tagged:

- **OBSERVED**: read in the game's code or data, or seen in a test.
- **INFERRED**: follows from what was read, not tested.
- **UNKNOWN**: not found yet.

Build these notes apply to: `MainSequence-Win64-Shipping.exe`, exe size `149200896`, Unreal Engine 5.7. The exe has no symbols. Addresses are listed only in the table at the end. They are build specific and change with every game update. Offsets in the text are offsets inside objects, not addresses.

## 1. The short version

- OBSERVED: there is no crew AI. "Crew", "Captain", "StationCommander" and similar names are dialogue NPCs (`VendorObject`). All AI belongs to ships.
- OBSERVED: player-faction ships run **orders**. Enemy ships run a six-state **task machine**. Enemies are not orders.
- OBSERVED: AI ships are the same class as the player ship (`DMSPlayerShipObject`). "Autopilot" means `bAutopilot` plus an `Orders[]` list plus `CurrentOrderIndex`. The simulation runs the current order every tick. There is no behaviour tree.
- OBSERVED: all AI and combat code is native code called from the simulation world tick. None of it is a UFunction, so UE4SS hooks never see it. Lua can send commands and read state. Changing the logic needs a C++ native hook.
- OBSERVED: combat is 100% simulation (deterministic, lockstep, saved). The Blueprint actors (`BP_RelicCrescent` and others) only show effects, markers and sounds.
- OBSERVED: the only safe way to change AI or combat state from a mod is an input command through `DMSPlayerController:AddInputCommand`, or a controller function that queues one.

{: .warning }
Never write ship properties from a mod (`Orders`, `CurrentOrderIndex`, `bAutopilot`, `TargetDockID`, `Health`, `Shields`, `Hull`, `Weapons`, `Target`). The simulation is lockstep, every peer simulates everything, and a local write desyncs co-op. Use commands.

## 2. Where the AI runs

- OBSERVED: the simulation world tick calls one static update function per object category. The categories are arrays on the `DMSWorld` object.

| DMSWorld array (offset) | Objects | What runs |
|---|---|---|
| `+0x98` PlayerShips | player-faction ships, including AI ships | ship tick: movement, warp, autopilot, orders, docking autopilot |
| `+0x198` EnemyShips | enemy ships | enemy dispatcher (section 7) |
| `+0x1a8` EnemyStations | enemy stations | spawn counter (section 8) |
| `+0xb8` Docks, `+0xc8` Miners, `+0x168` FloatingOres, `+0x1b8` BulletManagers, `+0xa8` PowerGrids, `+0x178` Elevators | logistics and bullets | per-type update |
| `+0x2b0` EarlyUpdates, `+0x2c0` DefaultUpdates, `+0x2d0` LateUpdates | drones, bays, beacons (INFERRED) | generic update through virtual slot 88 |

- INFERRED: `AttackManager` is updated through the manager object, not through those arrays.
- OBSERVED: other AI-related `DMSWorld` arrays: `+0x188` DroneTasks, `+0x1e8` DroneBays, `+0x1f8` MiningBeacons, `+0x90` ArenaWaveManager.
- OBSERVED: the frame counter used everywhere in the AI is read through the object's world pointer (`[[obj+0x28]+0x30]+0xb4`) and compared as an integer number of frames.
- OBSERVED: the command path is `controller.AddInputCommand(receiverId, cmd)`, then the simulation's command processor, then `object(receiverId)` virtual slot 89 (`ApplyCommand`). Orders have their own `ApplyCommand` too.
- OBSERVED: every AI function tests the garbage flag before it uses a stored pointer: bit 30 of the `UObject` flag word at `+8`. Set means the object is invalid. Do the same in a mod.

## 3. Ship objects

`DMSPlayerShipObject` (size `0x9b0`, super `BuildingFrame`, size `0x4c0`) is the class for the player ship and for every AI ship. Stations (`StationObject`, size `0x5c0`) share the `BuildingFrame` damage model.

### 3.1 Base object fields

| Offset | Field | Notes |
|---|---|---|
| `+0x30` | ID | object id |
| `+0x78` | Location | |
| `+0x84` | Quat | |
| `+0x94` | ChunkLocation | int3 |
| `+0xa0` | SolarSystemID | |
| `+0x28` | world / system reference | |
| `+0x70` | Actor | presentation actor |

All OBSERVED. This is `DMSObjectBase` (size `0xf8`).

### 3.2 Ship fields: flight, autopilot, orders

All OBSERVED. `PNum` is a 16.16 fixed-point int32.

| Offset | Field | Notes |
|---|---|---|
| `+0x2e8` | Health | i32 |
| `+0x340` | Inventory | |
| `+0x4e0` | FlightModel | data asset (section 3.3) |
| `+0x4e8` | Throttle | range -10 to 10 |
| `+0x4ec` | LastThrottleUpdateFrame | |
| `+0x558` | AutopilotImpulse | vector |
| `+0x564` | AvoidanceBias | |
| `+0x56c` | AutopilotImpulseMultiplier | |
| `+0x5d0` | DistanceToTarget | |
| `+0x5e8` | bAutopilot | |
| `+0x5ec` | CurrentOrderIndex | |
| `+0x5f0` | Orders[] | pointers, Num at `+0x5f8` |
| `+0x600` | OrderIDs[] | ints, Num at `+0x608` |
| `+0x610` | AutoMiningData | struct, size `0x40` (section 5.4) |
| `+0x650` | AutopilotExitDockTimer | |
| `+0x668` | bJumpTargetSet | |
| `+0x66a` | bInsufficientWarpFuel | |
| `+0x678` | TravellingTime | warp tunnel |
| `+0x694` | JumpAnimProgress | warp charge |
| `+0x6a4` | WarpTravelProgress | arrival |
| `+0x6bc` / `+0x6c8` / `+0x6d4` | JumpTargetLocation / JumpTargetChunkLocation / JumpTargetSolarSystemId | |
| `+0x6d8` | Drones[] | ship-mounted drones |
| `+0x700` | TaskQueue[] | drone tasks |
| `+0x7e0` | WarpDrive | |
| `+0x890` | DockingDistance | |
| `+0x898` | Dock | the dock the ship is in |
| `+0x8a0` | DockID | |
| `+0x8a4` | TargetDockID | |
| `+0x8a8` | bDockingAutopilot | |
| `+0x8ac` / `+0x8b8` / `+0x8c4` | DockTargetLocation / DockTargetChunk / DockTargetQuat | |
| `+0x910` | UndockingAnimationTimer | |
| `+0x970` | Pilot | |
| `+0x980` | DockedShips[] | |

Combat and targeting fields of the same class are in section 9.

### 3.3 FlightModel data asset

OBSERVED: size `0xe8`. Only one exists, `DAT_SmallShipFlightModel`.

| Offset | Field |
|---|---|
| `+0x30` | ThrottleInputDelay (frames) |
| `+0x80` | AlignmentTolerance |
| `+0x84` | AutopilotImpulseMultiplier |
| `+0x90` | AutopilotMinThrottle |
| `+0x94` | AutopilotRotationImpulseForce |
| `+0x98` | AutopilotBankFactor |
| `+0x9c` | AutopilotExitDockTime |
| `+0xa0` to `+0xb0` | AvoidanceQuerySize, QueryDist, QueryHeightOffset, Weight, Time |
| `+0xc8` | WarpArrivalDistance |
| `+0xdc` | RequiredChargeToJump |
| `+0xe4` | FinalApproachProximity |

OBSERVED: `FlightModel.SteeringResponse` and `BounceForce` are also used by the code.

### 3.4 Dock object

OBSERVED: `Dock` (size `0x2d0`, super `MachineObject`).

| Offset | Field |
|---|---|
| `+0x248` | DockedShipID |
| `+0x250` | Filters[] |
| `+0x268` | DisplayName |
| `+0x278` | DockingQueue (array of ship ids, Num at `+0x280`) |
| `+0x1c4` | DockingQueueOffset (spacing per queue slot) |
| `+0x298` | DockedShip |
| `+0x2a8` | ParentShip |

## 4. Pilots, autopilot and fleets

- OBSERVED: `+0x970 Pilot` is the ship's pilot. With no pilot, switching autopilot off sets the throttle to 0.
- OBSERVED: the autopilot is not a separate class. It is the ship tick running `Orders[CurrentOrderIndex]` when `bAutopilot` is set, then applying the throttle step.
- OBSERVED: `GetCurrentOrder` (a UFunction) returns `Orders[CurrentOrderIndex]` only if `bAutopilot` is set and the index is in range. Otherwise it returns null.
- OBSERVED: a separate path, the **docking autopilot** (`bDockingAutopilot`, `TargetDockID`), is used by `DockingInput`. It flies the ship to a dock and docks it. It does not use orders (section 5.7).
- OBSERVED: `DMSPlayerShipObject.DockedShips[]` lists ships docked in this ship. Small fighters in a hangar (`BP_Fighter_01`, `BPO_FighterDock`, `BPO_FighterPilotConsole`, `BPO_FighterDocking_01`) use the same ship class.
- OBSERVED: `FleetTagIDs` is at `BuildingFrame +0x300`. `IsFleetTagVisible` is a UFunction on the frame. `FactionManager` keeps per faction lists of Players, Ships, Stations, Docks, Missions and FleetTags.
- OBSERVED: `BuildingFrame.Faction` (`+0x2c0`) is the frame owner.
- INFERRED: enemy objects are not in any faction list. Hostility is "enemy classes against everything else".
- INFERRED: there are no friendly NPC ships. Player-faction ships with orders are the only friendly AI. Enemies are the only autonomous AI.
- UNKNOWN: the classes `Formation` and `EnemyFleet` (size `0xf8`) have no properties and no users in the Blueprint dumps. Treat them as unused.

### 4.1 Flight each tick

OBSERVED, abridged. This is the ship tick.

1. Health 0 or less: the ship drifts if `bDrifting` (`+0x5b0`) is set, then the tick returns.
2. `+0x6a4` greater than 0: arriving from warp. It counts up. At its maximum (`+0x6a8`) the arrival is finished.
3. `TravellingTime` (`+0x678`) greater than 0: in the warp tunnel. It counts up and ends the travel.
4. Normal flight. If `JumpAnimProgress` (`+0x694`) is greater than 0, the warp is charging. It counts up. At `RequiredChargeToJump` the warp starts.
5. Else, if `bJumpTargetSet`, the tick computes the jump cost, sets `bInsufficientWarpFuel` when the cost is more than the fuel, and updates alignment.
6. `UndockingAnimationTimer` (`+0x910`) greater than 0: the ship coasts on `AutopilotImpulse` and the timer counts down.
7. Else, if `bAutopilot`: run the current order (section 5.2), then apply the throttle step.
8. Else, if `bDockingAutopilot`: find the dock by `TargetDockID`. If the dock is not valid, clear the flag. Otherwise fly to `DockTargetLocation` with `DockingDistance` as the arrival distance. On arrival, dock.
9. Else: manual throttle and steering.

Then the tick notifies the collision environment if the position changed, ticks the ship's drones and runs abilities.

### 4.2 Flying to a target

OBSERVED. One shared function flies a ship to a point. Orders pass an arrival distance of `0x30000` (3.0 units). `DockingInput` passes the ship's `DockingDistance`.

- While `JumpAnimProgress` is greater than 0 the function returns (busy charging).
- If the ship is docked, it undocks. On success `AutopilotExitDockTimer` is set to `FlightModel.AutopilotExitDockTime`. While the timer runs the throttle is pushed toward +10, the timer counts down, and the function returns false.
- Far target (another chunk or system, more than the minimum chunk distance): the function sets a jump target about 7 seconds after autopilot was switched on, charges, and warps. Warp fuel is taken in one lump at charge start. The arrival point is the target minus the direction times `FlightModel.WarpArrivalDistance` (`+0xc8`). A dock trip therefore needs no warp code in the mod.
- Near target: steering uses the FlightModel values. The throttle moves by 1 every `ThrottleInputDelay` frames toward +10 or -10. The function writes `AutopilotImpulse`, `AvoidanceBias` and `DistanceToTarget`. It returns true within the arrival distance. A dock approach also needs alignment.
- INFERRED: avoidance is a ray-query steering bias against the collision environment. It is not pathfinding. A ship can stall behind a large obstacle.
- INFERRED: with no `WarpDrive`, or no fuel, a ship sent to a far dock stalls (`bInsufficientWarpFuel`) and the order stays current.

## 5. The order system

### 5.1 Order classes

OBSERVED: orders are saved objects owned by the ship. All derive from `ShipOrderBase`.

| Class | Size | Fields |
|---|---|---|
| `ShipOrderBase` | `0x78` | `+0x68` ShipObject; a `bCurrent` flag byte |
| `DeliverOrder` | `0xb0` | `+0x78` Dock, `+0x80` DockID, `+0x85` bWaiting, `+0x88` WaitConditions[] (Num at `+0x90`), `+0x98` WaitConditionIDs[] (Num at `+0xa0`), `+0xa8` CurrentWaitCondition |
| `WaitOrder` | `0x88` | `+0x78` Item, `+0x80` Comparison (byte), `+0x84` Amount |
| `WaitForTimeOrder` | `0x80` | `+0x78` Progress, `+0x7c` MaxProgress (the UI field "Seconds") |
| `LoadItemsOrder` | `0x88` | `+0x78` Item, `+0x80` Amount |
| `UnloadItemsOrder` | `0x80` | `+0x78` Item |
| `CollectOrder` | `0x90` | `+0x78` Miner, `+0x80` MinerID, flag bytes `+0x84` / `+0x85` / `+0x86`, `+0x88` ItemsRetrievedByDrone, `+0x8c` ItemsToWaitFor |
| `TutorialShipOrder` | `0x80` | `+0x78` bTutorialComplete, `+0x7c` Timer (counts to 90 frames, then fires `OnTutorialComplete`) |

- INFERRED: the three flag bytes of `CollectOrder` are bArrived, bWaitingForTask and bDroneHasCompletedTask.
- OBSERVED: `DeliverOrder` is the only order that moves a ship. `WaitOrder`, `WaitForTimeOrder`, `LoadItemsOrder` and `UnloadItemsOrder` are **conditions** attached to a `DeliverOrder` through `WaitConditions[]`.
- INFERRED: the conditions can also be used as top-level orders, as stand-alone waits.
- OBSERVED: `EAmountComparison` values: 0 LESS, 1 LESSEQUAL, 2 EQUAL, 3 GREATEREQUAL, 4 GREATER.
- OBSERVED: `EShipOrderInput` values: 0 NEW, 1 DELETE, 2 MODIFY, 3 SKIP.
- OBSERVED: "any item" is the data asset `/Game/Data/Items/ITEM_AnyItemFilter`. The UI substitutes it when the order item is null.
- OBSERVED: a destroyed or invalid pointer is detected by the garbage flag at `+8` (section 2).

### 5.2 How orders are executed

- OBSERVED: each ship tick with autopilot on calls `Orders[CurrentOrderIndex]`, virtual slot 97 (`+0x308`, `Update`). It returns true when the order is finished.
- OBSERVED: when the order finishes, the ship clears its current flag, adds 1 to the index, and wraps. If there are 2 or more orders and the index passes the end, it goes back to 0. The list loops forever.
- OBSERVED: with exactly 1 order the index becomes 1, which is out of range, and the ship idles after the order finishes.
- OBSERVED: the new current order is flagged and the widgets are refreshed with refresh type 4 (AUTOMATION).
- OBSERVED: creating a `DeliverOrder` does not move the ship. The ship moves only when autopilot is on **and** the index points at the order.

### 5.3 DeliverOrder: what one order does

OBSERVED. One `DeliverOrder` means: fly to the dock, dock, wait until **all** wait conditions are true, then advance.

1. No dock, or an invalid dock: the update returns false. The order never finishes.
2. If not yet waiting (`bWaiting` clear): reserve a queue slot at the dock, prepare the jump, and fly toward the slot position with the dock approach flag.
3. On arrival: if the queue position is not 0, return false (wait in line). If it is 0, dock and set `bWaiting`.
4. While `bWaiting`: evaluate every condition every tick. The first condition that is not yet true becomes `CurrentWaitCondition`. If any condition is pending, return false.
5. When all conditions are true: clear `bWaiting` and return true. The ship advances.

An order with no conditions finishes the same tick the ship docks.

### 5.4 The dock queue

- OBSERVED: reserving a slot finds the ship id in `Dock.DockingQueue` (it appends the id if missing). The index is the position. If the dock already holds a ship (`DockedShip` valid), the index is one more.
- OBSERVED: index 0 targets the docking location and rotation of the dock. Index above 0 targets a point offset from the dock by index times `DockingQueueOffset`. A line of ships waits outside.
- OBSERVED: the ship is removed from the queue when the order exits, when autopilot is switched, and when `UpdateShipOrderInput` changes the order's dock.
- OBSERVED: the queue exists so several ships can target one dock.

### 5.5 Conditions: wait, load, unload, time

| Order | True when |
|---|---|
| `WaitOrder` | the ship's count of `Item` compared with `Amount` by `Comparison`: LESS n<Amount, LESSEQUAL n<=Amount, EQUAL n==Amount, GREATEREQUAL n>=Amount, GREATER n>Amount. Invalid item or ship gives false. |
| `LoadItemsOrder` | count of `Item` is at least `Amount` ("wait until loaded") |
| `UnloadItemsOrder` | count of `Item` is 0 or less ("wait until unloaded") |
| `WaitForTimeOrder` | `Progress` has reached `MaxProgress`. Then `Progress` is reset to 0 and it returns true. Otherwise `Progress` goes up by 1 and it returns false. |

- OBSERVED: conditions never move items. Docks move items by their `Filters[]` (logistics), which changes the ship inventory. The order only reads the inventory.
- OBSERVED: `WaitForTimeOrder` counts frames. The UI multiplies the typed seconds by a factor before sending it as `Amount`.
- UNKNOWN: that factor was not decoded. INFERRED: 60 per second.
- OBSERVED: `CollectOrder` asks a drone to collect from the `Miner` (`ItemsToWaitFor`) and finishes when the drone has delivered (INFERRED). No UI creates it.
- OBSERVED: the controller functions `NewCollectOrder`, `NewDeliverOrder` and `NewWaitOrder` are dead stubs. They do nothing.

### 5.6 Order virtual interface

OBSERVED. Virtual slot N is at byte offset N times 8.

| Slot (offset) | Function |
|---|---|
| 87 (`+0x2b8`) | ResolveIDs: `DeliverOrder` turns `WaitConditionIDs` into pointers and `DockID` into `Dock` |
| 89 (`+0x2c8`) | ApplyCommand |
| 92, 93 (`+0x2e0`, `+0x2e8`) and 99, 100 (`+0x318`, `+0x320`) | save and load variants (INFERRED) |
| 96 (`+0x300`) | AttachToShip: sets `+0x68` to the ship. `DeliverOrder` forwards it to every wait condition |
| 97 (`+0x308`) | Update: returns true when finished |
| 98 (`+0x310`) | OnExit: `DeliverOrder` removes the ship from the dock queue and clears `bWaiting`. `WaitForTimeOrder` also has one |

OBSERVED: `SetCurrent(order, bool)` sets the current flag and fires the refresh delegate. Refresh type 4 (AUTOMATION) is what the autopilot status text reacts to.

### 5.7 Commands that create and change orders

OBSERVED. Receiver and fields:

| Command | Receiver | Fields |
|---|---|---|
| `NewShipOrderInput` | ship id (top-level order) or `DeliverOrder` id (condition) | `+0x40` Class |
| `UpdateShipOrderInput` | order id | `+0x40` TargetID, `+0x48` Item, `+0x50` Comparison, `+0x54` Amount |
| `ConfigureShipOrdersInput` | ship id (or order id to remove a condition) | `+0x40` SetAutopilotEnabled (-1 = none, else 0 or 1), `+0x44` RemoveOrderID (-1 = none), `+0x48` SkipToIndex (-1 = none), `+0x50` ReorderedIndices[] |
| `DockingInput` | ship id | dock id (-1 cancels, or undocks) |
| `MiningAutopilotInput` | ship id | see section 5.8 |

**NewShipOrderInput (ship receiver).** The game creates the order object with the ship as outer, adds it to `Orders` and `OrderIDs`, attaches it to the ship, and sets it current when it is the only order. It then refreshes the widgets.

- OBSERVED: there is no class filter. Any `ShipOrderBase` subclass is accepted.
- INFERRED: a class that is not a `ShipOrderBase` subclass makes the simulation crash.
- OBSERVED: the UI uses 5 classes: `DeliverOrder`, `WaitOrder`, `WaitForTimeOrder`, `LoadItemsOrder`, `UnloadItemsOrder`. The other two, `CollectOrder` and `TutorialShipOrder`, exist. A new order class is not possible, because the class list is native.

**NewShipOrderInput (DeliverOrder receiver).** Creates a child condition, appends it to `WaitConditions` and `WaitConditionIDs`, and attaches it to the ship.

**ConfigureShipOrdersInput.** The fields are checked in this order: autopilot, remove, skip, reorder.

- SetAutopilot, switching off: if there is no pilot the throttle is set to 0, and the current order's `OnExit` runs.
- SetAutopilot, switching on: if `CurrentOrderIndex` is invalid and there are orders, `OnExit` runs for the previous order, the index becomes 0 and `Orders[0]` becomes current. The dock queue of the `TargetDockID` object is released. Then `bAutopilot` is written. Refresh type 4.
- SkipToOrder(idx): if `0 <= idx < Num`, the current order gets its flag cleared and `OnExit`, the index becomes idx, and `Orders[idx]` becomes current. It does not change `bAutopilot`.
- Reorder: builds the new `Orders` from `Orders[ReorderedIndices[i]]` with **no bounds check**. `CurrentOrderIndex` stays on the same order object. `OrderIDs` is rebuilt.
- RemoveOrder: removes the order by id.

**Order-level ApplyCommand.** OBSERVED.

- `DeliverOrder`: `NewShipOrderInput` adds a child condition. `ConfigureShipOrdersInput(RemoveOrderID)` removes that child from both arrays. `UpdateShipOrderInput` sets the dock: it finds the object by `TargetID`, removes the ship from the old dock's queue, and sets `Dock` and `DockID`.
- `WaitOrder`, `LoadItemsOrder`, `UnloadItemsOrder`: set `Item`, `Comparison`, `Amount`.
- `WaitForTimeOrder`: sets `MaxProgress` from `Amount`. The UI sends seconds times the factor.
- OBSERVED: the fields are per class. `Dock` applies only to `DeliverOrder`. `Item`, `Comparison` and `Amount` apply to wait, load and unload. `Amount` alone applies to `WaitForTimeOrder`. Dock fields sent to a `WaitOrder` do nothing useful.

**Order factory functions** (Lua-visible factories that build these commands, all OBSERVED):

- `NewShipOrderInput.CreateInputNewShipOrder(Class)`, with Class one of `/Script/MainSequence.DeliverOrder`, `WaitOrder`, `WaitForTimeOrder`, `LoadItemsOrder`, `UnloadItemsOrder`.
- `UpdateShipOrderInput.CreateInputUpdateShipOrder(TargetID = dock id, Item, Comparison, Amount)`.
- `ConfigureShipOrdersInput.CreateInputSetAutopilot`, `CreateInputSkipToShipOrder`, `CreateInputRemoveShipOrder`, `CreateInputReorderShipOrder`.
- `DockingInput.CreateInputDocking(dockId)`.
- `MiningAutopilotInput.CreateMiningAutopilotInput(ItemStack, DockID, MiningBeaconID)`.

**Controller functions** (they only queue the same commands, so they are lockstep and co-op safe, and safe from Lua):

| Function | Queues | Details |
|---|---|---|
| `SetShipAutopilot(shipId, bool)` | `ConfigureShipOrdersInput`, type id 2 | command `+0x38` = shipId, `+0x40` = bool, `+0x44` = -1, `+0x48` = -1 |
| `SkipToOrder(shipId, idx)` | `ConfigureShipOrdersInput` | `+0x40` = -1, `+0x44` = -1, `+0x48` = idx |
| `DeleteOrder(shipId, orderId)` | `ConfigureShipOrdersInput` | remove |
| `ReorderShipOrders({ShipID, NewOrderedIndices})` | `ConfigureShipOrdersInput` | reorder |
| `DockCommand(dockId or -1)` | `DockingInput` | the separate docking autopilot |
| `AddInputCommand(receiverId, cmd)` | any command | |

Commands are appended to the controller's `PendingCommands` (`+0x768`).

- OBSERVED, tested in game: the player flow is `SetShipAutopilot(ship, true)` then `SkipToOrder(ship, 0)` on the existing `DeliverOrder`. Creating an order alone does not move the ship.

### 5.8 The mining autopilot data

- OBSERVED: `MiningAutopilotInput` (command id 8) only **stores** `TargetResource`, `Dock`, `DockID`, `MiningBeacon` and `MiningBeaconID` into the ship at `+0x610` to `+0x638`. No simulation code reads those fields. A scan of the game logic code found only the apply function, the id resolver, and save and clipboard code.
- INFERRED: in this build the "native mining autopilot" is user interface and data only. Nothing native mines for the ship. A mod's own mining state machine is not duplicated by the game.

### 5.9 Recipes for mods

- INFERRED: a top-level `WaitOrder`, `LoadItemsOrder` or `UnloadItemsOrder` gives "wait until a ship inventory condition holds", with no dock.
- INFERRED: a trade loop is `DeliverOrder` A with a `LoadItemsOrder` condition for X, then `DeliverOrder` B with an `UnloadItemsOrder` condition for X. With 2 or more orders the list loops by itself.
- INFERRED: "stop after the last order" must be done by the mod (`SetShipAutopilot` false), or by keeping exactly one order (the ship idles after it finishes).
- OBSERVED: the mod must compute busy docks itself. The `DockCommand` path does not use the dock queue.

## 6. Ship state a mod can read

OBSERVED, read only from Lua:

- ship: `bAutopilot`, `Orders[]`, `CurrentOrderIndex`, `GetCurrentOrder()`.
- orders: `Dock`, `DockID`, `WaitConditions`, `CurrentWaitCondition`, `bWaiting`. For conditions: `Item`, `Comparison`, `Amount`, `Progress`, `MaxProgress`.
- world: `Docks`, `EnemyShips`, `EnemyStations` and `PlayerShips` arrays through `DMSWorldSubsystem:GetDMSWorld`.
- combat state: section 9.
- objects are found by id (`DMSWorldSubsystem.GetObject`). Do not keep references across save, load or sector unload. Fetch again by id.

## 7. Enemy ships

Enemies are `EnemyShipObject` (size `0x268`, super `EnemyBase`, size `0x158`). They do not use orders.

### 7.1 Enums

OBSERVED:

- `EEnemyShipTask`: 0 PATROL, 1 FOLLOW, 2 ATTACK, 3 SEEK, 4 WANDER, 5 STAGING.
- `EEnemyPathfinding`: 0 TOWARDS, 1 AVOIDING, 2 AWAY.
- `EWeaponType`: 0 BULLET, 1 MISSILE.
- `EDamageType`: 0 BASIC, 1 MINING, 2 LASER.
- `ESpawnReason`: 0 DEFAULT, 1 CONSTRUCTED, 2 PARENT.
- `EDroneState`: 0 DOCKED, 1 FLYING, 2 BUILDING, 3 RETURNING.
- `EAttackWaveStage`: 0 STAGING, 1 ATTACKING.

### 7.2 EnemyBase fields

OBSERVED. Enemy health is a plain i32. There are no shields and no fixed-point hull.

| Offset | Field |
|---|---|
| `+0x108` | DisplayName |
| `+0x118` | Health (saved) |
| `+0x11c` | MaxHealth |
| `+0x120` | SupplyCost (the value of this enemy in wave strength) |
| `+0x128` | DroppedItemClass |
| `+0x130` | DroppedItem (item stack) |
| `+0x140` | SphereColliders[] |
| `+0x150` | SpawnerID (saved) |

`bAddSystemPOI` adds a map marker (`WBP_SystemPOI_Enemy`).

### 7.3 EnemyShipObject fields

All OBSERVED.

| Offset | Field |
|---|---|
| `+0x158` | Target (a `Damageable` interface; weak ref, cached pointer at `+0x160`) |
| `+0x168` | TargetID |
| `+0x16c` | TargetChunk |
| `+0x178` | CurrentTask (byte) |
| `+0x17c` | DetectionDistance |
| `+0x180` | ReleaseTargetDistance |
| `+0x188` | Spawner (`EnemyStationObject`) |
| `+0x190` | AttackSpeed |
| `+0x194` | AvoidanceSpeed |
| `+0x198` | FatigueSpeed |
| `+0x19c` | PathfindingRotSpeed |
| `+0x1a0` | AvoidanceRotSpeed |
| `+0x1a4` | WorldImpulse |
| `+0x1b0` | ImpulseDrag |
| `+0x1b4` | BounceForce |
| `+0x1b8` | VerySmallNum |
| `+0x1bc` | PathfindingAction (byte) |
| `+0x1c0` | AvoidanceDistance |
| `+0x1c4` | AvoidFrames |
| `+0x1c8` | FlyAwayFrames |
| `+0x1cc` | Fatigue |
| `+0x1d0` | FatigueFrames |
| `+0x1d4` | TargetLocation |
| `+0x1e0` | TargetChunkLocation |
| `+0x1f0` | AttackWave |
| `+0x1f8` | AttackWaveID |
| `+0x1fc` | CurrentAttackPatternFrame (reused as a countdown) |
| `+0x200` | CachedVelocity |
| `+0x20c` | RequiredAlignmentToFire |
| `+0x210` | AlignmentToTarget |
| `+0x218` | BulletData |
| `+0x220` | TimeBetweenShots |
| `+0x224` | BulletSpeed |
| `+0x228` | BulletLifetime |
| `+0x22c` | AttackDistance |
| `+0x230` | AttackPattern[] (entries of `0x38` bytes) |
| `+0x240` | AttackPatternIndex |
| `+0x244` | LastShotTime |
| `+0x248` | Charge |
| `+0x24c` | Distance (to target) |
| `+0x250` | PatrolPath[] (`0xc` bytes each) |
| `+0x260` | PatrolPathIndex |
| after it | bFlyToDestination (bool) |

`AttackPatternEntry` (size `0x38`): `+0x0` WeaponType, `+0x8` BulletData, `+0x10` MissileClass, `+0x18` SpawnOffset, `+0x24` SpawnRotation, `+0x30` DelayFrames, `+0x34` ChargeFrames. The charge visual is the actor event `UpdateWeaponCharge`.

- UNKNOWN: the numeric defaults (speeds, ranges, attack patterns) live in the data-only Blueprint classes `BPO_RelicDestroyer` and `BPO_RelicTrident`. They were not decoded. Read them live.

### 7.4 The task machine

OBSERVED. The dispatcher runs once per enemy ship per tick:

| Task | What happens |
|---|---|
| ATTACK (2) | run the attack (section 7.6) |
| SEEK (3) | steer toward `TargetLocation` / `TargetChunkLocation`. Once per 30 frames per ship (staggered by ship id), try to acquire a target. On success the task becomes ATTACK |
| STAGING (5) | steer to the wave staging point. No target acquisition |
| PATROL (0), WANDER (4) | once per 30 frames per ship, try to acquire a target. On success the task becomes ATTACK. The dispatcher has no movement code for these |
| FOLLOW (1) | nothing |

- OBSERVED: the 30-frame check is `frame % 30 == ship ID % 30`, so each ship is checked once per 30 frames, and ships are spread across the frames.
- UNKNOWN: where PATROL and WANDER ships move. `PatrolPath`, `PatrolPathIndex` and `bFlyToDestination` exist, and a setup function builds the path, but the per-tick movement was not found.

### 7.5 Target acquisition

OBSERVED:

1. Take the enemy's solar system and walk its target list (INFERRED: the resolved enemy-targets array, ids in `SolarSystem +0x268 EnemyTargetIDs`; `PlayerTargetIDs` is at `+0x288`).
2. Skip a target more than 3 chunks away (Manhattan distance).
3. Compute the distance in fixed point, including the chunk difference.
4. If the distance is at most `DetectionDistance` (`+0x17c`): set `Target`, `TargetID` and `Distance`, tell the spawner, add this enemy to the target's `TargettedBy`, and return true.

The first match in array order wins. It is **not** the nearest target.

### 7.6 Attack and steering

OBSERVED, attack:

1. No target, or an invalid target: the task becomes WANDER.
2. Target dead, or `Distance` greater than `ReleaseTargetDistance` (`+0x180`): tell the spawner the target is lost, remove this enemy from the target's `TargettedBy`, clear `Target` and `TargetID`, set `Fatigue` to 0, task WANDER.
3. Steer toward the target.
4. Step the attack pattern. With `ChargeFrames` above 0: if `Charge` is above 0 it counts up, and above `ChargeFrames` it resets to 0, advances the pattern index (wrapping) and fires. Otherwise, if aligned (`AlignmentToTarget` above `RequiredAlignmentToFire`), `frame >= LastShotTime + DelayFrames` and `Distance < AttackDistance`, charging starts (`Charge` = 1). With no charge frames: if the same conditions hold, advance the index and fire.
5. Firing spawns a bullet (`BulletManager`, enemy `BulletData`) or a missile, by `WeaponType`. Damage goes through the `Damageable` interface (INFERRED: shields first, then hull).

OBSERVED, steering (the pathfinding action):

| State | Behaviour |
|---|---|
| TOWARDS (0) | speed is `AttackSpeed` while `Fatigue <= FatigueFrames`, else `FatigueSpeed`. Aim at the target. If `Distance < AvoidanceDistance`: state AVOIDING, countdown `AvoidFrames`, `Fatigue` = 0. `Fatigue` goes up by 1 each tick |
| AVOIDING (1) | rotate with `AvoidanceRotSpeed`, count down. At 0: state AWAY, countdown `FlyAwayFrames` |
| AWAY (2) | fly away from the target at `AvoidanceSpeed`, count down. At 0: back to TOWARDS |

Then `WorldImpulse`, `ImpulseDrag` and `BounceForce` are integrated, the position is written, and `AlignmentToTarget` is updated (reset value `0xffff0000`).

OBSERVED result: enemies fly strafing runs. They approach, break off when close, fly away, and return. Tiredness slows them.

### 7.7 Early warning of an attack

- OBSERVED: Blueprint events on the enemy actors: `OnTargetAquired`, `OnTargetLost`, `OnTargettedByPlayer`, `OnUntargettedByPlayer`, `OnWeaponFired`, `OnTakeDamage`, `UpdateWeaponCharge`.
- OBSERVED: polling `TargettedBy[]` on your ship, or `IsShipInCombat()`.

## 8. Enemy stations, waves and arena

### 8.1 Enemy classes

OBSERVED:

| Class | Size | Notes |
|---|---|---|
| `EnemyStationObject` | `0x1d0` | spawns patrol ships and attack waves |
| `EnemyHiveObject` | `0x108` | `+0x100` StationClass |
| `EnemyAsteroid` | `0x138` | super `AsteroidObject`; `+0x128` Distance, `+0x130` ShipClass (releases ships when approached) |
| `EnemyStationSpawner` | `0x118` | `+0x100` SpawnClass, `+0x108` ModulesToSpawn, `+0x10c` MaxSpawnAttempts, `+0x110` ModuleSpacing (world generation) |
| `ArenaWaveManager` | `0x190` | super `EnemyBase` |
| `AttackWave` | `0x160` | |
| `AttackManager` | `0x68` | saved root object, `+0x28` Waves[] |

Data-only Blueprint classes: `BPO_RelicDestroyer`, `BPO_RelicTrident` (enemy ships); `BPO_RelicShipyard_01/_02`, `BP_SmallRelicShipyardObject` (stations); `BPO_RelicStationSpawner_01` to `_04`; `BPO_SmallHive`; `BPO_EnemyAsteroid` (`_02`, `_03`). Crescent and Dirk ships have actors `BP_RelicCrescent` and `BP_RelicDirk` and objects `BP_RelicCrescentObject` and `BP_RelicDirkObject`. Drops: `BPO_RelicCoreDrop`, `BPO_RelicShardDrop` (floating ore).

### 8.2 EnemyStationObject

OBSERVED fields: `+0x158` ShipClass, `+0x160` SpawnOffset, `+0x16c` PatrolDistance, `+0x170` ConsumedPollution, `+0x178` Ships, `+0x188` ShipIDs, `+0x198` ConstructionProgress, `+0x19c` MaxConstructionProgress, `+0x1a0` MaxPatrollingShips, `+0x1a4` MaxActiveChildren, `+0x1a8` ActiveChildren, `+0x1ac` AttackWaveID, `+0x1b0` PollutionPerShip, `+0x1b4` InitialWaveStrength, `+0x1b8` SupplyPerWave, `+0x1bc` PollutionForNextWave, `+0x1c0` AttackWaveCooldown, `+0x1c4` AttackWavesSpawned, `+0x1c8` LastAttackWaveTime.

Each tick (OBSERVED):

1. If `ActiveChildren < MaxActiveChildren`, `ConstructionProgress` goes up by 1.
2. At `MaxConstructionProgress` the progress resets to 0 and a ship is spawned. The build bar on the actor follows (`UpdateConstructionProgress`).

Spawning a ship (OBSERVED): create `ShipClass` at the station location plus `SpawnOffset` rotated by the station rotation, register it, add it to `Ships` and `ShipIDs`, set up its patrol (`PatrolDistance`), set `Spawner` to the station, and add 1 to `ActiveChildren`. `MaxPatrollingShips` limits patrols.

Wave economy (names OBSERVED, flow INFERRED): machines and mined ore add pollution to the nearest enemy station (`AssemblerObject.PollutionProduction` at `+0x1c0`, `ManualAssemblerObject` at `+0x1ec`, `ReactorObject.PollutionRate` at `+0x1c0`, `SmallResourceAsteroidObject.ThreatPerItem` at `+0x174` with `ClosestEnemyStation` at `+0x180`). The station builds ships (`PollutionPerShip` each). When `ConsumedPollution` reaches `PollutionForNextWave`, and `AttackWaveCooldown` has passed since `LastAttackWaveTime`, it makes an `AttackWave` of strength `InitialWaveStrength` plus `SupplyPerWave` per wave already spawned. Each ship costs its `SupplyCost`. Wave ships get task STAGING.

- UNKNOWN: all these numbers. They are the values of the `BPO_RelicShipyard_*` classes. Read them live.

### 8.3 AttackWave and AttackManager

OBSERVED `AttackWave` fields: `+0xf8` Stage, `+0x100` TargetFrame, `+0x108` TargetFrameID, `+0x110` Ships[], `+0x120` ShipIDs[], `+0x130` Stations[], `+0x140` StationIDs[], `+0x150` CurrentStrength, `+0x154` WaveStrength, `+0x158` MinStartTime. `TargetFrame` is the player ship or station being attacked. Enemy ships point back through `AttackWave` and `AttackWaveID`.

INFERRED update: for each wave, when `Stage` is STAGING, `CurrentStrength >= WaveStrength` and the frame is at least `MinStartTime`, the stage becomes ATTACKING. The location and chunk of the `TargetFrame` are written into every wave ship's `TargetLocation` and `TargetChunkLocation`. The ships then go SEEK, acquire, ATTACK. `AttackManager` is reached through `DMSGameState.AttackManagerClass` (`+0x360`; `BP_AttackManager` is an empty subclass).

- UNKNOWN: which function creates `AttackWave` objects. Two candidates exist (see the table at the end). The exact `AttackManager` transitions are inferred only.

### 8.4 Arena mode and creative spawn

- OBSERVED: `ArenaWaveManager` fields: `+0x158` WaveSpawnRules[] (`ArenaWaveSpawnRule`: EnemyClass, FirstWave, NewEnemiesPerWave), `+0x168` CurrentWave, `+0x170` WaveEnemies[], `+0x180` MinSpawnDistance, `+0x184` MaxSpawnDistance, `+0x188` EnemiesSpawned. Function `OnWaveCompleted`. UI: `WBP_ArenaHUD`, `WBP_ArenaWaveSelect`, `WBP_ArenaStartMenu`, `WBP_ArenaShipSelectEntry`. `WorldGenParams.bArena` turns it on.
- UNKNOWN: the per-wave enemy count formula, and the `WaveSpawnRules` layout beyond the three names.
- OBSERVED: `CreativeEnemyInput` (command id 11, field `+0x40` TargetID) forces a wave against a frame. It is reachable from `WBP_StationInventory`. INFERRED: it needs creative mode. It is useful as a test harness on a throw-away save.
- OBSERVED world generation inputs: `WorldGenParams.EnemyLevel` (`+0x8`, default 0), `SectorGenParams.ThreatLevel` (`+0x48`), `SectorGenData.ThreatLevel` (`+0x10`), `EnemyDensity` (`+0x2c`), `EnemyEvolution` (`+0x30`), `StarCluster.EnemyHomeStar` / `EnemyHomeStarID` (`+0xb0` / `+0xb8`), `SolarSystem.FactionControl` (`+0xd4`).

## 9. Combat

### 9.1 Safe observation and safe actions

OBSERVED. Read only (poll from the 250 ms MSCore tick):

- `BuildingFrame.Health` (`+0x2e8`, i32), `Hull` (`+0x3d0`, PNum), `Shields` (`+0x2f0`, fixed 64-bit), `MaxShields` (`+0x2f8`), `DamageState.MinorDamage` (`+0x320`).
- `DMSPlayerShipObject.TargettedBy` (`+0x880`).
- UFunctions `GetCurrentHull`, `GetTotalHull`, `GetShields`, `GetMaxShields`, `IsShipInCombat`, `GetRepairItemsRequired`.

Actions, all through `AddInputCommand` (co-op safe):

| Command | Type id | Fields |
|---|---|---|
| `ShipFiringInput` | 60 | `+0x40` bPrimary, `+0x41` bSecondary |
| `FireGroupInputCommand` (`CreateInputSetFireGroup`) | 54 | `+0x40` WeaponID, bPrimary, bSecondary |
| `AbilityInput` (`CreateInputAbility`) | 52 | `+0x40` Slot, bNewTriggered |
| `RepairFrameInputCommand` (`CreateInputRepair(count, bCreative)`) | 56 | `+0x40` RepairItemCount, `+0x44` bCreative |
| `NewRepairInput` (`CreateInputNewRepair`) | 49 | `+0x40` RepairType name, `+0x48` Amount, `+0x4c` InventoryID |
| `SelfDestructInput` | 35 | |
| `CreativeEnemyInput` (`CreateInputCreativeEnemy`) | 11 | `+0x40` TargetID |

Controller functions: `AddWeaponInputCommand`, `AddCycleTargetCommand`, `AddRepairShipCommand`, `SetTargetInputDelay`, `SetActiveCombat`, `OnPlayerKilled`, `OnPlayerRespawned`, `ForcePlayerRespawn`. `DMSPlayerObject.bActiveCombat` (set by `SetActiveCombat`) drives combat music and UI (INFERRED).

- INFERRED: the simplest "ship under attack" signal is `IsShipInCombat()` plus a drop in `Health`. `IsShipInCombat` is true when `TargettedBy.Num() > 0`.
- OBSERVED: a probe showed `Health` 350 equal to `GetCurrentHull` 350, `MaxHealth` 500, `Hull` raw value 350 times 65536, `MaxShields` 0 (no shield generators), damage seen as Health 350 to 320, `MinorDamage` rising with hits, and `IsShipInCombat()` = 1 with `TargettedBy` listing the enemies while attacked.

{: .warning }
Sending `ShipFiringInput` every tick adds one command per frame to the sync buffer (INFERRED). Send on change only.

### 9.2 Hull, health and shields

OBSERVED `BuildingFrame` fields (ships and stations share them; saved fields marked S):

| Offset | Field |
|---|---|
| `+0x274` | PowerGridID |
| `+0x2c0` | Faction (S) |
| `+0x2d8` | SphereColliders (S), the hit volumes |
| `+0x2e8` | Health, i32 (S) |
| `+0x2f0` | Shields, fixed 64-bit (S) |
| `+0x2f8` | MaxShields |
| `+0x300` | FleetTagIDs (S) |
| `+0x310` | DamageDebuffs (S), array of {Amount i32, Type} |
| `+0x320` | DamageState {MinorDamage i32} (S) |
| `+0x340` | Inventory (repair items) |
| `+0x358` | BuildingBlocks |
| `+0x3d0` | Hull, PNum (S) |
| `+0x3d4` | Weight, PNum (S) |
| `+0x460` / `+0x470` / `+0x490` | DefaultAbilities / AbilityIDs (S) / Abilities (repair and missile abilities) |

- OBSERVED: `BuildingBlockObject`: `+0x154` Hull, `+0x158` Weight, `+0x15c` Thrust, `+0x180` Colliders, `+0x190` ShipCollisions. INFERRED: frame Hull is the sum of block Hull, and `GetTotalHull` is `Hull.Value >> 16`.
- OBSERVED: armour blocks (`BPO_Armour_01L/R`, `ArmourSlope_*`, `StationArmour_01/01B`) are pure data. They have no code.
- OBSERVED: the maximum health of a frame is `Hull.Value >> 16`. The repair code clamps Health to it.
- OBSERVED: `DMSPlayerShipObject.MaxHealth` (`+0x7ec`) is a separate class-default value used by the Health HUD (probe: 500 against a hull of 350).
- OBSERVED: `Hull` and `Shields` are fixed point. `Hull.Value` is the whole number times 65536. `GetShields` already returns a converted double. Do not use the raw hull as a percent base.
- OBSERVED: UFunctions on the frame: `GetCurrentHull` (int), `GetTotalHull` (int), `GetShields` (double), `GetMaxShields` (double), `GetFaction`, `GetInventory`, `GetBlueprint`, `GetCollisionEnv`, `GetPowerGrid`, `GetDistanceToSun`, `IsFleetTagVisible`, `OnLowPower`, `CanDisassembleFrame`.
- OBSERVED: `ShieldGeneratorObject` (size `0x1b8`): `+0x1a8` Capacity i32, `+0x1ac` Recharge, `+0x1b0` PowerDraw. Blocks: `BPO_ShieldGenerator_01` (station) and `BPO_ShipShieldGenerator_01` (ship).
- INFERRED: `MaxShields` is the sum of the generators' `Capacity`, and `Shields` refills by the sum of `Recharge` while powered.
- UNKNOWN: the shield aggregation code.

HUD facts (OBSERVED, from the widget dumps): `WBP_ShipHUD_Health` shows Health / MaxHealth as a percent. `WBP_ShipStatus.RefreshHealth` shows `GetCurrentHull` over `GetTotalHull` plus the repair item text from `GetRepairItemsRequired`. `WBP_ShipHUD_Shields` uses `GetShields` over the larger of `GetMaxShields` and 1. The two health pairs exist and can differ (INFERRED).

### 9.3 Damage model

- OBSERVED: damage sources are `BulletData.Damage` (cannon, turret and enemy bullets), `LaserTurretObject.Damage` every `DamageInterval`, and `MissileObject.DamageAmount` within `DamageRadius`. Missiles themselves can be shot (they have Health).
- INFERRED: collisions do no damage (`BounceForce` only bounces).
- OBSERVED: `DamageRegistry` and `DamageTypeRegistry` exist.
- UNKNOWN: per-type multipliers.
- OBSERVED: `DamageState.MinorDamage` goes up with hits. Debuff assets: `DAT_MinorDamage`, `DAT_MajorElectricalDamage`, `DAT_MajorHullDamage`. `DAT_DamageRegistry` holds `MinorDamageRepairItem` (`+0x30`) and `MinorDamageAmount` (`+0x38`).
- INFERRED: minor hits add up into major electrical or hull debuffs that weaken systems until repaired.
- UNKNOWN: the thresholds, hit resolution, armour and shield ordering (shields first is INFERRED), falloff, and the friendly-fire rule. This logic is native code reachable only from the world tick.
- OBSERVED: damage reaches actors as Blueprint events: `OnTakeDamage` on `Enemy`, `DMSPlayerShip` and `StationPlatform`, and `DamageDestroy`. Destruction spawns `BP_Explosion` and a sound. The simulation removes the object natively. Enemy drops come from `DroppedItemClass` and `DroppedItem`.
- INFERRED: these events fire after the simulation applied the damage. They are good for "was hit" detection, with the hit actor as `self`. Their signatures are UNKNOWN, so probe with a parameter-agnostic hook first. No damage amount is seen.

### 9.4 Repairs

OBSERVED:

- Items needed = `ceil(missing / 10)`, where `missing = (Hull.Value >> 16) - Health`. Examples: missing 0 gives 0, 1 gives 1, 10 gives 1, 11 gives 2, 30 gives 3. **One repair item restores 10 Health.**
- `RepairFrameInputCommand` (receiver = ship id): `needed` is as above. Unless `bCreative`, the count is the smallest of the requested `RepairItemCount`, `needed` and the repair items in the frame inventory (`+0x340`). The ship is healed by count times 10, the items are removed (the item is `RepairItem` at `+0x7f0`), and Health is clamped to the hull.
- The game's own button sends `CreateInputRepair(1, false)` per click.
- `NewRepairInput` repairs debuffs. It spends the debuff type's `RepairItem` (`+0x48` on `DamageDebuffType`) from the inventory. `WBP_ShipDamageReport.Refresh` sends it.
- `RepairAbility` fields: bTriggered, `+0x84` DelayFrames, `+0x88` RepairAmount, `+0x8c` ChargesPerItem, `+0x90` RepairItem, `+0x98` RepairCharges, `+0x9c` LastRepairFrame.

### 9.5 Weapons

All OBSERVED. Saved fields marked S.

**WeaponObject** (size `0x1d0`): `+0x1a8` DisplayName, `+0x1b8` Ship, `+0x1c0` ShipID (S), flags bFiring, bAddToPrimaryGroup, bAddToSecondaryGroup, bAutoFire (S), `+0x1c8` Charge (S), `+0x1cc` Capacity.

| Class | Size | Fields |
|---|---|---|
| `CannonObject` | `0x200` | `+0x1d0` BulletData, `+0x1d8` BulletLifetime (ticks), `+0x1dc` BulletSpeed, `+0x1e0` TimeBetweenShots (ticks), `+0x1e4` LastShotTime (S), `+0x1e8` PowerPerShot, `+0x1ec` PowerDraw, `+0x1f0` BulletSpawnOffset |
| `TurretObject` (super Cannon) | `0x218` | `+0x200` Target, `+0x208` TargetID (S), `+0x20c` ChunkDetectionRange, `+0x210` FiringArc. Turrets aim and fire by themselves (`BPO_StationTurret_01/_01B`, `BPO_ShipTurret_01`) |
| `LaserTurretObject` | `0x1f8` | bFiringBeam, `+0x1d0` Damage i32, `+0x1d4` DamageInterval, `+0x1d8` Cooldown (S), `+0x1e0` Target, `+0x1e8` Range, `+0x1ec` ChunkRange, `+0x1f0` FiringArc |
| `MiningLaserObject` | `0x1e8` | `+0x1d0` LaserReachMeters, `+0x1d4` Damage, `+0x1d8` DamageTime, `+0x1dc` Progress, `+0x1e0` PowerDraw. It damages asteroids (`ResourceObject`: `+0x168` HealthPerOre, `+0x170` ResourceMaxHealth, `+0x174` ThreatPerItem) |
| `MissileLauncherObject` | `0x248` | `+0x1d0` MissileClass, `+0x1d8` SiloRotation, `+0x1e4` ReloadTime, `+0x1e8` FiringDelay, `+0x1ec` LastShotTime (S), `+0x1f0` Silos (S, each {Offset, LastShotTime}), `+0x200` MissileItem (ammo), `+0x208` MissileRange, `+0x220` AbilityClass, `+0x228` Inventory, `+0x230` InventoryID (S), `+0x238` FrameInventory, bAutoInput (S) |
| `MissileObject` | `0x150` | `+0x100` Target, `+0x108` TargetID (S), `+0x10c` Lifetime (S), `+0x110` MaxLifetime, `+0x114` DamageRadius, `+0x118` DamageAmount, `+0x11c` Speed (S), `+0x120` MinSteeringSpeed, `+0x124` MaxSpeed, `+0x128` Acceleration, `+0x12c` TurningSpeed, `+0x130` TargetLocation (S), `+0x13c` TargetChunkLocation, `+0x148` Health (S), `+0x14c` MaxHealth |

- OBSERVED: active missiles are in `SolarSystem.Missiles` (`+0x298`).
- OBSERVED: `ShipWeapon` struct: Class, bAutofire / bPrimary / bSecondary (S), LocalLocation, LocalRotation, ID (S). `MissileAbility` holds Launchers and LauncherIDs.
- OBSERVED: weapon actors (`BP_BasicCannon`, `_B`, `BP_BasicLance`, `BP_CannonTurret`, `BP_LaserTurret`, `BP_StationTurret_01B/T`, `BP_MissileLauncher_01`) only show the `OnFire` effect.
- OBSERVED: weapon block Blueprints: `BPO_CannonBlock(_B)`, `BPO_MiningLaserBlock(_B)`, `BPO_MissileLauncherBlock_01`, `BPO_ShipLaserTurretBlock_01`, `BPO_ShipTurret_01`, `BPO_StationTurretBlock_01`, `BPO_StationTurret_01/_01B`, `BPO_ShipShieldGenerator_01`. Machine classes: `BPO_LaserTurret`, `BPO_MissileLauncher_01`, `BPO_Missile_01`, `BPO_RelicMissile_01`. Relic weapons: `BPO_RelicPulseCannon_L/R`. Abilities: `BPO_MissileAbility`, `BPO_RepairAbility`.

### 9.6 Bullets

OBSERVED:

- `BulletData` (data asset, size `0x58`): `+0x30` Name, `+0x38` BulletActor class, `+0x40` Damage i32, `+0x44` Radius, `+0x48` Speed, `+0x4c` Lifetime, `bPoolActors`. Assets are under `Content/Data/Bullets/DAT_{Player,Enemy,Crescent,CrescentCharged,Lance,StationTurret,Trident}Bullet`. They are registered at boot through `DMSRegistry.RegisterBullets`.
- Bullet actors: `BP_PlayerBullet`, `BP_EnemyBullet`, `BP_CrescentBullet`, `BP_CrescentChargedBullet`, `BP_LanceBullet`, `BP_TridentBullet`. Each is a plain actor with 2 static meshes.
- `BulletManager` (saved root object, size `0x98`): `+0x68` SolarSystem, `+0x70` Bullets (S), `+0x80` BulletPools, `+0x90` BulletActorClass. One per solar system (`SolarSystem.BulletManagerID` at `+0x250`).
- `Bullet` struct (size `0x50`): `+0x0` BulletData, `+0x8` Velocity (S), `+0x14` Location, `+0x20` Quat, `+0x30` ChunkLocation, `+0x3c` RemainingLifetime (S), `+0x40` InstigatorID (S), `+0x48` Actor (pooled draw actor, not saved).
- Bullets are plain simulation data. There is no UObject per bullet. The actor pool only draws them (`BP_BulletImpact`, `BP_ShieldImpact` are effects).
- INFERRED: the hit test is against `SphereColliders` (frames at `+0x2d8`, `EnemyBase` at `+0x140`) in chunk space. `InstigatorID` gives damage credit or a faction filter.
- INFERRED damage values read from the raw data (treat as unconfirmed until read live): `DAT_PlayerBullet` 2, `DAT_EnemyBullet` 1, `DAT_LanceBullet` 20 (0x14), `DAT_CrescentBullet` and `DAT_CrescentChargedBullet` 1. Radius word `0x300` everywhere, units UNKNOWN.
- OBSERVED: speed and lifetime for most bullets live on the weapon (`CannonObject.BulletSpeed`, `BulletLifetime`) or in the enemy attack pattern.

### 9.7 Targeting and fire groups

Player ship fields (OBSERVED, `DMSPlayerShipObject`; saved marked S):

| Offset | Field |
|---|---|
| `+0x778` | WeaponSpawnData |
| `+0x788` | Weapons |
| `+0x798` | WeaponIDs (S) |
| `+0x7a8` | ActiveWeaponIndex (S) |
| `+0x7b0` | Reactors |
| `+0x7d0` | UpgradeMachineIDs |
| `+0x7ec` | MaxHealth |
| `+0x7f0` | RepairItem |
| `+0x7f8` | Target |
| `+0x800` | TargetID (S) |
| `+0x804` | ChunkDetectionRange |
| `+0x808` | TargettingRange |
| `+0x80c` | MaxTargettingAngle |
| `+0x810` | ValidTargets |
| `+0x820` | ValidTargetIDs (S) |
| `+0x830` | CurrentTargetIndex (S) |
| `+0x838` | ShipWeapons (S) |
| `+0x848` | PrimaryFireGroup |
| `+0x858` | SecondaryFireGroup |
| `+0x870` | RebuildFireGroups delegate |
| `+0x880` | TargettedBy (Num at `+0x888`) |

Also bPrimaryWeaponsSelected (S).

- OBSERVED: `ValidTargets` is rebuilt from a chunk scan within `ChunkDetectionRange`, `TargettingRange` and `MaxTargettingAngle`. `CurrentTargetIndex` is cycled by `AddCycleTargetCommand` and `SetNewTarget` (actor). The HUD lock marker uses `Enemy.OnTargettedByPlayer` and `OnUntargettedByPlayer`.
- INFERRED: player cannons fire along the ship heading. Only missiles home. Turrets acquire enemies by themselves.
- OBSERVED: `TargettedBy` on the **target** frame is filled by enemies. Num above 0 means in combat. It feeds `WBP_CombatWarning`, `WBP_DamageDirectionIndicator` and `CharacterHUD.CreateDamageDirectionWidget`.
- OBSERVED: fire groups are set with `FireGroupInputCommand` (weapon id, primary, secondary). The UI is `WBP_FireGroupConfig`, `WBP_ShipWeapon`, `WBP_WeaponsPrimary`, `WBP_WeaponsSecondary`.
- OBSERVED: `SolarSystem` fields used by combat: `+0x250` BulletManagerID, `+0x268` EnemyTargetIDs, `+0x288` PlayerTargetIDs, `+0x298` Missiles, `+0x2a8` ClearProgress {ClearedSectors, EnemySectors}.

## 10. Drones

OBSERVED properties. INFERRED behaviour. The drone tick was not traced (UNKNOWN).

- `DroneBayObject` (size `0x218`): `+0x1b0` Inventory, `+0x1c0` DroneClass, `+0x1c8` Docks[], `+0x1d8` TaskQueue[], `+0x1e8` TaskQueueIDs[], `+0x1f8` DroneRange, `+0x200` Filters[], `+0x214` CollectFromMinerTimer, `bAutoCollectMiners` (set by `StationConsoleInput.SetAutoCollectFromMiners`). Event `OnNewAvailableTask`. The bay is a task dispatcher (build, collect from miner).
- `DroneObject` (size `0x1d0`): `+0xf8` CurrentTask, `+0x128` DockingPath[], `+0x140` Inventory, `+0x150` InventorySize, `+0x158` State, `+0x160` WaitAmount, `+0x170` WaitAmountProgress, `+0x180` to `+0x1a0` pathfinding tuning (ArrivalDistance, PathfindingSpeed, SlowdownDistance, OffsetAboveDock), `bForceReturn`, `bMarkerEnabled`. Delegates `OnDroneStateChanged`, `OnDroneInventoryChanged`.
- `DroneTask` (size `0x138`): `+0xf9` TaskType, `+0x100` TargetObject, `+0x108` ID, `+0x110` RequiredItems[], `+0x120` MissingBuildItems[], `+0x130` MaxProgress, flags bAutomatic, bUseBounds, bUseTargetLocation, bTaskCompleted.
- `MiningDroneObject` (size `0x220`, `BPO_MiningDrone`): `+0xf8` InventorySize, `+0xfc` MiningTime, `+0x100` MiningDistance, `+0x104` DroneUnloadingRate, `+0x124` TargetResourceID, `+0x128` Progress, `+0x138` TargetResource, flags bWorking, bDocked, bWithinMiningRange, steering values (SeekSteering, AvoidanceSteering, MaxVelocity, SeekForce, AvoidanceForce, bUseBoidMovement).
- `MiningDroneBayObject` (size `0x280`): `+0x1a8` DroneClass, `+0x1b0` DroneDocks[], `+0x1c0` Range, `+0x1c4` InventorySize, `+0x1c8` PowerConsumptionRate, `+0x228` DroneIDs[], `+0x240` FilterItem, `+0x250` Resources[], `+0x268` MineableItemTotals[], bWorking, bResourceScanComplete.
- Ship-mounted drones: ship `+0x6d8` Drones[], `+0x700` TaskQueue[], `+0x720` DroneDocks, `+0x730` DroneRange. `DroneTaskInput(MinerID)` means "collect from this miner". `CollectOrder` is the order form.
- UNKNOWN: the `DroneTask` type values (one is REPAIRSTATION).

## 11. Dialogue NPCs (not crew)

- OBSERVED: `BPO_Crew`, `BPO_Captain`, `StationCommander`, `Lounge1/2`, `Mechanic`, `MilitaryLiaison`, `RequisitionOfficer`, `ShipwrightVendor`, `InventorVendor`, `Botanist`, `Armourer`, `CommunicationsOfficer` and `TutorialBriefing` are `VendorObject` (size `0x218`). Fields: `+0x1a8` Identity, `+0x1b0` DisplayName, `+0x1c0` MissionClasses[], `+0x1d0` MissionIndex, `+0x1d8` Mission, `+0x1e0` MissionID, `+0x1e4` InteractionCount, `+0x1e8` DialogueNodes[], `+0x1f8` VendorTechnologies[], `+0x208` DialogueTree, `bRepeatLastMission`.
- OBSERVED: interaction goes `BPC_MachineInteraction.Interact`, then `WBP_NPCDialogue`, then `InteractInput.Interact` (command id 18), then the simulation handles dialogue and mission accept (`MissionInput.AcceptMission`).
- OBSERVED: NPCs are static. They do not move and have no schedule.
- OBSERVED: missions use objective types TRIGGER, CRAFT, BUILD, RESEARCH, LOAD and UNLOAD. Load and unload objectives mirror the order conditions.

## 12. What mods may do

Safe (OBSERVED unless noted):

1. Send commands with `AddInputCommand`: order creation and update, autopilot, skip, remove, reorder (with a true permutation), docking, repair, firing, fire groups, abilities, target cycling.
2. Call the controller functions in section 5.7. They only queue commands.
3. Read ship, order and combat state, and poll from the 250 ms tick.
4. Hook Blueprint actor events (`OnTakeDamage`, `OnTargetAquired`, `OnTargetLost`, `OnTargettedByPlayer`, `OnWeaponFired`, `UpdateWeaponCharge`, `StationPlatform:OnTakeDamage`, `DamageDestroy`, `Weapon:OnFire`). These are effect events only.
5. `WBP_ShipHUD_Health:RefreshHealthText` fires on health change, for the player ship only. `WBP_ShipStatus:RefreshHealth` is also visible as a damage-tick proxy for the player ship. Health HUD widgets refresh on `EWidgetRefreshType` 3 (HEALTH). INFERRED: damage broadcasts it.
6. A C++ mod with native hooks, guarded by the exe size and a hash (see the table).

Native hook targets, and ideas (INFERRED):

- `DeliverOrder::Update`: a custom arrival or dock policy.
- `TryAcquireTarget`: return false so enemies ignore a ship, or choose the nearest target.
- `EnemyStation::Update`: skip it to stop spawns.
- A new order class is not possible.
- Damage application sites are not confirmed. Hook only after tracing.

## 13. Gotchas

- OBSERVED: `ConfigureShipOrdersInput.ReorderedIndices` is used with no check. Send a true permutation of 0 to Num-1, or the game reads out of bounds and crashes.
- OBSERVED: a destroyed dock leaves a `DeliverOrder` returning false forever. The ship hovers and never advances. A mod that removes docks must delete the order or retarget it (`UpdateShipOrderInput` with a valid dock id).
- OBSERVED: a command with type id (`+0x28`) 0 is dropped at flush. Factory-created commands set it. A command made with `NewObject` needs it written by hand (`ShipMovementInputCommand` is `0x3a`).
- INFERRED: `NewShipOrderInput` with a class that is not a `ShipOrderBase` subclass crashes the simulation. Use only the seven order classes (the UI uses five).
- OBSERVED: creating many orders in one tick spams widget rebuilds.
- INFERRED: `DockCommand` (`bDockingAutopilot`) does not use the dock queue. Two ships sent to one dock this way can both arrive, and the second dock attempt fails. Check dock state yourself.
- INFERRED: a far dock in another sector starts its warp about 7 seconds after autopilot is switched on and takes warp fuel in one lump.
- OBSERVED: a `DeliverOrder` alone does not move the ship until autopilot is on and the index points at it. With 1 order the ship idles after it finishes.
- OBSERVED: do not call `ExecuteUbergraph_*` from Lua, and do not hook it. Do not call Blueprint custom events or stubs on a class default object or on an object without an `UberGraphFrame`.
- OBSERVED: do not run `FindFirstOf` from a delayed callback after the action ended, and do not keep Lua references to closed windows.
- OBSERVED: do not spawn enemies or change weapons, targets or health by writing simulation arrays. It desyncs co-op. `BulletManager` and `SolarSystem` arrays are walked natively. INFERRED crash causes: a stale `Bullet.Actor`, or an `EnemyShips` index.
- OBSERVED: `AttackWave.Ships` and `EnemyStationObject.Ships` hold raw pointers rebuilt from the id lists at load. Never keep them across save or load.
- OBSERVED: bullets have no UObject, so `FindAllOf` cannot find them. Only the pooled `BP_*Bullet` actors exist, and they are reused when `bPoolActors` is set.
- OBSERVED: `AddCycleTargetCommand` and `AddRepairShipCommand` share one native thunk. An address hook fires for both. Use the UFunction path.
- OBSERVED: the UFunction hooks "damage" do not see the real damage code. It is native and runs from the world tick, not through the UFunction call path.

## Build-specific reference

| Item | Value |
|---|---|
| Image base | `0x140000000`. All addresses below are virtual addresses |
| Sim world tick | `0x144267910` |
| Command processor | `0x144269b40` |
| Ship tick | `0x144289b70` |
| Ship `ApplyCommand` | `0x144287960` (calls the `BuildingFrame` base `0x144276480` first) |
| Ship `GetCurrentOrder` | `0x144286cb0` |
| `FlyTowards` | `0x144285240` |
| `PrepareJump` | `0x144286ce0` |
| `DeliverOrder::Update` | `0x144298b60` |
| `WaitOrder::Update` | `0x144298f00` |
| `WaitForTimeOrder::Update` | `0x144298ee0` |
| `LoadItemsOrder::Update` | `0x144298d00` |
| `UnloadItemsOrder::Update` | `0x144298e90` |
| `CollectOrder::Update` | `0x144298950` |
| `TutorialShipOrder::Update` | `0x144298d50` |
| `DeliverOrder` `ApplyCommand` | `0x144297e30` |
| `WaitOrder` / `WaitForTime` / `LoadItems` / `UnloadItems` / `Collect` `ApplyCommand` | `0x1442981d0` / `0x144298170` / `0x1442980b0` / `0x144298110` / `0x1442984a0` |
| `DeliverOrder` `ResolveIDs` / `OnExit` / `AttachToShip` | `0x144298510` / `0x144298440` / `0x1442988e0` |
| `DeliverOrder` load / load and resolve | `0x144298750` / `0x144297820` |
| `ShipOrderBase::SetCurrent` | `0x1442988d0` |
| `Dock::ReserveQueueSlot` | `0x14428e5b0` |
| `Dock::RemoveFromQueue` | `0x14428e960` |
| `DockAt` | `0x14428c350` |
| `SetDockingAutopilot` | `0x14428c260` |
| `SetAutopilot` | `0x14428c130` |
| `RemoveOrder` | `0x14428bb20` |
| `SkipToOrder` | `0x14428d170` |
| `Reorder` | `0x14428bf00` |
| `ChangeThrottle` | `0x144283910` |
| `ThrottleStep` | `0x144283d10` |
| Warp cost function | `0x1442871a0` |
| Controller `AddInputCommand` | thunk `0x144211920`, native `0x1442609d0` |
| `SetShipAutopilot` | thunk `0x144213930`, native `0x144263bb0` |
| `SkipToOrder` (controller) | thunk `0x144213e20`, native `0x144263df0` |
| `DeleteOrder` | thunk `0x1442126c0`, native `0x144261ce0` |
| `ReorderShipOrders` | thunk `0x144212fb0`, native `0x144263570` |
| `DockCommand` | thunk `0x144212790`, native `0x144261d90` |
| Dead stub (nop) | `0x14419e010`, thunk `0x144212f20` |
| Command factories | `NewShipOrder` `0x1441a3900`, `UpdateShipOrder` `0x1441a3d30`, `RemoveShipOrder` `0x1441a3a50`, `ReorderShipOrder` `0x1441a3b00`, `SkipToShipOrder` `0x1441a3c90`, `SetAutopilot` `0x1441a3be0`, `MiningAutopilot` `0x1441b1400`, `Docking` `0x1441fc100` |
| Combat command factories | `CreateInputSetFireGroup` `0x1441f8d30`, `CreateInputAbility` `0x1441f9900`, `CreateInputRepair` `0x1441f8e90`, `CreateInputNewRepair` `0x1441f89c0`, `CreateInputCreativeEnemy` `0x1441b2e20` |
| Enemy dispatcher | `0x144227e60` |
| `TryAcquireTarget` | `0x144234a60` |
| Enemy `Attack` | `0x1442237a0` |
| Enemy `Fire` | `0x144228d10` |
| Enemy `Steer` | `0x1442298f0` |
| Enemy away vector | `0x144234620` |
| Enemy `SetupPatrol` | `0x144235870` |
| Enemy position write | `0x14425f460` |
| `EnemyStation::Update` | `0x144290ab0` |
| Station `SpawnShip` | `0x144291910` |
| `GetClosestEnemyBase` / `GetClosestPlayerStation` | `0x144290f20` / `0x144291130` |
| `AttackManager::Update` | `0x1442714f0` |
| `AttackWave` creator candidates | `0x144238370`, `0x144226420` |
| `IsShipInCombat` | `0x1442892f0` |
| `GetRepairItemsRequired` | `0x144287400` |
| Repair handler region | `0x144288197` to `0x144288292` |
| `GetSimulationObject` | thunk `0x1441bd370`, native `0x14425e860` |
| Class sizes and vtables | `DMSPlayerShipObject` `0x9b0`, vtable `0x147328d88` (INFERRED: `0x147328550` also appears); `ShipOrderBase` vtable `0x14737bf48`; `DeliverOrder` `0x14737c598`; `WaitOrder` `0x14737c8c0`; `WaitForTimeOrder` `0x14737d238`; `LoadItemsOrder` `0x14737cbe8`; `UnloadItemsOrder` `0x14737cf10`; `CollectOrder` `0x14737c270`; `TutorialShipOrder` `0x14737d560`; `EnemyBase` `0x14733b020`; `EnemyShipObject` `0x14733b748`; `EnemyStationObject` `0x14733bea0`; `EnemyHiveObject` `0x14733b3c8`; `EnemyStationSpawner` `0x14733c230`; `ArenaWaveManager` `0x14733c590`; `AttackWave` `0x1472f1a10`; `AttackManager` `0x1472f2020`; `Formation` `0x1473433a8`; `CannonObject` `0x1473953b0`; `TurretObject` `0x1473963b0`; `LaserTurretObject` `0x147396c80`; `MissileObject` `0x147397550`; `MiningLaserObject` `0x147395ae0`; `ShieldGeneratorObject` `0x14737a1f0`; `BulletManager` `0x147302d68`; `FactionManager` `0x147342fa0`; `StationObject` `0x14738cd70` |
| Weapon update batch vtables | `WeaponUpdate` `0x147395818`, `AttackWaveUpdate` `0x1472f1d58`, `ShieldGenUpdate` `0x14737a630`, `MissileUpdate` `0x1473978b8` |
| Damage write candidates (not verified) | `0x14425b0b0`, `0x14425cfc4`, `0x14425d1cd`, `0x1442615dd`, `0x1442690ec`, `0x144274c22`, `0x1442774d1`, `0x14427b21d`, `0x144280ca0`, `0x1442816d0`. Only `0x144288292` (repair clamp) is verified |
| Heal helper | `0x14427acc0` (body unknown) |

Offsets in the other sections are not build specific in the same way, but they also change when the game updates. Re-check them after every game update.

## Unknown

- Where PATROL and WANDER enemy ships move (section 7.4).
- The numeric defaults of enemy ships and stations (speeds, ranges, attack patterns, wave sizes, pollution numbers), and the damage, radius and speed of each bullet and missile. They must be read live.
- Which function creates `AttackWave` objects, and the exact `AttackManager` transitions.
- The function that applies damage, the armour and shield order, falloff, friendly fire, and the `MinorDamage` thresholds.
- How shield generators add up into `Shields` and `MaxShields`.
- The drone tick (state changes and task assignment), and the `DroneTask` type values.
- The full `CollectOrder` flow, and what `TutorialShipOrder` flies to.
- What `PrepareJump` does beyond setting the jump target for far docks.
- Which `SolarSystem` field is at `+0x258`, and what fills it.
- The `WaitForTimeOrder` unit (frames, with a UI factor that was not decoded).
- The save and load roles of virtual slots 92, 93, 99 and 100 on orders.
- The `Damageable` interface method list.
- The `ArenaWaveManager` per-wave enemy count formula and the `WaveSpawnRules` layout.
- The numeric values of some enums on the player-side combat code were read from the enumerator tables (section 7.1). Treat the order of any other enum as unknown until checked.
