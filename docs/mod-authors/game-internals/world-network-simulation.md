---
title: World, network and simulation
parent: Game internals
nav_order: 2
---

# World, network and simulation

Notes from reading the game and testing mods in it. Each claim is tagged:

- **OBSERVED**: read in the game's code, data or logs, or seen in a test.
- **INFERRED**: follows from what was read, not tested.
- **UNKNOWN**: not found yet.

Build these notes apply to: `MainSequence-Win64-Shipping.exe`, exe size `149200896`, Unreal Engine 5.7. Addresses and struct offsets are listed only in the table at the end. They are build specific and change with every game update. Everything here comes from static reading of the game. Where a claim says "tested", it was a single-player test unless stated.

## 1. Two worlds: simulation objects and actors

- OBSERVED: the game keeps two parallel worlds.
  - **Simulation objects** (`SaveableObject` and its children, for example `DMSObjectBase`). They are saved, deterministic and run in lockstep.
  - **Actors** (`DMSActorBase`, an Unreal `Actor`). They are what you see on screen. They exist only on your machine and are spawned and removed by chunk streaming.
- OBSERVED: an actor points to its simulation object with `ObjectId` and `GetSimulationObject()`.
- OBSERVED: everything in section 2 (cluster, systems, sectors, chunks, rocks, stations) is simulation. Rocks and stations on screen are actors made per loaded chunk.
- OBSERVED: naming. `BPO_X` is a simulation class (saved). `BP_X` is the actor or visual class. Most things come as a pair.
- OBSERVED: class chains that matter to mod authors.

| Kind | Chain |
|---|---|
| Ships | `DMSPlayerShipObject` > `BuildingFrame` > `DMSObjectBase` > `SaveableObject`. Player ships and AI ships use the same class. |
| Machines | `MachineObject` > `DMSObjectBase`. `WeaponObject` and `PilotConsoleObject` > `MachineObject`. `MiningLaserObject`, `ItemScooperObject`, `CannonObject` > `WeaponObject`. |
| Rocks | `SmallResourceAsteroidObject` > `AsteroidObject` > `DMSObjectBase`. `FloatingOreObject` > `DMSObjectBase`. |
| Inventories | `Inventory` > `SaveableObject`. `InventoryMachine` and `ShipUpgradeMachine` > `MachineObject`. |
| Blocks | `BuildingBlockObject` > `DMSObjectBase`. |
| Builders | `ConveyorBuilder` > `MachineBuilder` > `BuilderBase` > `Object`. |
| Actors | `Weapon`, `BuildingBlock`, `Machine`, `BuildingFrameActor`, `BuildProgressActor`, `StationActor` > `DMSActorBase`. |
| Walking body | `BP_Character_C` > `DMSCharacter` > `DMSActorBase`. It is not an Unreal `ACharacter`. |
| Controller | `BP_PlayerController_C` > `DMSPlayerController` > `DMSNetController` > `PlayerController`. |
| Camera pawn | `BP_CameraPawn_C` > `Pawn`. |

- OBSERVED: the game's native module lists 333 classes and 582 structs, enums and delegates. That list has no parent classes. Parents above are marked OBSERVED only where a class dump or probe showed them.
- OBSERVED: singletons: `DMSWorld` (`GetPlayers`, `GetResearchState`, `GetFactionManager`, `AddFixedUpdate`, `RemoveFixedUpdate`, `IsDemo`), `DMSGameState`, `DMSGameMode`, `FactionManager`, `WidgetSubsystem`, `SpawningSubsystem`.
- OBSERVED: ships that exist today: Gallant, Prospector, Scout (and a fighter). Enemies use `EnemyShipObject`, `EnemyStationObject`, `EnemyBase`, `EnemyFleet`, `EnemyHiveObject`, `EnemyAsteroid`, `EnemyStationSpawner`, with `AttackManager`, `AttackWave` and `ArenaWaveManager`.
- OBSERVED: stations use `StationObject`, `StationPlatformObject`, `StationConstructionObject`, `MinerConstructionObject`, `BuildProgressObject` and the actor `StationActor`. They are built with `StationBuilder` or `MapStationBuilder`. Docking uses `Dock`, `DockingInput`, `DroneBayObject`, `ShipyardObject`.
- OBSERVED: ships follow orders (`ShipOrderBase` and its children `DeliverOrder`, `WaitOrder`, `WaitForTimeOrder`, `CollectOrder`, `LoadItemsOrder`, `UnloadItemsOrder`, `TutorialShipOrder`). The game has its own mining autopilot command (`MiningAutopilotInput`).
- OBSERVED: there is no crew class.
- UNKNOWN: the power formula. Known pieces: `PowerGrid` (`RefreshPowerReport`, delegate `PowerGridLowPower`), `MachineObject.GetPowerGrid`, `BuildingFrame.OnLowPower`, `StationObject.SetStationLowPower`, sources `ReactorObject` and `SolarPanelObject`.

### Stations, frames and blocks

- OBSERVED: a ship and a station body are both a `BuildingFrame` made of blocks (`BuildingBlockObject`). Machines are separate `MachineObject`s attached to it.
- OBSERVED: block data (the `BuildingBlockObject` defaults) holds: `DisplayName`, `Icon`, `Description`, `FunctionalType`, `ItemCost[]`, `bAllowRotate`, `MirrorClass`, `bStationBlock`, `bShipBlock`, `bLockedInDemo`, `Hull`, `Weight`, `Thrust`, `BlockDetails[]`, `Connectors[]`, `Colliders[]`, `ShipCollisions[]`, `BlockBounds`, `MountPoints[]`, `bCapturePlayerAnchor`, `RespawnLocation`, `PlacementOffset`, `GridTransform`, `DefaultMachines[]`, `DefaultMachineIDs[]`, `AttachedMachines[]`, `AttachedDecorations[]`, `PaletteID`.
- OBSERVED: `Hull`, `Weight` and `Thrust` are fixed point numbers (see section 3). `Hull = 327680` is 5. `Weight = 196608` is 3.
- INFERRED: `ConnectorComponent`, `MountPointComponent` and `VerticalConnectorComponent` describe how blocks touch each other.
- OBSERVED: the player's building inventory is `DMSPlayerObject.GetBuildingInventory()`.

## 2. World model

- OBSERVED: the world is a tree. There is no territory class.

```
StarCluster                 (saved; holds WorldGenParams)
 +- SolarSystem             (an id, for example 6)
     +- SolarSystemSector   (5x5x5 chunks)
     +- AsteroidBelt        (several BPO_AsteroidBelt* variants)
     +- Planet              (several BPO_Planet* variants)
     +- Chunk               (2048 m cube)
     +- MacroChunk          (holds large features)
```

- OBSERVED: `SolarSystem` holds: `DisplayName`, `WorldGenParams`, `StarGenParams`, `StarGridLocation`, `StellarLocation`, `StarTemp`, `StarSize`, `SystemPOIs[]`, `AsteroidBelts[]`, `ChunkMap` (a map from chunk location to chunk), `ChunkSaveList`, `MacroChunkMap` and `MacroChunkIDs`, `DynamicObjects` and ids, `LargeFeatures` and ids, `EnemyTargetIDs`, `PlayerTargetIDs`, `Missiles`, `BulletManagerID`, `ClearProgress`, `SpawnChunk`. Functions: `GetStarCluster`, `GetStarDirection`, `GetStellarLocation`.
- OBSERVED: `SolarSystemSector` holds: id, `ParentID` (the system id), `ChunkLocation`, `HalfExtents`, `GenParams` (a `SectorGenParams`), `Biome`, `GenerationID`. Functions: `GetCenterChunk`, `GetDisplayName`, `GetSolarSystem`. `HalfExtents` is `(2,2,2)`, so a sector is 5 x 5 x 5 chunks.
- OBSERVED: `Chunk` holds: `ChunkSeed`, `Location`, `Biome`, `BiomeStrength`, `DynamicObjectIDs`, `DetailLevels`, `Colliders`, `ExtrudingColliders`, `DynamicColliders`, `bWarpTraversable`, `bGenerated`, `bLoaded`, an objects array, an applied detail level and a target detail level. `MacroChunk` holds `MacroChunkLocation` and `FeatureIDs`.
- OBSERVED: a chunk is a 2048 m cube.
- OBSERVED: belts (`BPO_AsteroidBelt`, `_2`, `_Outer`, `_Outer_2`, `_Fractured`, `_EnemyFortress`) are empty Blueprint shells. Their data lives in the native class.
- OBSERVED: planets (`BPO_Planet_Inner`, `_Inner_2`, `PlanetTerrestrial`, `PlanetFrozen`, `Planet_Biologic`, `Planet_Anomaly`) hold `PlanetType` and `Materials[]`.
- OBSERVED: stars are `BP_Star` variants: Red, RedDwarf, Yellow, BlueGiant, BlueSupergiant, Pulsar, BlackHole. Map actors (`BP_StarMapActor`, `BP_SectorMapActor`, `BP_PlanetMapActor`, `BP_AsteroidBeltMapActor`) are presentation only.
- OBSERVED: sample system 6 has 21 sectors. Ids 768, 1008, 1247 and 1488 are rich. 265, 435 and 602 are fractured. 1977 to 1981 are icy. 1728 and 1969 to 1972 are plain. 1909 is a trading outpost and 1973 is a relic destroyer.
- OBSERVED: sectors you have not visited can be read at runtime in full.
- OBSERVED: about 39000 rocks exist in a sample world.

## 3. Coordinates

- OBSERVED: positions use `PNum`, a fixed point integer. `65536` raw is 1 m.
- OBSERVED: world position (raw) is `ChunkLocation * 134217728 + Location`. A chunk is 134217728 raw, which is 2048 m.
- OBSERVED: an identity quaternion has `W = 65536`.
- OBSERVED: example distances in generator code: `6553600` is 100 m, `9830400` is 150 m, `13107200` is 200 m, `22937600` is 350 m. The generator comments call these "units" (1 unit is 1 m here).

## 4. World generation

### Inputs

- OBSERVED: new-game settings (`WorldGenParams`, also stored on the star cluster and each system): `Seed`, `StartingArea`, `EnemyLevel`, `ResourceAbundance`, `ResourceRichness` (all int), `bSkipTutorial`, `ClusterSpan` (int), `StarsToGenerate` (int), `bArena`.
- OBSERVED: `DMSGameSubsystem` has `GetWorldGenParams` and `SetWorldGenParams`. `LoadingMode` and `LoadingSlotName` tell the game what to do: mode `0` is a new game, otherwise it loads a slot.
- OBSERVED: the skip-tutorial path builds `WorldGenParams` with only `Seed` and `bSkipTutorial = true`.
- OBSERVED: `DAT_GenerationParams` is the main data asset. `DMSGameState` and `DMSWorld` reference it as `GenerationParams`. 76 world generation assets exist (`GEN_*`, `BIOME_*`, sector assets).
- OBSERVED: members of the generation params: `ChunkGenRate` (int), `PlayerRadius`, `StartingItems[]`, `TravellingTime`, `bDemo`, `StarterShipClass`, `TutorialShipClass`, `PlayerObjectClass`, `ResearchStateClass`, `FactionManagerClass`, `StatisticsClass`, `StationObjectClass`, `ItemCrateClass`, `BulletManagerClass`, `AttackManagerClass`, `TutorialMission`, `bUseNewSerialization`, `ItemRegistry`, `DMSRegistry`, `Version`, `MinSaveVersion`.
- INFERRED: `ChunkGenRate` is the number of chunks generated per tick.
- OBSERVED: a `SectorGenParams` holds `ResourceRichness[]` (item and amount), `ThreatLevel`, `WarpArrivalDistance`, `DisplayName`, `MapMesh`, `Biome`, `BiomeFog`, `MaxDistance`, `MinAmount`, `MaxAmount`, `Resources[]`.
  - A rich sector has 6 resource entries (amounts 3, 3, 3, 3, 1, 1). A plain sector has 4 entries of 1.
  - `ThreatLevel` is 1 or 2.
  - Rich sector 1008 has a `WarpArrivalDistance` of `91750400` raw.
- OBSERVED: a `Biome` holds `Features[]`, `MacroFeatures[]`, `Colliders` and `MapMaxVisibleZoom`. A feature row holds `Class`, `Weight`, `MinPerChunk`, `MaxPerChunk`, `MinResourceCount`, `MaxResourceCount`, `MinWidth`, `MaxWidth`, `HeightLimit`, `MinHalfExtent`, `MaxHalfExtent`, `GravityDistance`, `Health`, `MaxHealth`, `ResourceMultipliers`.
- OBSERVED: system layout rows hold `EmptyBiome` and `EmptyBiomeDistance`, `Biome` and `BiomeDistance`, `TransitionBiome` and `TransitionBiomeDistance`, `GenerationChance`, `Class`, `SpawnIndex`, `StationIndex`, `NumSectors`, `bPlayerSpawn`, `Sectors[]`, `VersionAdded`. Planet rows hold `PlanetType` and `Materials[]`. Belt rows hold `SectorGenParams[]`, `Distance`, `SectorDistance`.
- INFERRED: which property belongs to which row type. The dump is a flat list of property names.
- OBSERVED: biome display names seen: AsteroidField, RichAsteroidField_3, Icy, Fractured, Trading Outpost, Relic Destroyer.
- OBSERVED: rock gen data: `HealthPerOre`, `ItemsPerFloatingOre`, `ResourceMaxHealth`, `ResourceRadius` (8 m or 70 m), `ThreatPerItem`. A rock (`SmallResourceAsteroidObject`) has `DamageToMine`, `Damage`, `ResourceItems` and `FloatingOreClass`.
- OBSERVED: sector resource item names come back empty from `DisplayName`. Use the item's `GetName()` instead (for example `ITEM_CobaltOre`).

### Flow

1. OBSERVED: a new game goes from the new-game menu and generation preview, to `DMSGameSubsystem.SetWorldGenParams`, to `DMSWorldSubsystem.StartNewGame`.
2. INFERRED: the star cluster is created. `ClusterSpan` and `StarsToGenerate` decide how many stars are placed (`StarGridLocation`). Each star gets a solar system with star params. The native body was not traced.
3. INFERRED: `SolarSystemGen` builds the sectors from the layout rows. `SpawnIndex` marks the player's start sector. A sector is a biome, a `GenerationID` and `GenParams`. Belts and planets are added to the system.
4. INFERRED: chunks are **not** generated when the world is created. A chunk has a `bGenerated` flag, and the level setter needs it to be set. A chunk is generated when streaming first needs it.
5. INFERRED: on first load the native generator picks biome features by weight, using the chunk seed. The seed probably comes from the world seed and the sector's `GenerationID`. It spawns simulation objects (rocks, icy and hex asteroids, station spawners) into the chunk.
6. INFERRED: the random generator is an Unreal `FRandomStream`. The same seed, code and data give the same world on every peer. That is why co-op does not need to send the world.

### Custom generators

- OBSERVED: some sectors use a Blueprint generator with a `CustomGenerate(SpawnChunk, Seed)` function. Classes: `BPO_StarterSectorGenerator`, `BPO_RichSectorGenerator`, `BPO_SectorGenerator_Iridium`, `BPO_Generator_FracturedSector`.
- OBSERVED: helpers used by the Blueprints: `GenerateClass`, `GenerateAdjacentClass`, `GenerateResource`, `GenerateAdjacentResource`, `IncrementDistance`. Native helpers: `CustomGenerator::GetRandomOffset`, `GetOffsetChunk`, `GetRandomRotation`, `Chunk::GenerateObject`, `AddToMacroChunk`, `Array_RandomFromStream`.
- OBSERVED: generator defaults: `AsteroidClasses`, `MediumAsteroidClasses` (a map from item to class), `Resources[]`, `InitialClasses[]`, `MinDistance`, `MaxDistance`, `Constrain Z`.
- OBSERVED: the rich generator, in outline:

```
repeat 5 times:
    GenerateClass(a random or indexed InitialClass)
    GenerateAdjacentClass(a relic station spawner, an icy asteroid,
                          or three medium graphite asteroids)
    IncrementDistance(100 m, 150 m, 200 m or 350 m)
GenerateClass(BP_AbandonedStationObject_01)
```

- OBSERVED: the starter generator calls `GenerateResource` for silica, five passes, and medium asteroids at 300 to 350 m.

## 5. Chunk streaming

- OBSERVED: the only streaming source is the **local player**. `DMSPlayerController.LoadChunks` centres on the player object and loads a radius of `ViewDistance = 2`, which is 5 x 5 x 5 = 125 chunks.
- OBSERVED: each chunk has a detail level. Level 0 unloads and removes objects. Level 1 is a Chebyshev distance of 1 or less. Level 2 is the outer ring. The level setter only works when `bGenerated` is set. The apply function creates or removes objects when the applied level differs from the target.
- OBSERVED: the mod `mb_chunkhold` calls the game's own set-level and apply functions for a 3 x 3 x 3 cube around far AI ships. It took one far area from 0 to 316 rocks in one second without a crash. Tested in single player only.
- INFERRED: each peer streams actors for its own local player only.
- UNKNOWN: whether far simulation chunks are generated on every peer or only when some player loads them. Generation is deterministic, so the order does not matter. But a chunk generated on one peer only would change the object count that the checksum compares (`ClientTotalObjects` against `ServerTotalObjects`).
- INFERRED: `mb_chunkhold` is a co-op risk until it is tested with two peers.

## 6. Commands (the only way to change the world)

- OBSERVED: `DMSPlayerController.AddInputCommand(Receiver, Cmd)` stores the receiver on the command and appends it to the controller's pending list. The list is flushed at the end of the controller tick.
- OBSERVED: a command with type id `0` is dropped at the flush. Commands from the game's `CreateInput*` factories have the type id set. Commands from `NewObject` need the id written by hand (for example `ShipMovementInput` is `0x3a`).
- OBSERVED: when the receiver is not `0`, the sim finds the object by id and calls its virtual handler. When the receiver is `0`, the sim treats it as a world command and finds the handler by class (for example `Build`, `Disassemble`).
- OBSERVED: the controller has wrappers (names only): `AddBuildCommand`, `AddDisassembleCommand`, `AddCollectFromMinerCommand`, `AddConfigureMachineCommand`, `AddElevatorCommand`, `AddInventoryInteraction`, `AddModifyDroneConfigCommand`, `AddQueueResearchCommand`, `AddRepairShipCommand`, `AddSetDisplayNameCommand`, `AddUnstuckCommand`, `AddWeaponInputCommand`, `AddBlueprintBuildCommand`, `AddLookInput`, `AddCycleTargetCommand`, `NewDeliverOrder`, `NewWaitOrder`, `NewCollectOrder`, `DeleteOrder`, `ReorderShipOrders`, `DockCommand`, `GiveCreativeItems`, `ForcePlayerRespawn`, `SetShipAutopilot`, `SkipToOrder`.
- OBSERVED: the stubs `AddBuild{Machine,ShipMachine,Miner,AttachedStation,Decoration,NewStation}Command` do nothing.
- OBSERVED: `BuildInput` needs receiver `0`, and its Buildable is a `DAT_*` asset, not a class. There is no error for missing items or a blocked placement. The throttle cooldown (a flight model value, in frames) limits how often a command repeats. The flush happens once per controller tick.
- OBSERVED: some commands are sent from the designer to a frame, so their receiver is the frame id: `AddBlockInput`, `RemoveBlockInput`, `SetBlueprintTypeInput`, `SetPinnedCostInput`.
- INFERRED: the `Creative*` commands (ids 9, 10, 11, 43) and `GiveCreativeItems` are the built-in cheat route. The sender's creative flag skips item cost. How the game gates this in non-creative worlds is UNKNOWN. Not tested.
- OBSERVED: new command kinds cannot be added from Lua or a pak. They need a native class. Reuse existing ids.

### Command type ids

OBSERVED unless noted. The class was read from the game for each id. Decimal first, hex in brackets where it is useful.

| Id | Class | Id | Class |
|---|---|---|---|
| 1 | `NewShipOrderInput` | 34 | `ModifyBlueprintInput` |
| 2 | `ConfigureShipOrdersInput` | 35 | `SelfDestructInput` |
| 3 | `UpdateShipOrderInput` | 36 | `SetBlueprintCreativeInput` |
| 4 | `ClipboardInput` | 37 (`0x25`) | `MovementInput` |
| 5 | `SetColourInput` | 38 | `SetCharacterAnchorInput` |
| 6 | `SetPaletteInput` | 39 | `DeltaMovementInput` |
| 7 | `ColourPaletteInput` | 40 (`0x28`) | `ViewInput` |
| 8 | `MiningAutopilotInput` | 41 | `DesignerMovementInput` |
| 10 | `CreativeContainerInput` | 42 | `EmoteInput` |
| 11 | `CreativeEnemyInput` | 44 | `ResearchInput` |
| 12 | `InventoryInput` | 45 (`0x2d`) | `BuildUpgradeInput` |
| 13 | `LogisticFilterInput` | 46 (`0x2e`) | `BuildInput` |
| 14 | `NewShipFromClassInput` | 47 (`0x2f`) | `DisassembleInput` |
| 15 | `NewShipInput` | 48 | `FleetTagInput` |
| 16 | `DisassembleShipInput` | 49 | `NewRepairInput` |
| 18 | `InteractInput` | 50 | `WarpDriveInput` |
| 19 | `SetItemInput` | 52 | `AbilityInput` |
| 22 | `SetWorkingInput` | 53 | `HotbarInput` |
| 25 | `CreateMissionInput` | 54 | `FireGroupInputCommand` |
| 26 (`0x1a`) | `BuildBlueprintInput` | 55 | `MapPinInputCommand` |
| 27 | `SetPinnedCostInput` | 56 | `RepairFrameInputCommand` |
| 28 | `SetBlueprintScreenshotInput` | 57 | `SetTextInputCommand` |
| 29 | `LoadBlueprintInput` | 58 (`0x3a`) | `ShipMovementInputCommand` |
| 30 | `AddBlockInput` | 59 | `InterstellarWarpCommand` |
| 31 | `RemoveBlockInput` | 60 | `ShipFiringInput` |
| 32 | `SetBlueprintTypeInput` | 61 | `ShipBoostingInput` |
| 33 | `PendingBlueprintInput` | 62 | `SetPilotingInput` |
| | | 65 (`0x41`) | `DockingInput` |

Ids known only from the name of the factory function that sets them (INFERRED):

| Id | Command |
|---|---|
| 9 | Creative family (`CreativeCommand`, `CreativeMission`, `CreativeUnlock`, `GiveCreativeItems`) |
| 17 | `MachinePortInput` (`TogglePortEnabled`) |
| 20 | `SetRecipeInput` (and the stack variant) |
| 21 | `SetConfigInput` (and a second variant) |
| 23 | `AssemblerInput` (`ManualCraft`, `CancelManualCraft`, `SetManualMode`) |
| 24 | `MissionInput` (`SetMission`, `AcceptMission`) |
| 43 | `ProsperitySelectInput` (and a creative variant) |
| 51 | `ShipTeleportInput` (`SetJumpTarget` and `CancelJumpTarget` share the id) |
| 63 | `SetAutoCollectFromMiners` or `SetFilter` (class not known) |
| 64 | `Setup` or `SetupTransient` (class not known) |

- OBSERVED: these classes have no id found. They set the id inside their `CreateInput` factories, so use the factories: `ElevatorInput`, `StationConsoleInput`, `DroneTaskInput`, `HUDMarkerInput`.
- UNKNOWN: which commands the host rejects when they come from a non-host client.

## 7. Networking and co-op

### Model

- OBSERVED: the game uses **deterministic lockstep**. Every peer simulates the whole world. Only player input travels over the network.
- OBSERVED: the classes involved: `DMSNetworkingSubsystem`, `DMSNetController` (base of `DMSPlayerController`), `DMSChannel` and `DMSBunch` (custom replication), `InputCommandBundle`, `DMSPlayerInput` and `NetPlayerInput`, `DMSInputFrame`, `DMSChecksumReport`, `NetworkChunk`, `WorldTransferBunch`. Matchmaking uses Steam (`BP_GameMode.SteamMatchmakingHelper`). UI classes: `WBP_JoinGameMenu`, `WBP_MultiplayerCapWarning`, `WBP_ChooseLoadingPlayer`.
- INFERRED: Unreal's normal actor replication is not used for gameplay. Actors are spawned locally from simulation objects.
- OBSERVED: what is shared and what is local.

| Shared (by input only) | Local only |
|---|---|
| The whole simulation: ships, machines, chunks, sectors, systems, players, inventories, docks, orders | Actors, UI, camera, HUD, widgets, Lua mod state, `settings.sav`, `Designs/*.sav` |

### Roles

- OBSERVED: if the controller is the authority, or the game is standalone, commands go straight into the simulation command buffer. Otherwise they are serialised and sent.
- OBSERVED: the controller keeps a network state struct (0x58 bytes). `GetNetworkState` returns a copy. It also holds the start frame, the target input delay (`SetTargetInputDelay` is a plain setter), `MaxInputDelay` and `CurrentInputFrame`. One flag blocks further world transfer bunches. `AccountOverride` is stored on the controller.
- OBSERVED: `DMSPlayerController.IsSinglePlayer()` exists. Use it, or `GetNetworkState`, to detect co-op.

### Controller RPCs

OBSERVED unless noted. All are on `DMSPlayerController` unless noted.

| Function | Role |
|---|---|
| `Server_RequestJoin(InAccountOverride)` | Client asks to join. The account name is stored on the controller. Has a validate function. |
| `Server_NotifyClientReady` | Client says it has loaded the world. Has a validate function. |
| `Server_PossessCharacter` | Possess request. Has a validate function. |
| `Server_ReceiveClientInput(UpdateIndex, NetPlayerInput)` | Client sends its input for a frame. Its validate function always returns true and does not check the content. |
| `Client_ReceiveRemoteInput` | Host sends another player's input to a client. |
| `Client_AddExistingPlayers` | Join catch-up. |
| `Client_AddRemotePlayer(NewRemotePlayerID, Nickname, StartingFrame)` | Adds a remote player object and input stream. No controller or pawn. |
| `Client_RemovePlayer` | Removes a player. |
| `Client_ResumeGame` | Unpauses after a load. |
| `Client_SetPlayerStartFrame(int)` | Sets the player's start frame. |
| `Client_ValidateDeterminism(Checksum)` | Compares the checksum with the local one. On a mismatch it calls the server response. |
| `Server_ValidateDeterminismResponse(Report)` | Client reports a mismatch. The report is a `DMSChecksumReport`: `Frame`, `FailureCode`, `ClientTotalObjects`, `ServerTotalObjects`. |
| `Client_ReceiveWorldTransferBunch(Index, Total, bunch)` | One part of the world sent to a joining client. Each record is 0x818 bytes. Ignored when the block flag is set. |
| `Client_ReceiveExistingInput` | Join catch-up for input (name only). |

- OBSERVED: `DMSNetController` also has `Client_ReceiveMessage` and `Server_ReceiveMessage`. INFERRED: chat or misc messages.
- OBSERVED: `BP_PlayerController_C` adds reliable `Server_PossessMapPawn`, `Server_PossessCameraPawn`, `Server_PossessDesignerPawn`, `Server_PossessSpectatorPawn`, plus `Server_Ping` and `Client_PingAck`.

### Frame flow

INFERRED, built from the observed pieces:

```
each peer, every controller tick:
    AddInputCommand puts commands in the pending list
        (type id 0 is dropped)
    flush: serialise type id + payload into a NetPlayerInput
        for frame F = CurrentInputFrame + TargetInputDelay
    authority or standalone: into the sim command buffer
    client: Server_ReceiveClientInput(F, bundle)

host:
    collects every player's bundle for F
    sends Client_ReceiveRemoteInput to the others

all peers:
    when frame F has every player's input
        -> process commands, then run the sim tick

now and then:
    Client_ValidateDeterminism(checksum)
    on mismatch -> Server_ValidateDeterminismResponse(report)
```

- INFERRED: lockstep waits for the slowest peer. A mod that stalls the frame path slows everyone.

### Joining a running game

Names are OBSERVED, the order is INFERRED:

1. Client calls `Server_RequestJoin`.
2. The host serialises the world (`NewSerializeWorld`).
3. The host sends it in `Client_ReceiveWorldTransferBunch` parts.
4. The client reads it (`NewDeserializeWorld`) and calls `FinalizeLoadGame`.
5. The client calls `Server_NotifyClientReady`.
6. The host sends `Client_AddRemotePlayer` for each player and the existing input (`Client_ReceiveExistingInput`).
7. `Client_SetPlayerStartFrame`, then `Client_ResumeGame`.

## 8. Saving and loading

- OBSERVED: `DMSWorldSubsystem` has `SaveGame`, `LoadExistingGame`, `StartNewGame`, `NewSerializeWorld`, `NewDeserializeWorld`, `FinalizeLoadGame` and `GetObject(id)`.
- OBSERVED: `DMSGameSubsystem` has `SaveBlueprint`, `LoadBlueprints`, `DeleteSaveGame`, `RefreshSaveGameList`, `GetRegistry`, `ReadConfig`.
- OBSERVED: `NewSerializeWorld` writes the world into a memory archive. It collects the sim objects, runs their serialise virtuals, writes each object, then closes the archive. The same function is used for the co-op world transfer (INFERRED from names).
- OBSERVED: save folder is `%LOCALAPPDATA%\MainSequence\Saved\SaveGames\`, not the Steam folder. Per slot:
  - `<slot>_M.sav`: about 232 KB, standard Unreal GVAS (`SavedMetaData`: `DisplayName`, `SlotName`, `ScreenshotData`, `Playtime`, `Version`, `LastPlayedDateTime`).
  - `<slot>_S.sav`: 4 to 16 MB, a custom binary dump of the world. It is not GVAS and not compressed.
  - Autosave: `<slot>-Autosave_M.sav` and `<slot>-Autosave_S.sav`.
  - Also `settings.sav` (GVAS `SavedSettings`), `EnhancedInputUserSettings.sav` and `Designs\<name>.sav` (GVAS `DesignSaveGame`, 300 to 500 KB).
- OBSERVED: `_S.sav` starts with `u32 0`, `u32 30`, `u32 N` (the class count, 155 in one save and 200 in another), then N entries of `[u32 length][ASCII class path with NUL]`. The paths are the distinct simulation classes in the world, in order: research state, faction manager, missions, statistics, star cluster, solar system, planets, belts, sectors, chunks, generators. After the table comes a preamble `1, 1, 5, 2, 3, 4, A, B` (`A` and `B` differ per save, likely object count and counters, INFERRED), then the object records.
- INFERRED: every saved object stores its class path. A mod class that appears in a save (for example `BPO_MiningBenchBlock`, `BPO_MinerModuleBlock`, `BPO_MiningMod`) must stay in a mounted pak. A save with a missing class fails or drops those objects.
- INFERRED: a co-op joiner needs the same pak. What happens without it is UNKNOWN. A class load failure while reading the world is the likely result.
- OBSERVED: do not write saves while the game runs. Never restore an old backup over the live save.
- UNKNOWN: whether autosave goes through `SaveGame`.
- OBSERVED: the class table alone is enough to build a "does this save need a missing mod pak" checker.

## 9. World update and tick structure

- OBSERVED: the simulation tick is a native consumer function. It has no `UFunction`, so UE4SS cannot hook it.
- OBSERVED: `DMSPlayerController:FixedUpdate` is a Blueprint event. `DMSWorld:AddFixedUpdate` and `RemoveFixedUpdate` exist. INFERRED: they register your own fixed update. Not tested.
- OBSERVED: the MSCore `events.on("tick")` loop runs on the game thread every 250 ms. It is safe. `ExecuteInGameThread` from UE4SS also works.
- OBSERVED: UE4SS hooks only see `UFunction`s that go through `ProcessEvent` (Blueprint and Lua call sites). Native-to-native calls skip them. Examples: the native part of `BuilderBase.ConfirmBuild`, and the controller tick call to `AddInputCommand`. For these use a C++ mod with an address, the exe size and a hash guard.
- OBSERVED: a pre-hook on `AddInputCommand` or `AddBuildCommand` sees commands that Lua or Blueprints send. It does not see commands sent from native UI code.
- OBSERVED: spawn hooks: `DMSActorBase:OnSpawn(SimulationObject, ESpawnReason)`, `OnHidden`, `OnUnhidden`, `DamageDestroy`; `DMSObjectBase:BP_OnSpawn`; `SaveableObject:BP_Initialize` and `BP_Reinitialize`; `DMSPlayerShipObject:OnInitialized`; `NotifyOnNewObject` for streamed actors.
- OBSERVED: build hooks: `EnterBuildingMode`, `EnterMachineBuildMode`, `ExitBuildingMode`, `ConfirmBuild` (hit 5 of 5 in a probe), `BuilderBase:UpdatePreview` (per frame, hookable).
- OBSERVED: other useful events: `ResearchCompletedDelegate`, `NewActiveResearchDelegate`, `Mission:OnMissionCompleted`, `OnStartPiloting`, `OnStopPiloting`, `OnPlayerKilled`, `OnPlayerRespawned`, `PowerGridLowPower`, `StationActor:OnTakeDamage`.
- OBSERVED: safe world-ready point: `FinalizeLoadGame`. Use it to set up mod state.
- OBSERVED: hooks on simulation Blueprint functions (such as `CustomGenerate`, `OnMissionCompleted`, `GetStartingDialogue`) run on every peer, inside the simulation.

## 10. What mods can read and hook

- OBSERVED (safe): read `SolarSystem`, `SolarSystemSector` (`ID`, `ParentID`, `ChunkLocation`, `GenParams`, `Biome`), `Planet`, `AsteroidBelt`, `SectorGenParams.ResourceRichness`, and item `GetName()`. Cache the result. Looping `FindAllOf` every second freezes the game.
- OBSERVED: the `CustomGenerate` functions of the four generator classes can be hooked (INFERRED: UE4SS can hook them since they are `UFunction`s). They run on every peer while a chunk is generated. Logging is safe. Touch only the `SpawnChunk` argument. Do not call `FindFirstOf` inside (known crash).
- OBSERVED: read-only net observation points (see the table): the implementations of `Server_ReceiveClientInput`, `Client_ReceiveRemoteInput`, `Client_ValidateDeterminism`, `Client_AddRemotePlayer`, `Client_ReceiveWorldTransferBunch`, `Client_ResumeGame`.
- INFERRED: changing world gen data (`GEN_*`, `BIOME_*`, `DAT_GenerationParams`, generator defaults) by pak patch only affects chunks that are not yet generated, and new worlds. Chunks already saved stay as saved. Every peer needs the identical pak.

## 11. Co-op rules for mods

OBSERVED unless noted. The consequences are INFERRED.

1. Never write properties of simulation objects (ships, machines, chunks, sectors, systems, players, inventories, docks, orders). The write is local, the checksum differs, the session desyncs. This includes `Throttle`, `bAutopilot`, `JumpTarget*`, orders, and the chunk applied and target levels on chunks that generate content.
2. Change the world only through input commands (`AddInputCommand`, `AddBuildCommand`). Use the `CreateInput*` factories, or write the type id by hand for `NewObject` commands.
3. A command's receiver must be a simulation id that is valid on all peers. Never use an actor-only id. Do not send commands for another player's player object.
4. Do not call, or rewrite the arguments of, the `Server_*` and `Client_*` RPCs: `RequestJoin`, `NotifyClientReady`, `PossessCharacter`, `ReceiveClientInput`, `ReceiveRemoteInput`, `ResumeGame`, `AddRemotePlayer`, `ReceiveWorldTransferBunch`, `ValidateDeterminism`, `SetTargetInputDelay`, `SetPlayerStartFrame`, and the Blueprint `Server_Possess*Pawn` and `Server_Ping`. The result would be a desync or a kick.
5. Hooks on simulation Blueprint functions may log. Any state change there must be identical on every peer, so do not do it from a mod.
6. Do not `SpawnObject` simulation classes (`BP_*Object`, `BPO_*`) outside the game's own paths. They get local ids, and that breaks saves and sync. Actor Blueprints can be spawned locally, but they have no simulation object.
7. Do not create a second or fake player object. It is a dead end and a desync.
8. `mb_chunkhold` uses the game's own set-level and apply calls (local) but is a co-op risk until tested with two peers. Never use it on the ship the player flies. It is capped at 150 chunks and guarded by address hash checks.
9. Mod classes that appear in saves need the same pak on every peer. Never remove them from a world that uses them.
10. Do not change gameplay data (recipes, costs, `GEN_*`, `ChunkGenRate`, `TargetInputDelay`) on one peer only.
11. Keep mod work off the network frame path. Do not do heavy work in controller tick hooks. Use the 250 ms game-thread loop.
12. Single-player-only features (instant sector warp, creative commands, direct teleports) should check `IsSinglePlayer()` or the network state, and refuse in co-op. This is a recommendation.
13. Always safe: UI windows, HUD, text, sound, camera, logging, reading actors and simulation objects, files in the mod folder.

## 12. Crash traps

- OBSERVED: calling a method on a freed `UObject` is a native crash. `pcall` cannot catch it. Check validity first. Streaming frees actor pointers, so cached actors go stale.
- OBSERVED: `FindFirstOf` or `FindAllOf` in a delayed callback (`ExecuteWithDelay`) after the world changed gives an access violation. Do not press Ctrl+R while callbacks are pending.
- OBSERVED: `FindAllOf` or reflection over all rocks every second freezes the game (about 39000 rocks). Cache for 5 to 10 s. `FindAllOf("Class")` returns nothing.
- OBSERVED: do not hook `ExecuteUbergraph_*`. It fires for every event.
- OBSERVED: `ShowWindow` or `ToggleWidget` with a null player object, or right after a world change, crashes.
- OBSERVED: do not write `PlayerCachedChunkLocation`. `LoadChunks` overwrites it.
- OBSERVED: do not raise `ViewDistance` or `ChunkLoadingDistance`. The chunk cube grows too big (it exploded at 270 km).
- OBSERVED: `KismetSystemLibrary` soft-path calls crash UE4SS. `LoadAsset` does not work. `GetText` on a hidden widget crashed.
- OBSERVED: a local write to a simulation property can desync co-op and may not stick (for example a character's Z).
- OBSERVED: a DLL change needs a full game restart. Lua can reload with Ctrl+R.
- OBSERVED: a compat file that reports every feature as `DISABLED` was a reporting bug in the mod (the summary was printed before the `done` flag was set). All checks had passed.

## Build-specific reference

| Item | Value |
|---|---|
| Image base | `0x140000000` |
| Start new game (`DMSWorldSubsystem`, exec) | `0x1441ce2a0` |
| Load existing game (exec) | `0x1441cd3b0` |
| Save game (exec) | `0x1441cdfe0` |
| `NewSerializeWorld` (exec / native, 1117 bytes) | `0x1441cd5b0` / `0x14426d170` |
| `NewDeserializeWorld` (exec) | `0x1441cd500` |
| `SolarSystem` functions (`GetStarCluster`, `GetStarDirection`, `GetStellarLocation`) | `0x1441fddf0` |
| `SolarSystem.ChunkMap` offset | `+0xe0` |
| `Chunk` offsets: generated flag / objects array / applied level / target level | `+0xfd` / `+0x70` / `+0xa0` / `+0xa4` |
| `SmallResourceAsteroidObject` offsets: `DamageToMine` / `Damage` / `ResourceItems` / `FloatingOreClass` | `+0x100` / `+0x104` / `+0x108` / `+0x118` |
| Player object position (read by `LoadChunks`) | `+0x94` |
| `DMSPlayerController.LoadChunks` (native) | `0x144212f00` |
| Get system / set level / apply (RVA) | `0x425f2e0` / `0x42512e0` / `0x424cbc0` (apply continues at `0x424d400`) |
| Same three at runtime in one tested session | `0x7ff72010f2e0` / `0x7ff7201012e0` / `0x7ff7200fcbc0` |
| `AddInputCommand` (thunk / native) | `0x144211920` / `0x1442609d0` |
| Command serialiser | `0x144264f00` |
| Process commands (sim) | `0x144269b40` |
| Receiver handler (virtual slot offset) | `+0x2c8` |
| Sim tick (native consumer) | `0x144267910` |
| Command type id offset on a command | `+0x28` |
| Receiver offset on a command | `+0x38` |
| Controller: authority value test | `this+0x168 == 3` |
| Controller: native net-state pointer | `+0x760` |
| Controller: pending commands | `+0x768` |
| Controller: network state struct (0x58 bytes) | `+0x898` to `+0x8f0` |
| Controller: start frame / target input delay | `+0x8f0` / `+0x8f4` |
| Controller: block-world-transfer flag | `+0x85c` |
| Controller: world transfer bunch array | `+0x878` |
| Controller: `AccountOverride` | `+0x888` |
| Controller: creative flag (sender) | `+0x370` |
| `GetNetworkState` | `0x144212ac0` |
| `SetTargetInputDelay` | `0x144213bb0` |
| `Server_RequestJoin` (thunk / impl) | `0x1442134a0` / `0x1442636b0` (calls `0x14426d670`) |
| `Server_NotifyClientReady` (thunk / impl) | `0x1442130e0` / `0x144263610` (calls `0x14426c1f0`) |
| `Server_PossessCharacter` (impl) | `0x144263620` (calls `0x1437f4bb0`) |
| `Server_ReceiveClientInput` (thunk / impl / validate) | `0x1442131f0` / `0x144263630` / `0x144263680` |
| Input frame buffer (called by input RPCs) | `0x14426c160` |
| `Client_ReceiveRemoteInput` (thunk / impl) | `0x144212150` / `0x1442610d0` |
| `Client_AddExistingPlayers` (impl / helper) | `0x144261050` / `0x141295d60` (via `this+0x868`) |
| `Client_AddRemotePlayer` (impl / helper) | `0x144261060` / `0x14426c0b0` |
| `Client_RemovePlayer` (impl / helper) | `0x144261390` / `0x14419e010` |
| `Client_ResumeGame` (thunk / impl / helper) | `0x144212550` / `0x1442613a0` / `0x14426d850` |
| `Client_SetPlayerStartFrame` (thunk / impl) | `0x144212570` / `0x1442613b0` |
| `Client_ValidateDeterminism` (thunk / impl) | `0x144212600` / `0x1442613c0` |
| Local checksum compare (via net state `+0xe8`) | `0x14426b830` |
| Send determinism response | `0x144210fb0` |
| `Server_ValidateDeterminismResponse` (thunk) | `0x144213560` |
| `Client_ReceiveWorldTransferBunch` (thunk / impl) | `0x144212350` / `0x144261110` |
| World transfer bunch record size | `0x818` bytes |
| `Client_ReceiveMessage` / `Server_ReceiveMessage` (property descriptors) | `0x147313188` / `0x147313198` |
| Native config queue for ship orders (`ConfigureShipOrdersInput`) | `0x144263570`, `0x144261ce0` |
| Property descriptor ranges (not struct offsets): sector and biome gen | `0x1473491e0` to `0x147349ef8` |
| Property descriptor ranges: chunk and region | `0x14737e390` to `0x14737e8a0` |
| Property descriptor ranges: `SolarSystem` | `0x14737fac0` to `0x147380300` |
| Property descriptor ranges: `WorldSettings` | `0x147393f00` to `0x1473945d0` |
| Property descriptor ranges: `GenerationParams` | `0x147314c70` to `0x147315300` |
| Sample session facts (mod compat check) | exe timestamp `0x6932bb03` |
| Serialise helpers (collect objects, per-object write) | `0x144267380`, `0x144265980`, `0x14421be70` |

## Unknown

- How the world seed turns into the cluster, systems and sectors (native, not traced).
- Whether far simulation chunks are generated on every peer or only by the host, and what `mb_chunkhold` does to determinism in co-op.
- What the checksum contains, how often it runs, and what happens on a failure (disconnect or popup).
- The layout of `NetPlayerInput`, `InputCommandBundle`, the 0x818-byte world transfer record, and the frame buffer behind the input RPCs.
- Struct member offsets of `SectorGenParams`, `Biome` and the feature rows (needs a usmap or a runtime reflection read).
- The per-object record layout in `_S.sav`, and how `NewDeserializeWorld` reads it.
- The player cap in the demo (a warning widget exists), host migration, and joining with a missing mod pak.
- Which commands the host rejects from non-host clients.
- The classes behind command ids 63 and 64, and how the game gates the creative commands.
- Whether autosave goes through `SaveGame`.
- The power grid formula.
