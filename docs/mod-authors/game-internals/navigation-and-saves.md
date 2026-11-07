---
title: Navigation, warp and saves
parent: Game internals
nav_order: 6
---

# Navigation, warp and saves

Notes from reading the game and testing mods in it. Each claim is tagged:

- **OBSERVED**: read in the game's code or data, or measured on real saves.
- **INFERRED**: follows from what was read, not tested.
- **UNKNOWN**: not found yet.

Build these notes apply to: `MainSequence-Win64-Shipping.exe`, exe size `149200896`, Unreal Engine 5.7. Addresses and offsets are listed only in the table at the end, except where an object offset is the only way to name a field. They are build specific and change with every game update.

## 1. How ships move

- OBSERVED: everything that moves a ship is native code in `/Script/MainSequence`, on the sim object `DMSPlayerShipObject`. Player ships and AI ships are the **same class**. The pilot only decides who sends the commands.
- OBSERVED: the ship Blueprints (`BP_Scout`, `BP_Prospector`, `BP_Gallant`, `BP_PilotConsole`, `BP_PlayerController`) only wire input and play audio and particles.
- OBSERVED: every state change goes through an input command. The path is: UI or Lua, then `DMSPlayerController`, then `AddInputCommand(receiverId, cmd)`, then the controller's queue, then the sim's command processing, then the ship's command handler.
- OBSERVED: the sim is lockstep (see [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %})).

{: .warning }
Do not write ship properties (throttle, dock, jump target) from a mod. A local write changes only your copy. Other peers do not see it, the sim overwrites it, and a co-op session desyncs. Send commands.

## 2. Sectors, coordinates and chunks

- OBSERVED: a position is a **chunk** (three integers) plus a **local position** inside the chunk. A chunk is 2048 m on a side. Raw units are fixed point with 16 fractional bits, so one chunk is `2^27` raw units and `65536` raw is 1 m.
- OBSERVED: every object stores a chunk, a local position, a rotation (quaternion) and a solar system id.
- OBSERVED: a chunk delta between two points is `(chunk difference << 27) - own position + target position`. The game uses this exact form while the chunk distance is under 5. Above that it uses a coarser path.
- OBSERVED: chunk distance is the sum of the absolute differences on the three axes (a Manhattan distance, not a straight line).
- OBSERVED: the sector hierarchy is `StarCluster`, `SolarSystem`, then sector, planet and belt, then `Chunk`, then objects. The save tree follows it (INFERRED for the exact nesting).
- OBSERVED: sectors have generation parameters. They can override the default warp arrival distance. Rich sector `1008` uses `91750400` raw (1400 m).
- OBSERVED: streaming centres on the **local player object**.
- OBSERVED: a ship that is far from the player has no loaded rocks around it unless a chunk hold is used (section 8).

## 3. Ship state

Main fields on `DMSPlayerShipObject`. Offsets are in the reference table at the end.

| Field | Meaning |
|---|---|
| `Throttle` | Integer, clamped to -10 to 10. |
| `LastThrottleUpdateFrame` | Frame of the last throttle step. Drives the cooldown. |
| `FlightModel` | Pointer to the flight model data (section 4). |
| `StrafeImpulse`, `RotationImpulse` | Written by the command handler. Rotation per axis is clamped to +-10000 raw. |
| `bAutopilot`, `CurrentOrderIndex`, `Orders` | Autopilot flag and order list. |
| `bJumpTargetSet`, `bAlignedWithJumpTarget`, `bInsufficientWarpFuel` | Warp arming state. |
| `JumpTargetID`, `JumpTargetLocation` | The armed target. |
| `bTravelling` | True while in warp travel. |
| `JumpAnimProgress` | `0` idle, `1` or more while charging. |
| `JumpProgress`, `JumpArrivalProgress` | Warp travel progress. |
| `ChargeToJump`, `RequiredChargeToJump` | Charge counters. |
| `WarpDrive`, `WarpDriveID` | The fitted warp drive. `CanWarp()` is true when the pointer is not null. |
| `Dock`, `DockID`, `TargetDockID`, `bDockingAutopilot` | Docking state. |
| `Pilot`, `PilotID` | When the pilot id equals the controller id, the player flies the ship. |
| `Health` | An integer. At less than 1 throttle changes are ignored. |

- OBSERVED: the test for a dead or removed object (dock or any other) is bit 30 of the flags word at `+8`: `(obj+8 >> 30) & 1`.
- OBSERVED: a dock pointer can dangle (pending kill) until the ship tick clears it. Always test that bit.

## 4. Flight model data

- OBSERVED: there is one flight model, `DAT_SmallShipFlightModel`. Every ship uses it. Values below were read from the data asset (fixed point raw / 65536).
- OBSERVED: a pak override of that asset changes the speed cap for **all** ships. A runtime write of the pointer on one ship is local only (fine solo, desync in co-op).

| Group | Values |
|---|---|
| Throttle | ThrottleInputDelay 3 frames, GlobalMaxSpeed 200 m/s at throttle 10, ThrustMultiplier 1.0, AccelerationMultiplier 1.0, MaxImpulse 1.0, ImpulseMultiplier 0.125, Drag 0.031, BounceForce 1.25, MaxLowPowerSpeed 0.333, LowPowerThrottle 5 |
| Steering | MaxSteeringDegrees 40, DeadZoneDegrees 4, SteeringResponse 5.333, SteeringMultiplier 1.0, SteeringInterpStep 4.0, RotSpeed 0.031, MaxRotationImpulse 2.7, RotationImpulseForce 0.084, AlignmentTolerance 0.003 |
| Boost | BoostSpeed 8.0, BoostDragMultiplier 0.25, MinBoostIgnition 30, MaxBoostCharge 180 |
| Autopilot | AutopilotImpulseMultiplier 0.375, AutopilotMinThrottle 3, AutopilotRotationImpulseForce 0.021, AutopilotBankFactor 150, AutopilotExitDockTime 45 frames, AvoidanceQuerySize 40 m, AvoidanceQueryDist 50 m, AvoidanceQueryHeightOffset 20 m, AvoidanceTime 8, MiningDistance 50 m, AlignmentAdjustmentRadius 500, FinalApproachProximity 40 |
| Warp | WarpSpeed 300, SlowWarpSpeed 100, JumpAnimLength 30, WarpArrivalDistance 400, WarpArrivalChunkDistance 5, ExitWarpDistance 750, MinChunkDistanceToJump 20, RequiredAlignmentToJump 0.995 (65236 raw), RequiredChargeToJump 90 (see below), WarpFuelConsumption 1.0 |

- OBSERVED: `WarpFuelConsumption` is **not used** by the cost code. Cost comes from distance (section 6).
- UNKNOWN: `SlowWarpSpeed` use.
- UNKNOWN: `RequiredChargeToJump`. The data parse says 90. The game's own developer reference and a runtime read of the ship say 30. The parse of the data asset stops 8 bytes short of its end, so the slot mapping after index 31 is only INFERRED. Treat 30 as the live value.
- INFERRED: target speed is `Throttle x GlobalMaxSpeed / 10`, limited by MaxImpulse and added to momentum. The `BP_PilotConsole` reads the ship's `LocalMomentum` for dust and audio.

## 5. Navigation, steering and commands

### Piloting input

- OBSERVED: piloting adds the input mapping `IMC_Ship` (priority 1). Actions: `IA_Throttle`, `IA_Strafe`, `IA_Roll`, `IA_Mouse`, `IA_Boost`, `IA_Sprint`, `IA_WarpJump`, `IA_StopPiloting`, `IA_FirePrimary`, `IA_FireSecondary`, `IA_Ability0` to `IA_Ability9`.
- OBSERVED: `IA_WarpJump` opens `WBP_WarpDestination` when `PilotedShip.CanWarp()` is true.
- OBSERVED: seat flow. `BPC_PilotSeat.Interact` calls `SetStartPiloting(ShipID)`. `BP_PilotConsole.StartPiloting` sends `AddInputCommand(PlayerObject.ID, CreateInputSetPiloting(true, ConsoleSimObject.ID))`. Stop piloting is the same with `false, 0` (type id 62).

### Throttle

- OBSERVED: `IA_Throttle` calls `SetThrottleInput(int)`. That only stores the value and sets a dirty flag. The controller tick then queues **one** `ShipMovementInput` command per tick, addressed to the **player id** (not the ship). The sim routes it to the piloted ship.
- OBSERVED: only `+1` and `-1` steps exist. There is no "set throttle to N".
- OBSERVED: the command handler does not check the pilot. An unpiloted ship accepts the command (confirmed in game).
- OBSERVED: throttle cooldown is `ThrottleInputDelay` (3 frames). A burst of N commands moves throttle by 1.

### Steering

- OBSERVED: steering uses `ViewInput` (type id 40). It carries an aim offset relative to the nose. The turn rate grows with the offset. The dead zone is 4 degrees in the data (the game's developer reference says about 7). Maximum is 40 degrees.
- OBSERVED: for an unpiloted ship the receiver is the pilot console object id.
- OBSERVED: rotation and strafe come from `SetRotationImpulseInput` and `SetStrafingInput`. The handler turns each axis into a fixed step: rotation +-10000 raw, strafe +-1.0 (65536 raw).

### Command types used for navigation

| Command | Type id | Notes |
|---|---|---|
| `ConfigureShipOrdersInput` | 2 | Built by `SetShipAutopilot` and `SkipToOrder`. |
| `ShipTeleportInput` | 51 | Also carries SetJumpTarget and CancelJumpTarget. |
| `ShipMovementInput` | 58 (`0x3a`) | No factory function. |
| `InterstellarWarpCommand` | 59 | Payload layout UNKNOWN. |
| `ShipFiringInput` | 60 | Has a factory. |
| `ShipBoostingInput` | 61 | Payload layout UNKNOWN. |
| `SetPilotingInput` | 62 | Has a factory. |
| `DockingInput` | 65 (`0x41`) | Dock object id, `-1` means undock. |
| `ViewInput` | 40 | Has a factory. |
| `MiningAutopilotInput` | 8 | Separate native code, not read. |

- OBSERVED: `ShipMovementInput` fields: DeltaThrottle (int32, +1 or -1), RotationImpulse (3 x int32 fixed point), StrafeImpulse (3 x int32).
- OBSERVED: `ConfigureShipOrdersInput` fields: autopilot tri-state (`-1` means leave as is), another operation index (`-1` unused, INFERRED), SkipToOrder index (`-1` none). `SetShipAutopilot(ShipID, bool)` and `SkipToOrder(ShipID, Index)` build it themselves and write type id 2.
- OBSERVED: commands made with `NewObject` have type id `0` and are silently dropped at flush. Factory-made commands are fine. A `ShipMovementInput` made with `NewObject` needs the type id `58` written at command offset `0x28` by hand.

### Autopilot

- OBSERVED: `SetShipAutopilot(ship, true)` plus `SkipToOrder(ship, 0)` on an existing `DeliverOrder` with a dock makes the ship fly there and dock. Each is one command. `NewShipOrder` alone does not move a ship.
- OBSERVED: `WaitOrder` and `WaitForTimeOrder` never fly.
- OBSERVED: steering toward a target: the flight function does nothing while a jump is charging. After undocking it waits `AutopilotExitDockTime` (45 frames), pushing the throttle toward 10. Then it works out direction and alignment, and either arms a warp (section 6) or steers: throttle by distance (minimum 3), bank factor 150, rotation force 0.021, avoidance queries of 40 m and 50 m.
- OBSERVED: only **dock** targets feed the autopilot warp. It also requires the same solar system.
- OBSERVED: `bAutopilot == 1` makes a manual `-1` a no-op and blocks warp start.
- UNKNOWN: the mining autopilot, enemy pathfinding and formations. Not read.

### Docking

- OBSERVED: player flow: the ship enters the dock's trigger, the controller adds a possible dock, the dock interaction manager picks the closest available one, holding F for more than 1 second sends `DockCommand(DockId)` (or `-1` when the piloted ship is already docked).
- OBSERVED: the handler for `DockingInput` does **not** dock. It only arms `bDockingAutopilot`, stores the dock id as target and computes the berth position and rotation. The flight code flies in, and `SetDock` completes it.
- OBSERVED: `SetDock` then fires `OnShipDocked(Ship, true)` for **every** ship, not only the player's.
- INFERRED: the handler has no pilot check. Availability is checked on the client by `IsAvailableForManualDocking`.
- OBSERVED: `Dock.SetShip` and `GetDockedShip` exist as UFunctions.

### Boost

- OBSERVED (shape of the tick): while boosting with a working engine the throttle is capped at the low-power throttle value, the charge counter drops each frame, and at 0 the boost stops. When not boosting the charge grows by 1 per frame up to `MaxBoostCharge` (180). Input is `ShipBoostingInput` (`IA_Boost`).

## 6. Warp and jump

### What is needed to start a warp

- OBSERVED: all of these must hold: health above 0, no dock, `bAutopilot == 0`, not already travelling, a warp drive, `bJumpTargetSet`, `bAligned` (dot of forward and direction to target above `0.995`), fuel at least the cost, throttle cooldown (3 frames) over, and a `ShipMovementInput` with `DeltaThrottle +1`.
- OBSERVED: `bAligned` and `bInsufficientWarpFuel` are recomputed on every tick while a target is set and no charge is running.

### Sequence

1. OBSERVED: the warp UI row click calls `SetDestinationTarget`. That calls `GetJumpArrivalLocation` and sends `ShipTeleportInput.SetJumpTarget` (`bSetTarget = 1`).
2. OBSERVED: a `+1` throttle command starts the charge. The game takes the fuel **in one lump** at this moment and sets `JumpAnimProgress = 1`. `OnJumpStartCharging` fires.
3. OBSERVED: each tick `JumpAnimProgress` increases and `UpdateJumpChargeProgress` fires. When it reaches `RequiredChargeToJump` the real warp starts and `OnJumpStart` fires.
4. OBSERVED: the ship travels at `WarpSpeed` (300). After 600 frames of travel the game starts scanning the chunk distance (capped at 10 000 000) for arrival.
5. OBSERVED: the ship arrives at `target - direction x WarpArrivalDistance` (400 by default, or the sector's override). The exit distance is 750. `OnJumpEnd` fires.

### Cost

- OBSERVED: cost is the length of the fixed-point delta (chunk delta x 65536 plus local position), scaled, shifted right by 16, with a **minimum of 1**. Fuel is removed once at charge start.
- OBSERVED: the autopilot also needs fuel at least the cost before it arms a jump.

### Cancel and exit

- OBSERVED: `DeltaThrottle -1` while travelling asks to exit warp (`JumpAnimProgress` is set to 1 again if it was 0).
- OBSERVED: with a jump armed (`bJumpTargetSet` and a warp drive) the throttle is **frozen**. `+1` starts the charge, `-1` is ignored unless travelling. Send `CancelJumpTarget` (same type id 51) before flying normally.

### Autopilot jump arm

- OBSERVED: the autopilot arms a jump when chunk distance is above `MinChunkDistanceToJump` (20), alignment is above `RequiredAlignmentToJump`, the target is in the same solar system and a warp drive exists and has fuel. It sets the jump position to `target - direction x WarpArrivalDistance`.

### Instant teleport and interstellar

- OBSERVED: `ShipTeleportInput` with `bSetTarget = 0`, wrapped in `SetPiloting(true)` and `SetPiloting(false)`, teleports at once with no fuel.
- INFERRED: a teleport with mismatched state on peers can desync co-op.
- OBSERVED: interstellar travel uses `InterstellarWarpCommand` (id 59). `WarpDriveObject.IsInterstellarWarpDrive` gates other systems. The mission chain is `SlipstreamDrive` then `InterstellarWarpDrive`.
- UNKNOWN: the payload of the interstellar command. Hooks on it never fired in earlier probes.

## 7. Arrival logic

- OBSERVED: arrival is decided in the ship tick and in the flight function. While `bTravelling`, once the chunk and position thresholds are met, the game calls the end-warp function.
- INFERRED: the thresholds are stored in the flight model next to the arrival distance. Their names are UNKNOWN. The end-warp function is called from three places in the tick and once from the flight function.
- OBSERVED: `WarpArrivalChunkDistance` is 5 and `ExitWarpDistance` is 750.

## 8. Chunk loading and holds

- OBSERVED: streaming follows the local player object. It does not follow other ships.
- OBSERVED: a far unpiloted ship has no loaded rocks unless a chunk hold (`SetLevel` and `Apply`) is used.
- INFERRED: a mod that moves or warps ships that nobody watches must hold the destination chunks itself, or the ship arrives at an empty area.
- UNKNOWN: the exact limits and cost of holds. They are not covered by these notes.

## 9. Save files

### Files per slot

| File | Format | Content |
|---|---|---|
| `<slot>_M.sav` | Unreal GVAS, class `/Script/MainSequence.SavedMetaData` | Name, slot, playtime, last played, screenshot. |
| `<slot>_S.sav` | Custom binary world | The whole simulation. |
| `<slot>-Autosave_M.sav`, `<slot>-Autosave_S.sav` | Same formats | Autosave. |
| `settings.sav` | GVAS, class `BP_Settings_C` | FOV, cinematic camera, tutorial flag. |
| `EnhancedInputUserSettings.sav` | Standard UE class | Input bindings. |
| `Designs\<name>.sav` | GVAS `DesignSaveGame` | One saved blueprint design. |

All of them live under the game's `SaveGames` folder in the local app data.

- OBSERVED: saving writes the `_S` file first and the `_M` file second.
- OBSERVED: the save function picks a serializer by a flag byte in the game instance. Nonzero (every save seen) uses the new world serializer. Zero uses an older reflection style serializer.
- UNKNOWN: the older serializer's format. It was never seen on disk.
- INFERRED: the autosave is `SaveGame` with the slot name `<slot>-Autosave`. The trigger and interval are UNKNOWN.

### `_S.sav` layout

The file is three parts, one after the other. All integers are little endian, 4 bytes. Booleans are also 4 bytes.

| Part | Content |
|---|---|
| A, header (8 bytes) | `u32 0` (dropped by the loader, INFERRED a format marker), then `u32 30` (**save version**). |
| C, class table | `u32 N`, then N strings (`u32` length including the terminator, then ASCII class path). |
| B, objects | The root payload, then the object tree. |

- OBSERVED: the class table is written last in time, so it is complete. Entries are in **first-use order** of the tree walk. Entries 0 to 5 are always ResearchState, FactionManager, two missions, Statistics and StarCluster. N seen on real saves: 155, 157, 159, 200, 203, 206.
- OBSERVED: there is no compression, no checksum, no per-object size and no trailer. The file ends right after the last node.

### Root payload

The root class is exactly `SaveableRoot`. Fields in order:

| Field | Seen value | Notes |
|---|---|---|
| id | 1 | Always 1. |
| bool | 1 | |
| StarClusterId | 5 | INFERRED. |
| ResearchId | 2 | OBSERVED. |
| FactionId | 3 | OBSERVED. |
| StatisticsId | 4 | OBSERVED. |
| NextId | 43721, 44956, 88972, 212684, 261216, 41803 | INFERRED: the next free object id. |
| Ticks | equals `_M` Playtime | OBSERVED on 7 of 7 saves. INFERRED: the sim tick counter. Tick rate UNKNOWN. |
| player ids | `u32` count then `int32` ids | INFERRED: player object ids (for example 2337, 2311, 2325, 2360). |
| trailing word | 1 | |

### Object tree

- OBSERVED: the tree is **positional**. After the root payload, every parent writes `u32 childCount`. For each child it writes `u32 classIdx` (index into the class table), then the class specific payload, then the child's own children the same way.
- OBSERVED: the payload of every sim object starts with `u32 id`.
- OBSERVED: the first nodes after the root are ResearchState (class 0, id 2), FactionManager (class 1, id 3), Statistics (class 4, id 4) and StarCluster (class 5, id 5). ResearchState alone is about 30.7 KB (88 research entries, with data asset references stored as path strings).
- OBSERVED: root child counts seen: 6, 8, 9, 13, 14.
- OBSERVED: the same code path serves save and load. The direction comes from the archive flags.

{: .warning }
**There is no length prefix on any record.** An unknown class cannot be skipped. A reader needs the exact serializer of every class, including the native parent of every Blueprint class.

### How classes serialize

- OBSERVED: serialize functions use hard-coded field offsets. None of them walks Unreal properties or tagged properties.
- OBSERVED: coverage. 67 native classes cover all 210 distinct class paths found in the 7 saves. Blueprint classes (`BPO_*`, `BP_*`) resolve to their native parent.
- INFERRED (strong): variables you add in a Blueprint of a `BPO_*` class are **not saved**. A mod class persists only its native parent's fields.
- OBSERVED: base layouts. The plain object base is `id` only. The "DMS object" level is 10 words (INFERRED: id, location and chunk pieces, parent id). The machine level adds more words, two integer arrays and (from version 21) one more word. Derived machine classes call the machine level and append fields. Containers, conveyors, turrets, reactors, stations, research, factions, statistics, star clusters, sectors, chunks, missions, power grids, orders and others each add their own fields.
- UNKNOWN: `Inventory`, `AssemblerObject`, `MiningBeacon` and `DeliverOrder` matched only the id-only layout. That is probably a wrong match (they surely hold more). Their real payloads are unknown.
- UNKNOWN: the bodies of about a dozen complex helper functions used by many classes.

### Versions

- OBSERVED: the current save version is **30**. Saving always writes the newest layout.
- OBSERVED: the loader reads the version and stores it. On the load path seen there is no compare with 30, so a **newer** version is read blindly. UNKNOWN whether `LoadExistingGame` or the `_M` version gates it earlier.
- OBSERVED: class loaders have `if (version >= N)` branches. Old saves load. Downgrading is impossible.
- INFERRED: the table of gates.

| Class | Gate |
|---|---|
| Machine level | 21 (read directly) |
| Statistics | 27 and 28 |
| MiningDroneObject | 26 |
| Mission | 25 |
| ResearchState | 23 |
| MissileObject | 18 |
| FactionManager | 17 |
| SmallResourceAsteroid | 16 |
| PowerGrid | 14 |
| EnemyShip, EnemyStation | 13 |
| Door, StationObject | 9 |
| ContainerObject | 8 |
| Planet, StarCluster | 7 |
| CharacterCollisionEnvironment | 2 |
| VendorObject | 1 |

- OBSERVED: `_M` has its own `Version` integer, value 2 on all 7 saves. UNKNOWN what it gates.

### `_M.sav` and other GVAS files

- OBSERVED: header is `GVAS`, SaveGameVersion 3, package version UE4 522 and UE5 1018, engine 5.7.4, branch `++UE5+Release-5.7`, custom version format 3, 82 custom version pairs, the save class name, one extra `0x00` byte, then the properties.
- OBSERVED: property tags use the UE 5.6+ type-name form: name, type name with nested names, size, a flag byte (`0x01` array index, `0x02` guid, `0x04` extension, `0x08` binary struct, `0x10` bool true), then the value. The list ends with the string `None` and 4 zero bytes. Arrays of binary structs (such as `Color`) are raw.
- OBSERVED: `SavedMetaData` fields:

| Field | Type | Notes |
|---|---|---|
| DisplayName | Text | |
| SlotName | String | For example `New Game 2_S`. |
| ScreenshotData | Array of Color | 57600 entries, 320 x 180 BGRA, 230400 bytes. |
| ScreenshotSize | IntPoint | 320, 180. |
| Playtime | Int | Equals the root tick counter. |
| Version | Int | 2. |
| LastPlayedDateTime | DateTime | 100 ns ticks since year 1, UTC. |

- OBSERVED: an `_M` file is always about 232 KB because of the screenshot.
- OBSERVED: `settings.sav` holds FOV (double, 120), CinematicCamera (array of structs with GUID-suffixed Blueprint names) and DesignerTutorialCompleted (bool).
- OBSERVED: a design file holds one struct `Blueprint` (a `BuildBlueprint`): Version 1, DisplayName, Screenshot (dimensions and greyscale values), camera location and rotation, colour palettes, `Blocks[]` and `Connectors[]`. Each block has the class as an object path, a transform (grid location, height, grid rotation), BlockID, PaletteID and `DefaultMachines[]` (class, transform, MachineID, PaletteID, `SerializedMachineConfig` bytes).
- OBSERVED: designs refer to classes by path only, and ids are local to the design (41793 and up in samples). They are portable between saves.

## 10. Loading behaviour

Load order, from `LoadExistingGame`:

1. OBSERVED: read the header. The first `u32` is dropped. The second is the version.
2. OBSERVED: load the class table. Each entry is loaded as a class by path. If a class **fails to load**, the game logs `Failed to load class: <path>` as a `LowLevelFatalError`.
3. INFERRED: that error ends the game. A save that lists a mod class crashes on load when the mod's pak is not mounted.
4. OBSERVED: create the `SaveableRoot` and read its payload, then its children.
5. OBSERVED: for each child, read `classIdx`. An index outside the table logs `Save file corrupted! Unknown class ID`. Then the object is created, read, its own children read, and it is added to the id map.
6. OBSERVED: afterwards the root post-load runs, each top-level child gets a post-load call, and the world is finalized with the version.

## 11. What a mod must not hand-edit

{: .warning }
Never edit a `_S.sav` by hand. Every node is positional and has no length. Adding or deleting one object means rewriting the whole object stream. One wrong byte shifts everything after it, with no recovery. Reading past the end sets the archive error flag.

To change objects safely you would need to:

- fix the parent's child count,
- keep the class table indices valid (INFERRED: first-use order is probably not required, only valid indices),
- fix every id cross reference (INFERRED: inventories and belts hold ids in integer arrays),
- fix the root `NextId`.

Other rules:

- OBSERVED: three saved class paths come from this project's mods: `BPO_MiningMod`, `BPO_MinerModuleBlock`, `BPO_MiningBenchBlock` (3 of 7 saves list them). Removing the pak or renaming the class path makes old saves crash on load.
- OBSERVED: **keep a mod class path forever.** A renamed class is a new class. Old nodes with the old path crash the load.
- OBSERVED: adding fields to an existing class means patching its serialize and deserialize functions and bumping the version. That breaks every old save and every co-op peer. Use a **sidecar file** instead.
- INFERRED: store mod state outside the `_S`, in a sidecar file keyed by slot name and object id. A native config blob could also work if the parent class saves one (UNKNOWN which classes do).
- INFERRED: create new objects through the sim (the build command path) so ids, `NextId` and the tree stay consistent.
- OBSERVED: do not read or copy a save while the game is autosaving. Write atomicity is UNKNOWN.
- Do not restore an old backup over a live save. Test any edit on a copy under a new slot name.

## 12. Gotchas

- OBSERVED: commands with type id `0` are dropped silently. `ShipMovementInput` needs the id `58` written by hand.
- OBSERVED: the 3 frame throttle cooldown means a burst of N throttle commands moves the throttle by 1.
- OBSERVED: an armed jump freezes the throttle. Send `CancelJumpTarget` first.
- OBSERVED: `bAutopilot == 1` blocks manual slow-down and warp start.
- OBSERVED: a dock pointer can dangle. Test the pending-kill bit.
- OBSERVED: do not write throttle, dock or `bJumpTargetSet` locally. They are local only and the sim overwrites them.
- OBSERVED: natives run on the game thread only. Delayed Lua callbacks that fire after a world change crashed the game with an access violation.
- OBSERVED: autopilot warp works only toward **dock** targets in the **same solar system**.
- OBSERVED: character space jump was abandoned. The character class has no movement component.
- OBSERVED: poll ship state (`bAutopilot`, `bTravelling`, `JumpAnimProgress`, `Dock`, `bDockingAutopilot`, `Throttle`) at most every 250 ms from the MSCore tick, never every frame.

## 13. Hook points for mods

- OBSERVED: UFunctions you can hook from UE4SS:
  - `/Script/MainSequence.DMSPlayerController`: `AddInputCommand` (pre-hook sees every ship, dock, order and warp command; compare class pointers only), `DockCommand`, `SetShipAutopilot`, `SkipToOrder`, `SetThrottleInput`, `SetStartPiloting`, `SetJumpTarget`.
  - `DMSPlayerShipObject`: `CanWarp`, `GetJumpCost`, `GetAvailableJumpFuel`, `GetJumpArrivalLocation`, `IsShipInCombat`. Use post-hooks to read. Patching a return value is local only.
  - Blueprint signals: `BP_Scout_C`, `BP_Prospector_C`, `BP_Gallant_C` `OnJumpStartCharging`, `OnJumpStart`, `OnJumpEnd`. `BP_PlayerController_C` `OnShipDocked(Ship, bNewDocked)`, `OnStartPiloting`, `OnStopPiloting`, `AddPossibleDock`, `RemovePossibleDock`. `BPO_DockInteractionManager_C` `SetNewClosestDock`.
  - `DMSWorldSubsystem`: `SaveGame`, `FinalizeLoadGame`, `NewSerializeWorld`, `NewDeserializeWorld`, `LoadExistingGame`.
- OBSERVED: a native detour (C++ only) needs a size and FNV-1a hash check at each address, and must run on the game thread. Candidates are in the table below.
- OBSERVED: to send a throttle step: create a `ShipMovementInputCommand` with `NewObject`, write `58` at `+0x28` and `DeltaThrottle` at `+0x40`, then `AddInputCommand(shipId, cmd)`. For autopilot call `SetShipAutopilot` and `SkipToOrder` directly.
- INFERRED: to write a sidecar next to a save without parsing it, hook the return of the world serializer and copy the raw `_S` bytes, then write `<slot>.mods` next to the save.
- INFERRED: to log missing classes before the fatal error, hook the class registry load and check each path first.
- Never call methods on a stale `UObject`.

## Build-specific reference

Build: `MainSequence-Win64-Shipping.exe`, size `149200896`, image base `0x140000000`. All values are virtual addresses unless marked as an object offset.

### Ship object offsets (`DMSPlayerShipObject`)

| Field | Offset |
|---|---|
| Registry pointer (id lookup); current frame at `[reg+0x30]+0xb4` | `0x28` |
| Console or owner pointer | `0x70` |
| Position (3 raw) | `0x78`, `0x7c`, `0x80` |
| Rotation quaternion W, X, Y, Z (the flight code uses the conjugate) | `0x84` to `0x90` |
| Chunk (IntVector) | `0x94`, `0x98`, `0x9c` |
| Solar system id | `0xa0` |
| Registry-held health | `0x2e8` |
| Thruster and part container | `0x348` |
| Stats pointer (byte `+0xc8` = engine ok, INFERRED) | `0x350` |
| FlightModel pointer | `0x4e0` |
| Throttle | `0x4e8` |
| LastThrottleUpdateFrame | `0x4ec` |
| StrafeImpulse | `0x510`, `0x518` |
| RotationImpulse | `0x520`, `0x528` |
| Cached speed | `0x544`, `0x5d0` (arrival) |
| Autopilot aim result | `0x558`, `0x560` |
| Boost charge / bBoosting | `0x584` / `0x588` |
| Engine or jump flag (INFERRED) | `0x5c0` |
| bAutopilot | `0x5e8` |
| CurrentOrderIndex / Orders | `0x5ec` / `0x5f0` |
| Autopilot wait counter | `0x650` |
| bJumpTargetSet / bAligned / bInsufficientWarpFuel | `0x668` / `0x669` / `0x66a` |
| JumpTargetID | `0x66c` |
| bTravelling | `0x670` |
| Warp frame counter | `0x674` |
| JumpProgress / JumpArrivalProgress | `0x678` / `0x67c` |
| ChargeToJump / RequiredChargeToJump | `0x68c` / `0x690` |
| JumpAnimProgress | `0x694` |
| JumpTargetLocation | `0x6bc` |
| Jump chunk / SolarSystemId | `0x6c8` / `0x6d4` |
| WarpDrive pointer / WarpDriveID | `0x7e0` / `0x7e8` |
| Dock pointer / DockID / TargetDockID | `0x898` / `0x8a0` / `0x8a4` |
| bDockingAutopilot | `0x8a8` |
| DockTargetLocation / Chunk / Quat | `0x8ac` / `0x8b8` / `0x8c4` |
| Pilot / PilotID | `0x970` / `0x978` |

### Flight model offsets (runtime)

| Field | Offset |
|---|---|
| Throttle cooldown (ThrottleInputDelay) | `0x30` |
| Low-power or boost throttle cap | `0x60` |
| MaxBoostCharge | `0x70` |
| Autopilot exit-dock wait | `0x9c` |
| WarpArrivalDistance | `0xc8` |
| Arrive and close thresholds (names UNKNOWN) | `0xcc`, `0xd0`, `0xe4` |
| MinChunkDistanceToJump | `0xd4` |
| RequiredAlignmentToJump | `0xd8` |
| RequiredChargeToJump | `0xdc` |

### Command object offsets

| Item | Offset |
|---|---|
| Command type id | `0x28` |
| Command receiver id | `0x38` |
| `ConfigureShipOrdersInput` autopilot / other index / SkipToOrder index | `0x40` / `0x44` / `0x48` |
| `ShipMovementInput` DeltaThrottle | `0x40` |
| `ShipMovementInput` RotationImpulse | `0x44`, `0x48`, `0x4c` |
| `ShipMovementInput` StrafeImpulse | `0x50`, `0x54`, `0x58` |
| `DockingInput` dock id | `0x40` |
| Controller command queue (array / num / max) | `+0x768` / `+0x770` / `+0x774` |
| Controller throttle value / dirty flag | `+0x80c` / `+0x808` |

### Navigation functions

| Item | Address |
|---|---|
| Ship command handler (virtual) | `0x144287960` |
| `ChangeThrottle` | `0x144283910` |
| Ship tick | `0x144283d10` |
| `FlyTowards` | `0x144285240` |
| `SetDock` | `0x14428c350` |
| EndWarp (arrive, INFERRED) | `0x14428d3a0` |
| `SetJumpTarget` core | `0x14428c910` |
| Jump cost core | `0x1442871a0` |
| Start real warp | `0x144286920` |
| `OnJumpStartCharging` | `0x1441c6eb0` |
| `CanWarp` | `0x1442861a0` |
| `GetAvailableJumpFuel` | `0x144286c90` |
| `AddInputCommand` thunk / native | `0x144211920` / `0x1442609d0` |
| `SetShipAutopilot` thunk / native | `0x144213930` / `0x144263bb0` |
| `SkipToOrder` thunk / native | `0x144213e20` / `0x144263df0` |
| `SetThrottleInput` thunk / native | `0x144213c40` / `0x144263ca0` |
| Controller tick (queues throttle command) | `0x144261794` |
| `DockCommand` thunk / native | `0x144212790` / `0x144261d90` |
| `SetStartPiloting` thunk | `0x1442117e0` |
| Factories: `CreateInputShipTeleport` / `SetPiloting` / `View` / `ShipFiring` | `0x1441fd5b0` / `0x1441fcf50` / `0x1441ec900` / `0x1441fd390` |
| `IsAvailableForManualDocking` native | `0x14428e4a0` |
| UFunction execs: `CanWarp` / `GetJumpCost` / `GetAvailableJumpFuel` / `GetJumpArrivalLocation` / `IsShipInCombat` | `0x1441c7c00` / `0x1441c80e0` / `0x1441c7e30` / `0x1441c7f30` / `0x1441c8670` |
| Dock UFunction execs: `SetShip` / `GetDockedShip` / `IsAvailableForManualDocking` | `0x1441ce180` / `0x1441c9870` / `0x1441c98a0` |

### Save functions and constants

| Item | Value |
|---|---|
| `SaveGame` exec / native writer | `0x1441cdfe0` / `0x14426d8c0` |
| New world serializer (`NewSerializeWorld`) | `0x14426d170` |
| Older serializer | `0x14426d9e0` |
| File writer (`_S` then `_M`) | `0x14421cf70` |
| Save version immediate | `0x1e` (30), written at `0x14426d271` |
| Class table writer | `0x14421d360` |
| Class table load (fatal call site) | `0x144217280` (`0x144217449`) |
| Class load helper | `0x14140d230` |
| `LoadExistingGame` exec | `0x1441cd3b0` |
| `NewDeserializeWorld` exec / native | `0x1441cd500` / `0x14426cef0` |
| Root deserialize | `0x14426eb80` |
| Child read (load) / child write (save) | `0x14426ec20` / `0x14426f090` |
| Per child post-load | `0x14426d6d0` |
| Finalize load | `0x1442695d0` |
| `SaveableRoot` vtable / serialize slot 92 / deserialize slot 93 | `0x147330330` / `0x1441cc810` / `0x1441cc270` |
| Base `SaveableObject` serialize | `0x1441a3580` |
| "DMS object" level serialize | `0x1441a3340` |
| Machine level serialize / deserialize | `0x1441a3480` / `0x1441a2c00` |
| Id map register | `0x1442657d0` |
| Archive error flag setter | `0x141200220` |

Selected class serializers (save, load):

| Class | Save | Load |
|---|---|---|
| Machines (Elevator, ShipContainer, SolarPanel, Dock, ShieldGenerator) | `0x1441a3480` | `0x1441a2c00` |
| BuildingBlockObject (59 `BPO` classes) | `0x1441a9df0` | `0x1441a8bc0` |
| ContainerObject | `0x1441b50d0` | `0x1441b4ba0` |
| ConveyorBeltObject | `0x1441b51e0` | `0x1441b4cd0` |
| Turret, ItemScooper, MiningLaser | `0x14420d920` | `0x14420d260` |
| CannonObject | `0x14420d560` | `0x14420ceb0` |
| SmallResourceAsteroidObject (24 `BPO` classes) | `0x1441f5f80` | `0x1441f5880` |
| FloatingOreObject | `0x1441f5e80` | `0x1441f5770` |
| EnemyShipObject, EnemyStationObject | `0x1441d2810` | `0x1441d1870` |
| ReactorObject | `0x1441f0900` | `0x1441f0340` |
| StationObject | `0x1442087b0` | `0x1442080d0` |
| ResearchState | `0x1441f5aa0` | `0x1441f52d0` |
| FactionManager | `0x1441d69d0` | `0x1441d6490` |
| Statistics | `0x144208840` | `0x144208180` |
| StarCluster | `0x1442033d0` | `0x144202270` |
| SolarSystemSector | `0x144203030` | `0x144201e60` |
| Chunk | `0x1442027d0` | `0x1442015e0` |
| Mission (12 `BPO` classes) | `0x1441ea570` | `0x1441ea200` |
| PowerGrid | `0x1441f0860` | `0x1441f0290` |
| CharacterCollisionEnvironment | `0x1441b0370` | `0x1441afdd0` |
| SolarSystem, MacroChunk and similar (id only, some wrong matches) | `0x1441a3580` | `0x1441a2d20` |

- Where the class to vtable match is uncertain, the row is a candidate only (INFERRED).
- Primitive helpers: bool as `u32` `0x1410c36c0`, `TArray<int32>` `0x141942a20`, string `0x14106df60`, two `u32` `0x1441a1810`, three `u32` `0x1441a16b0`.

## Unknown

- `RequiredChargeToJump`: 90 in the data parse, 30 at runtime.
- Use of `SlowWarpSpeed`, and the names of the flight model thresholds used for arrival.
- Payload layouts of `ShipBoostingInput` (id 61) and `InterstellarWarpCommand` (id 59).
- The mining autopilot, enemy pathfinding, formations, and the command handler branches other than docking and movement.
- The older world serializer, and whether the loader checks the first `u32` or the `_M` version before reading.
- The autosave trigger and interval, and whether a save write is atomic.
- Payloads of `Inventory`, `AssemblerObject`, `MiningBeacon` and `DeliverOrder`, and the bodies of the complex serialize helpers.
- The save tick rate, and the exact meaning of `NextId`.
- Which native classes save a config blob that a mod could use for its own state.
- Limits and cost of chunk holds.
