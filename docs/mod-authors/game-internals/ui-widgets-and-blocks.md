---
title: UI, widgets, actors and custom blocks
parent: Game internals
nav_order: 7
---

# UI, widgets, actors and custom blocks

Notes from reading the game's Blueprint assets (decoded to pseudo-code) and the game's native code. Each claim is tagged:

- **OBSERVED**: read in the decoded assets or the game's code.
- **INFERRED**: follows from what was read, not tested in the game.
- **UNKNOWN**: not found yet.

Build these notes apply to: `MainSequence-Win64-Shipping.exe`, exe size `149200896`, Unreal Engine 5.7. Addresses are listed only in the table at the end. They are build specific and change with every game update.

Inventory commands, machine configs and refunds are on [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}). This page covers the UI layer, the Blueprint actors that open it, and how to add your own block.

## 1. The big picture

- OBSERVED: the Blueprint layer is thin. Real game logic (simulation, orders, mining autopilot, crafting, combat, power) is native code in the `/Script/MainSequence` module. Blueprints add UI glue, mission scripting, sector generation recipes, HUD and input wiring.
- OBSERVED: asset counts in `/Game/Blueprints`: 299 `BPO_*` sim classes, 425 `WBP_*` widgets, 518 `BP_*` actors, 14 `BPC_*` components, 4 `BPI_*` interfaces. The 299 sim classes hold only 82 functions in total.
- OBSERVED: **pairing rule.** A thing `X` exists as a sim object (`BPO_X` or `BP_XObject`, saved, almost always empty) and as an actor (`BP_X`, the visuals and all Blueprint logic). Example: `BP_AssemblerObject` (sim, empty) and `BP_Assembler` (actor).
- OBSERVED: a machine is three parts. A `BP_X` actor (parent `Machine`), a `BPO_X` sim class, and a `BPC_MachineInteraction` component on the actor that opens a window.
- OBSERVED: a window is a `WBP_X` widget whose parent is `DMSWidgetBase`. 57 windows live in `Widgets/Windows`. Of the 425 widgets, only `DMSWidgetBase` children can be opened by the widget subsystem. Every window and popup is one.
- OBSERVED: the sim side is lockstep. Blueprint actors and widgets are local presentation. State changes go through input commands (see the inventories page).

{: .warning }
Writing to a sim object from Lua desyncs co-op. Writing to a Blueprint variable on an actor or widget is local only and is safe in that sense. When in doubt, send a command.

## 2. Reading the Blueprints

- OBSERVED: assets are cooked with **unversioned properties**. Property names and default values are not in the files. Tools show the structure (widget tree, class variables, functions, bytecode) but not defaults. For example, which window a machine opens is a class default and cannot be read from the file.
- OBSERVED: to read a default, read it at runtime from a live instance.
- OBSERVED: UE4SS path of a Blueprint function: `/Game/Blueprints/<dir>/<Asset>.<Asset>_C:<Func>`. Example: `/Game/Blueprints/Widgets/Windows/WBP_Dock.WBP_Dock_C:SetupWidget`.
- OBSERVED: event-graph events are not their own code. Each custom, bound or input event is a tiny stub function that copies its parameters into the widget's persistent frame, then calls `ExecuteUbergraph_<Asset>(entry offset)`. Hook the stub. Stub names look like `BndEvt__<Asset>_<Widget>_K2Node_ComponentBoundEvent_<n>_<Delegate>__DelegateSignature` (button handlers) and `InpActEvt_IA_<Name>_K2Node_EnhancedInputActionEvent_<n>` (input).
- OBSERVED: fixed point numbers (`PNum`) are `units x 65536`. A Blueprint value of `52428800` is 800 units.

## 3. The window contract

### 3.1 UDMSWidgetBase

OBSERVED: every window derives from the native class `UDMSWidgetBase`. Native functions, overridden in Blueprint:

| Function | Signature | Meaning |
|---|---|---|
| `SetupWidget` | `(InObject1: Object, InObject2: Object, out bSuccess: bool)` | Called when the window opens. Must set `bSuccess` to true. |
| `RefreshWidget` | `(RefreshType: EWidgetRefreshType)` | Called by the sim or native code to redraw part of the window. |
| `OnHideWidget` | none | Called when the window closes. |
| `CloseSelf` | none | Closes this window. |

- OBSERVED: every vanilla `SetupWidget` starts with `Cast<X>(InObject1)` and `Cast<Y>(InObject2)`. It sets `bSuccess` true only if the casts pass. On failure it returns false and does nothing else.
- OBSERVED: when `SetupWidget` returns false, `ShowWindow` returns null and nothing is shown. The native default `SetupWidget` returns false, so a Blueprint override that returns true is mandatory.
- OBSERVED: in vanilla windows, `InObject1` is usually the player object (`DMSPlayerObject`) and `InObject2` is the machine or vendor sim object. Some windows take only one (see section 6).
- OBSERVED: the native open path finds a cached widget of that class in the subsystem, or creates one. It then writes the outer object and the two objects into the widget, calls the Blueprint `SetupWidget`, and shows the widget only when `bSuccess` is true.
- OBSERVED: `ShowWindow` also reads a byte on the widget (the **Window** class default). Nonzero means the window path, zero means the popup path.
- INFERRED: a Blueprint-only `UserWidget` has none of these native members. The gate rejects it. Opening it through `ShowWindow` would also write into memory past a plain `UserWidget`.
- OBSERVED: each vanilla window binds its shell's `CloseWindow` event to a stub that calls `CloseSelf`.

### 3.2 Refresh types

OBSERVED: the enum strings are in the exe. The order below is INFERRED from declaration order.

| Value | Name |
|---|---|
| 0 | PROGRESS |
| 1 | CONFIG |
| 2 | QUEUE |
| 3 | HEALTH |
| 4 | AUTOMATION |
| 5 | HOTBAR |
| 6 | PALETTE |
| 7 | TRAVEL |
| 8 | FILTER |

- OBSERVED: `WBP_Dock.RefreshWidget` acts only on type `1` (CONFIG). Native code calls `RefreshWidget(CONFIG)` after a config input is applied.
- OBSERVED: only `CONFIG = 1` is confirmed. The other numbers are not.
- INFERRED: calling `RefreshWidget(1)` on a live window forces a redraw after your own config change.

### 3.3 Sim to UI notification

- OBSERVED: windows bind Blueprint functions to sim delegates. 20 widgets bind `RefreshWidgetDelegate` on a machine or station object (fired with a refresh type). 12 bind `Inventory.RefreshSlotDelegate`. 3 bind `RefreshResearchWidgetDelegate`.
- OBSERVED: only 11 of the 78 widgets that bind a delegate ever unbind it. Bound delegates are weak, so stale entries are skipped (INFERRED).
- OBSERVED: some widgets poll instead: `WBP_ShipStatus` (tick), `WBP_PowerOverview` (timer), `WBP_PowerGrid` (tick), `WBP_HUDMarkerManager` (tick).
- OBSERVED: the window shell classes are `WBP_Window_01` and `WBP_WindowBase` (`SetWindowName(Text)`, multicast `CloseWindow`), `WBP_WindowHeader` (`SetHeaderText`, `SetIcon`, `SetShowCloseButton`, `SetCloseWindowIcon`) and `WBP_Window_02` (close button only, used for popups).

## 4. Opening windows

### 4.1 Widget subsystem and player controller

| Call | Notes |
|---|---|
| `WidgetSubsystem:ShowWindow(WidgetClass, InObject1, InObject2, bool)` | Returns the widget (null when setup fails). |
| `WidgetSubsystem:ToggleWindow(WidgetClass, InObject1, InObject2, bool)` | Same arguments. Closes the window if it is already open. |
| `WidgetSubsystem:ShowPopup(WidgetClass, null, null, bool)` | Returns the widget. |
| `WidgetSubsystem:HideWindow`, `CloseCurrentWindow`, `HidePopup`, `ShowHUD` | Also seen. |
| `WidgetSubsystem.CurrentWindow` | Property. The window that is open now. |
| `DMSPlayerController:ShowWidget(WidgetClass, o1, o2)` | HUD-level widgets. |
| `DMSPlayerController:ToggleWidget(WidgetClass, o1, o2)` | Returns bool. Machines use this. |
| `DMSPlayerController:HideWidget`, `HideWidgetByClass`, `CloseWidgetOrPause` | Also seen. |

- OBSERVED: Blueprint code uses `ToggleWindow` and `ShowWindow`. Machines open windows through the controller's `ToggleWidget`.
- OBSERVED: windows chain on the return value. Example: `Cast<WBP_Signpost_C>(ToggleWindow(...))`, then call setup functions on it.
- OBSERVED: `WBP_CharacterHUD.OnWindowOpened` and `OnWindowClosed` are native events driven by the widget subsystem. They hide and show HUD pieces.

{: .warning }
Do not call `OnWindowOpened` or `OnWindowClosed` by hand. It desyncs the HUD visibility.

### 4.2 Machines and blocks (BPC_MachineInteraction)

- OBSERVED: `BPC_MachineInteraction` derives from the native `InteractionComponent`. It has these variables: `Widget Class` (a `DMSWidgetBase` class, default set per actor), `MachineObject` (the sim object), `OnInteract` (delegate) and `InteractionTextFormat`.
- OBSERVED: `Interact(InteractingPlayer)` does this: if `MachineObject.CanInteract()`, call `InteractingPlayer.ToggleWidget(Widget Class, InteractingPlayer.PlayerObject, MachineObject)`, then broadcast `OnInteract`.
- OBSERVED: `CanInteract` wraps `MachineObject::CanInteract`. The prompt is `Format(InteractionTextFormat, machine = Buildable.DisplayName)`.
- OBSERVED: the whole machine to window mapping is **one class default**, `Widget Class`, on the `BPC_MachineInteraction` instance of each machine actor.
- OBSERVED: a minimal machine actor (`BP_ShipAssembler` is the smallest) has two components, `DefaultSceneRoot` and `BPC_MachineInteraction`. Its `OnSpawn(SimulationObject, Reason)` sets `BPC_MachineInteraction.MachineObject = SimulationObject`, and that is all. `SetDisassemblyHighlight(bool, Context)` calls `SetRenderCustomDepth` on the component.
- INFERRED: you can set `Widget Class` on a live component to any `DMSWidgetBase` class. This needs the class object loaded.
- OBSERVED: three more interaction components exist. `BPC_InteractionComponent` only broadcasts `OnInteract`. `BPC_PilotSeat` calls `SetStartPiloting(Ship ID)`. `BPC_MachinePortInteraction` toggles ports through a `MachinePort` command.
- OBSERVED: **the camera ray.** `BP_CameraPawn` (the possessed pawn) traces from the camera each tick up to `ShipInteractionDistance`. If the hit is an `InteractionComponent` within `ShortInteractionDistance`, it shows the interaction prompt and stores the component in `Interaction`. If the owner is a `DMSActorBase` within `LongInteractionDistance` (any distance while piloting), it shows the actor's `DetailWidget` as a tooltip panel.
- OBSERVED: a custom machine needs an actor with an `InteractionComponent` that the camera ray hits, and an `OnInteract` handler or a `Widget Class`. A `DetailWidget` is optional.
- UNKNOWN: the trace channel and the default distances.
- OBSERVED: `BP_CameraPawn.Interaction:Interact(controller)` is a programmatic tap on F.

OBSERVED: blocks that do not use the machine pattern.

| Block | How it opens its window |
|---|---|
| `BP_CommandModule`, `BP_ScannerModule` | The block actor binds `DesignerTable` (a `BPC_InteractionComponent`) `OnInteract` to `OpenStationStatus`, which calls `ToggleWindow(WBP_StationInventory, PlayerObject, block sim)`. InObject2 is the **block**, so the window can show only frame data. |
| `BP_Cockpit_01` | `ToggleWindow` of `WBP_FireGroupConfig` or `WBP_ShipDamageReport` with the frame and null. |
| `BP_DockingCollar` | `ToggleWindow(WBP_ShipInventory, ...)`. |
| `BP_ShipyardBlockDock`, `BP_ShipyardBlockDock_02` | `WBP_Shipyard`. |
| `BP_Plaza_01` | `ToggleWindow(WBP_StationProsperity, ...)`. |
| `BP_SignWall` | `WBP_Signpost`, both signs. |
| `BP_TradeConsole_01` | `ShowWindow(WBP_StationProsperity, PlayerObject, Sim.GetFrame())`. |

- OBSERVED: delegates bound in `BeginPlay` through `CreateDelegate` (`OnInteract += OpenStationStatus`, `OnInteract += StartPiloting`) are not rebound if you change the function later. Hook the target function.

### 4.3 How a block gets its own menu

- OBSERVED: a vanilla block with its own menu has a **machine** in its `DefaultMachines` list. That machine's actor carries `BPC_MachineInteraction` with `Widget Class` set to the window. Examples: the dock block spawns `BPO_StationBlockDock` (window `WBP_Dock`), the shipyard core spawns `BPO_ShipyardBlockDock` (`WBP_Shipyard`), the plaza spawns `BPO_TradeConsole_01` (`WBP_TradeConsole`), and the ship container block spawns `BPO_ShipContainer`.
- OBSERVED: abandoned rooms spawn `BPO_LootContainer_01/02/03` at cells such as `(-12,0)` and `(8,-13)`.
- OBSERVED: the container window `WBP_Container` casts `InObject2` to `ContainerObject` and binds two inventory widgets to the container and player inventories. `WBP_StationInventory` casts `InObject2` to `BuildingBlockObject` and binds the shared frame inventory.
- OBSERVED: only one function named `GetInventory` exists in the exe (on `BuildingFrame`). A cloned `WBP_StationInventory` therefore cannot bind another inventory without bytecode surgery.

## 5. Player controller input

### 5.1 Gates

- OBSERVED: `BP_PlayerController` (parent `DMSPlayerController`) has the bool `CharacterInputEnabled`. Almost every handler starts with a check on it. `AllowCharacterInput(bool)` sets it.
- OBSERVED: `IA_FirePrimary` exits early while `WidgetSubsystem.CurrentWindow` is valid. There is no shooting with a window open.
- OBSERVED: the Statistics, Codex, Research, Prosperity and Fleet handlers all start with that `CharacterInputEnabled` check.
- INFERRED: a Lua hook cannot cancel a Blueprint event. To block these keys, set `CharacterInputEnabled = false`.

### 5.2 Input mapping contexts

- OBSERVED: `LocalPlayerSetup` adds the contexts `IMC_System`, `IMC_Common`, `IMC_Menus` and `IMC_Character` (priority 0, ignores all pressed keys until release).
- OBSERVED: `SetInputState(Movement, Piloting, Menus, Designer, Map, Building)` clears all mappings, adds `IMC_System` and `IMC_Common`, then per flag adds `IMC_Character` (0), `IMC_Ship` (1), `IMC_Designer` (1, or 0 for the map) and `IMC_Building` (0).
- INFERRED: a mod that needs its own key can add its own mapping context (needs an input mapping asset), or use UE4SS `RegisterKeyBind`.

### 5.3 Actions and what they do

OBSERVED: each action has one to three stub functions named `InpActEvt_IA_<Name>_K2Node_EnhancedInputActionEvent_<n>`, one per trigger event. All are hookable. The handled actions: Ability0 to Ability9, Boost, BuildMenu, BuildRotate, Codex, Confirm, DisassembleMode, Emote, Emote2, Enter, Escape, ExitBuildMode, Eyedrop, FirePrimary (two stubs), FireSecondary (two), FleetStatus, Interact (three), Map, MassDisassemble (three), Mouse, Movement, ProsperityOverview, QuickRetrieve, QuickSwitch, Roll, Scan (three), Sprint (two), Statistics, StopPiloting, Strafe, SwitchBuildMode, Throttle, ToggleResearchWindow, ToggleStatus, VerticalMovement, WarpJump.

| Action | What it does |
|---|---|
| `IA_Statistics` | `ToggleWidget(WBP_Statistics, null, null)` (needs `CharacterInputEnabled`). |
| `IA_Codex` | `ToggleWidget(WBP_Codex)`. |
| `IA_ToggleResearchWindow` | `ToggleWidget(WBP_ResearchTree_2)`. |
| `IA_ProsperityOverview` | `ToggleWidget(WBP_ProsperityOverview, FactionManager, null)`. |
| `IA_FleetStatus` | `ToggleWidget(WBP_Fleet, PlayerObject, null)`. |
| `IA_ToggleStatus` | `ToggleWidget(WBP_CharacterStatus, PlayerObject, null)`. |
| `IA_WarpJump` | If the piloted ship is valid and `CanWarp()` is true: `WidgetSubsystem.ToggleWindow(WBP_WarpDestination, PlayerObject, null, false)`. This is the ship warp, not a body jump. |
| `IA_BuildMenu` | If the arena wave manager is valid, leave the building mode. Otherwise `ToggleWidget(WBP_Building, null, null)`. |
| `IA_Scan` | `ShowWidget(WBP_ScanSelect)`. |
| `IA_Map` | `SetMapOpen(true, MapPawn)` when not open. Opening needs `CharacterInputEnabled` and first calls `ExitAllModes`. |
| `IA_Escape` | `CloseWidgetOrPause()`. The path was not followed to the end. |
| `IA_QuickRetrieve` | `QuickRetrieveFromViewTarget()`. If the view target is an assembler it sends `AddInventoryInteraction` with Primary = the assembler's output inventory, Secondary = the player's inventory, SlotIndex 0, Action 2. |
| `IA_Confirm` | If `PaintingMode >= 0` then `TryPaint`. Else if `DisassemblyMode` then `TryDisassemble`. Else next dialogue line. |
| `IA_Enter` | HUD `OnEnter`, otherwise focus the chat text box. |
| `IA_StopPiloting` | If `CharacterInputEnabled` and not in spectator mode: `AddInputCommand(PlayerObject.ID, CreateInputSetPiloting(false, 0))`. |
| `IA_Ability0` to `9` | `CreateInputAbility`. |
| `IA_FirePrimary`, `IA_FireSecondary` | `CreateInputShipFiring`. |

OBSERVED: `IA_Interact` has three stubs.

| Stub | Behavior |
|---|---|
| `_58` (ongoing, hold) | Calls `HUD.SetInteractionCharge(ElapsedTime)` every frame. When `ElapsedTime > 1.0` s: if the piloted ship is docked, `DockCommand(-1)` (undock). Otherwise, if `DockInteractionManager.ClosestDock` is valid, `DockCommand(ClosestDock.ID)`. |
| `_59` (press) | Casts the pawn to `BP_CameraPawn` and calls `Interaction.Interact(controller)`. This is the machine interaction from section 4.2. |
| `_60` (release) | `SetInteractionCharge(0)`. |

- INFERRED: which trigger event maps to which of the three stubs.
- OBSERVED: so hold F means dock or undock, and a tap on F means interact with whatever the camera ray hit. Docking is a controller command, not a sim input.
- OBSERVED: manual docking needs the player **body** inside the dock's trigger box. Autopilot docking does not.
- OBSERVED: the dock machine actor's trigger overlap calls `controller.AddPossibleDock(this.Dock)`, which adds to `DockInteractionManager` (`Docks`, `ClosestDock`, `Controller`). Its `Refresh` sets `ClosestDock` to null when the piloted ship is docked or on docking autopilot. Otherwise it picks the dock with the smallest distance among those with `IsAvailableForManualDocking()`. The prompt text is `Dock with {name}`.
- OBSERVED: `OnShipDocked(Ship, NewDocked)` on the controller fires for **every** ship actor whose dock state changes (AI and miner ships too). The controller filters on `Ship == PlayerObject.PilotedShip`.

### 5.4 Other controller functions

OBSERVED, callable from Lua as methods on the controller: `AllowCharacterInput(bool)`, `ExitAllModes`, `QueueDialogue(Text, bool)`, `SetMapOpen(bool, out MapPawn)`, `SetHUDHidden(bool)`, `SetFOV`, `EnterDesignMode(Blueprint, Frame, Inventory, ObjectId, bool, bool, X)`, `SetStartPiloting(shipId)`, `DockCommand(dockId or -1)`, `SetShipAutopilot(shipId, bool)`, `EnterBuildingMode`, `ExitBuildingMode`, `EnterBlueprintBuildMode`, `AddInventoryInteraction({...})`, `AddDisassembleCommand`, `AddQueueResearchCommand`, `SkipToOrder`, `ReorderShipOrders`, `DeleteOrder`, `LoadChunks`, `UnloadChunks`, `SendMessage(text)`, `SetRecipe`, `AddUnstuckCommand`.

OBSERVED: creative and dev switches go through controller natives: `SetCreativeMode(bool)`, `SetCreativeUnlockAllResearch`, `GiveCreativeItems`, `SetCreativeContainer(struct)`, `ForcePlayerRespawn`. They change sim state or save-visible flags (`PlayerObject.bCreative`).

{: .warning }
Do not call the controller's Server RPCs from Lua on a client: `PossessMapPawn`, `PossessCameraPawn`, `PossessDesignerPawn`, `PossessSpectatorPawn` (reliable), `Server_Ping`, `Client_PingAck`. Everything else on the controller is local presentation.

- OBSERVED: piloting is a sim command. `BPC_PilotSeat.Interact` calls `SetStartPiloting(Ship ID)`. `BP_PilotConsole.StartPiloting` sends `AddInputCommand(PlayerObject.ID, CreateInputSetPiloting(true, console id))`.
- OBSERVED: the controller `BeginPlay` creates `WBP_PlayStateStatus`, `WBP_CursorItemStack` (viewport z 2) and `WBP_SpawningScreen` (z 3, hidden when spawning completes), and spawns `BPO_DockInteractionManager`.
- OBSERVED: `ReceiveTick` (local controller) calls `RefreshDock()` and `WidgetSubsystem.UpdateSceneCompMarkers(...)`. That draws every `WBP_MarkerComponent` in the world, a per-frame cost that grows with marker count.

## 6. Game windows and their setup contracts

OBSERVED: the table is decoded from each window's `SetupWidget`. `O1` and `O2` are `InObject1` and `InObject2` and the class each is cast to. `-` means the window casts nothing and works with nulls. `Player` is `DMSPlayerObject`. `Opened by` lists the machine actors that reference the window (so the actor's `Widget Class` default), or the caller.

| Window | O1 | O2 | Opened by |
|---|---|---|---|
| `WBP_ArenaWaveSelect` | - | - | Player controller |
| `WBP_Armoury` | Player | `WorkbenchObject` | Armoury |
| `WBP_Assembler` | Player | `AssemblerObject` | Advanced assembler, assembler, enrichment chamber, growth chamber, refinery, relic synthesizer (supercomputer also maps here) |
| `WBP_AssemblerRecipeSelect` | - | `AssemblerObject` | `WBP_Assembler` (toggle) |
| `WBP_AstronomicScanner` | Player | `StellarCollectorObject` | Astronomic scanner |
| `WBP_BeltBalancer` | - | `SplitterObject` | Belt balancers 1 to 3 |
| `WBP_BiosampleCollector` | Player | `StellarCollectorObject` | Biosample collector |
| `WBP_Building` | - | - | `IA_BuildMenu` |
| `WBP_CharacterStatus` | Player | - | `IA_ToggleStatus` |
| `WBP_Codex` | - | - | `IA_Codex` |
| `WBP_Container` | Player | `ContainerObject` | Container, Container_2, LootContainer_01 |
| `WBP_Dock` | - | `Dock` | Industrial dock, station block dock, T0 dock console |
| `WBP_DroneBay` | Player | `DroneBayObject` | T0 drone bay |
| `WBP_Elevator` | Player | `ElevatorObject` | Elevator pods (ship and station) |
| `WBP_ElevatorConfig` | - | `ElevatorObject` | Elevator actor 02 |
| `WBP_FireGroupConfig` | `DMSPlayerShipObject` | - | Cockpit_01 |
| `WBP_Fleet` | Player | - | `IA_FleetStatus` |
| `WBP_ItemCrate` | Player | `ItemCrateObject` | Item crate |
| `WBP_LockedDoor` | Player | `DoorObject` | Abandoned door 01 |
| `WBP_MiningDroneBay` | Player | `MiningDroneBayObject` | Mining drone bay |
| `WBP_MissileLauncher` | Player | `MissileLauncherObject` | Missile launcher 01 |
| `WBP_NPCDialogue` | Player | `VendorObject` | Crew, captain |
| `WBP_NewShipWindow` | Player | `ShipyardObject` | `WBP_Shipyard` |
| `WBP_PowerOverview` | `BuildingFrame` | - | `WBP_RemoteStationStatus` |
| `WBP_ProsperityOverview` | `FactionManager` | - | `IA_ProsperityOverview` |
| `WBP_Reactor` | Player | `ReactorObject` | Reactor machine, Reactor_T0, Reactor_T1 |
| `WBP_RemoteShipStatus` | `DMSPlayerShipObject` | - | `WBP_Fleet` |
| `WBP_RemoteStationStatus` | `StationObject` | - | `WBP_Fleet` |
| `WBP_ResearchTree_2` | - | - | `IA_ToggleResearchWindow` |
| `WBP_Researcher` | Player | `ResearcherObject` | Astro, comp, electro, mat and quantum labs, relic analyzer (xeno lab also maps here) |
| `WBP_ShipAssembler` | Player | `ManualAssemblerObject` | Ship assembler, ship refinery |
| `WBP_ShipAutopilot` | `DMSPlayerShipObject` | - | `WBP_ShipStatus`, `WBP_RemoteShipStatus` |
| `WBP_ShipDamageReport` | `BuildingFrame` | - | Cockpit_01, `WBP_StationInventory` |
| `WBP_ShipInventory` | Player | `BuildingFrame` | Docking collar |
| `WBP_ShipReactor` | Player | `ReactorObject` | Ship reactors (Gallant, Prospector, Providence, Scout, small block) |
| `WBP_ShipStatus` | `DMSPlayerShipObject` | - | Player controller (open path not found) |
| `WBP_ShipUpgrades` | Player | `ShipUpgradeMachine` | Upgrades_Scout1, Upgrades_Scout2 |
| `WBP_Shipyard` | Player | `ShipyardObject` | Shipyard block docks, T1 and T2 shipyard consoles |
| `WBP_Signpost` | - | `SignpostObject` | Sign wall |
| `WBP_SmallHangar` | Player | `ShipyardObject` | Hangar dock 02 |
| `WBP_Splitter` | - | `SplitterObject` | Splitter 1 to 3 |
| `WBP_StationBuilding` | - | - | Player controller |
| `WBP_StationInventory` | Player | `BuildingBlockObject` | Command module, scanner module |
| `WBP_StationProsperity` | Player | `StationObject` or `VendorObject` | Plaza, trade console, station commander |
| `WBP_Statistics` | - | - | `IA_Statistics` |
| `WBP_Teleporter` | Player | `TeleporterObject` | Teleporter |
| `WBP_TradeConsole` | Player | `VendorObject` | Trade console 01 |
| `WBP_VendorBlueprintPurchase` | Player | `VendorObject` | `WBP_VendorBlueprints` |
| `WBP_VendorBlueprints` | Player | `VendorObject` | Armourer, botanist, inventor, shipwright vendors |
| `WBP_VendorMissions` | Player | `VendorObject` | Communications officer, mechanic, military liaison, requisition officer |
| `WBP_WarpDestination` | Player | - | `IA_WarpJump`, map pawn |
| `WBP_WarpDrive` | Player | `WarpDriveObject` | Warp drive, interstellar warp drive |
| `WBP_Workbench` | Player | `WorkbenchObject` | Workbench, workbench machine |
| `WBP_Workstation` | Player | `AssemblerObject` | Workstation assembler, workstation refinery |

- OBSERVED: `WBP_MinerList` is a `DMSWidgetBase` with no machine or controller opener found. `WBP_HUDMarkerList` and `WBP_MissionsPanel` are plain `UserWidget`s (HUD panels), not windows.
- OBSERVED: `WBP_DockSelectPopup.SetupWidget` calls `RefreshDocks` and always returns true.
- OBSERVED: the pilot console is not a window. It owns six world-space widget components (crosshair, hull, power, throttle, weapons primary and secondary, shields) rendered in the world.
- OBSERVED: windows send changes as input commands with the pattern `Command.CreateInput<X>(args)` followed by `AddInputCommand(receiverId, cmd)`. There are 171 such call sites. The receiver is a ship id, a machine or frame id, an order id, or the player id. See the inventories page for why the receiver matters.
- OBSERVED: windows that reuse one sub-widget: `WBP_Container`, `WBP_Dock` and `WBP_ShipAutopilot` embed `WBP_LogisticFilters`. `WBP_Dock` shows the filter widgets only when the logistics technology is unlocked.
- OBSERVED: `ResearchState.IsTechnologyUnlocked(tech)` gates parts of `WBP_Dock`, `WBP_Container`, `WBP_LockedDoor`, `WBP_TradeConsole` and `WBP_VendorBlueprintPurchase`. `DMSWorld.IsDemo` is read by `WBP_ResearchTree_2`.

{: .warning }
Opening a window with a wrong or null `InObject2` can leave a half-built widget whose handlers later call native methods on null sim references. Match the class in the table. Native calls such as `BuildingFrame` and `GetInventory` on invalid objects crash (INFERRED). A null object is valid only for the windows marked `-`.

## 7. HUD, missions and pins

- OBSERVED: `WBP_CharacterHUD` is the only `DMSHUDWidget`. It exposes `AddInteractionCharge`, `HideInteractionCharge`, `SetInteractionCharge` (the hold-to-dock ring), `HideInteractionPrompt`, `ShowInteractionPrompt`, `RefreshFactionDisplay`, `RefreshGravityWarning`, `CreateDamageDirectionWidget`, `ShowBuildInputHints`, `HideBuildInputHints` and `ClearBuildError`. The controller keeps it in `CharacterHUDWidget`.
- OBSERVED: the game state references `WBP_Chat`, `WBP_SaveSuccess`, `WBP_SavingInProgress`, `WBP_AutosaveCountdown`, `WBP_PopupErrorMessage`, `WBP_ConfirmSettings` and `WBP_ChooseLoadingPlayer`.
- OBSERVED: **toasts and alerts.** `BP_GameState:ShowPlayerAlert(InAlert)` shows a toast through `CharacterHUDWidget.WBP_PlayerAlerts.ShowAlert`. `ShowErrorMessage` opens `WBP_PopupErrorMessage`. `BP_PlayerController:QueueDialogue(Text, bool)` queues dialogue. `SetHUDHidden(bool)` hides the HUD, and `GameState.OnHUDHidden` fires.
- OBSERVED: **missions.** `WBP_MissionsPanel` shows the missions, `WBP_ObjectivesHUD` shows objectives. `BP_PlayerObject.AddInitialMissions` adds `BPO_NewIntroMission` with the tutorial, otherwise `BPO_AutomationMission` and `BPO_ShipUpgrades1Mission`. Objective events (added, updated, completed) forward to `CharacterHUDWidget.WBP_ObjectivesHUD`.
- OBSERVED: 19 of the 23 `Mission` BPOs have a graph. Each overrides `OnMissionCompleted`. The graph calls `Mission::AddMission(<next class>)` and queues dialogue lines held in the variables `NextSteps`, `NextSteps2`, `NextSteps3`. The 8 vendor missions are data only.
- OBSERVED: mission chain (`AddMission` edges): NewIntro to Automation and ShipUpgrades1. Automation to ComputationScience. ComputationScience to FuelEnrichment and ShipAutomation. FuelEnrichment to SlipstreamDrive and ShipUpgrades2. SlipstreamDrive to ElectroSci. ElectroSci to LargeStationConstruction, RelicDestroyer and Logistics3. Logistics3 to QuantumSci. QuantumSci to InterstellarWarpDrive. SilicaRefining to ComputationScience.
- OBSERVED: **pins and markers.** Actors that can show a marker implement the `BPI_HUDMarkerActor` interface (`GetHUDActor`, `GetScanItem`, `GetScanType`, `GetMaxScannableDistance`, `GetMarkerColour`). `WBP_HUDMarkerManager` (a `DMSWidgetBase`) creates and destroys markers (`CreateMarkerForObject`, `CreateHUDMarker`, `ShowHUDMarker(marker, lifetime)`, `DestroyHUDMarker`). Marker content widgets are `WBP_Marker_Enemy`, `Resource`, `SmallResource`, `Station`, `Sector`, `JumpTarget` and `DefaultPin`.
- OBSERVED: `IA_Scan` opens `WBP_ScanSelect`, which calls `CreateScan` and spawns `BPO_ScanTimer` (a plain object). After its timer it calls `PC.HUDMarkerManager.ShowHUDMarker(marker, lifetime)`.
- OBSERVED: the map pin is a command. `MapPinInputCommand{ObjectId, bShow, bHide, bDestroy}` goes to the player id.
- OBSERVED: world-space markers (`WBP_MarkerComponent`) are added with `WidgetSubsystem.AddSceneCompMarker(widget, maxDistance)` and removed with `RemoveSceneCompMarker`. Floating ore adds one per actor (maximum distance 15000) and removes it on `EndPlay`.
- INFERRED: every floating ore actor therefore adds to the per-frame marker update.
- OBSERVED: the HUD actors are `BP_HUDActor`, `BP_PinnedHUDActor`, `BP_SectorHUDActor` and `BP_ShipHUDActor`.

## 8. Hook points for mods

OBSERVED: everything below is a Blueprint function run through `ProcessEvent`, so a UE4SS hook sees it. Native to native calls are not seen. UE4SS paths follow section 2.

| # | Hook | Use |
|---|---|---|
| 1 | `/Game/Blueprints/Components/BPC_MachineInteraction.BPC_MachineInteraction_C:Interact` (also `CanInteract`, `BP_GetInteractionText`) | Every machine window opens here. Pre-hook: read `Widget Class` and `MachineObject`. Post-hook: the widget exists. |
| 2 | `WBP_<Window>_C:SetupWidget(InObject1, InObject2, out bSuccess)` at `/Game/Blueprints/Widgets/Windows/<Name>.<Name>_C` | Post-hook means the window just opened for that object. Add or restyle widgets, start polling. |
| 3 | `WidgetSubsystem:ShowWindow` / `ToggleWindow`, `DMSPlayerController:ToggleWidget` | The choke point for all windows. |
| 4 | `BP_PlayerController_C:InpActEvt_IA_*` stubs, and `BP_CameraPawn_C` input stubs | A post-hook means the key was pressed. |
| 5 | `BP_PlayerController_C:OnShipDocked`, `AddPossibleDock`, `RemovePossibleDock`, `BPO_DockInteractionManager_C:SetNewClosestDock`, `DMSPlayerController:DockCommand` | Docking. |
| 6 | `BP_PlayerController_C:OnStartPiloting`, `OnStopPiloting`, `BP_PilotConsole_C:StartPiloting` | Piloting. |
| 7 | `BP_Scout_C`, `BP_Prospector_C`, `BP_Gallant_C`: `OnJumpStartCharging`, `OnJumpStart`, `OnJumpEnd(bViewedExternally)` | A reliable "warp began" and "warp arrived" signal. |
| 8 | `BP_GameState_C:Autosave`, `StartAutosave`, `ShowSavingCompleteWidget`, `DMSWorldSubsystem:SaveGame(Name)`, `LoadExistingGame(Slot, bool, Str)` | Saving. The autosave name is `<slot>-Autosave`. |
| 9 | `BP_<Machine>_C:OnSpawn(SimulationObject, Reason)`, `BP_FloatingOre_C:OnSpawn`, `BP_Character_C:OnSpawn` | Spawn. 265 actor Blueprints have `OnSpawn`. Read `BPC_MachineInteraction.MachineObject` in a **post** hook. |
| 10 | `BP_GameState_C:RegisterAll` | A post-hook means the registries are filled. |
| 11 | `BP_GameState_C:ShowPlayerAlert`, `ShowErrorMessage`, `BP_PlayerController_C:QueueDialogue`, `SetHUDHidden` | HUD and alerts. |
| 12 | `BPO_*Mission_C:OnMissionCompleted` (23 classes), `BP_PlayerObject_C:AddInitialMissions` | Missions. |
| 13 | `BPO_*SectorGenerator_C:CustomGenerate(SpawnChunk, Seed)` | Sector generation. |

- OBSERVED: the sector generators (`BPO_StarterSectorGenerator`, `BPO_RichSectorGenerator`, `BPO_SectorGenerator_Iridium`, `BPO_Generator_FracturedSector`) override `CustomGenerate` with helpers `GenerateClass`, `GenerateAdjacentClass`, `GenerateResource`, `GenerateAdjacentResource`, `IncrementDistance`. Their data lives in class defaults: `AsteroidClasses`, `MediumAsteroidClasses`, `Resources`, `InitialClasses`, `MinDistance`, `MaxDistance`.
- OBSERVED: the rich sector recipe has 5 passes. Between passes the distance grows by 100, 150, 200 and 350 units. Each pass can add the relic station spawners `BPO_RelicStationSpawner_03` and `_04`, and one abandoned station.
- INFERRED: hooks on sim classes run on every peer. A side effect that changes sim state must be deterministic, or co-op desyncs. Read-only logging hooks are safe in principle.
- INFERRED: hooks on `CustomGenerate` run while chunks stream in. Touch only the given chunk and do not call `FindFirstOf`.

{: .warning }
Per-frame hooks are expensive: `BP_PlayerController_C:ReceiveTick` (dock refresh and marker update), `BP_CameraPawn_C:ReceiveTick` (a ray trace each frame), `BP_Character_C:ReceiveTick` and the ship ticks. If you hook one, return early in Lua. A slow timer is cheaper.

## 9. Building your own windows

### 9.1 What the base class requires

- OBSERVED: a window that the game can open needs the parent class `/Script/MainSequence.DMSWidgetBase`, the class default **Window = true**, and a Blueprint function `SetupWidget(InObject1, InObject2, out bSuccess)` that sets `bSuccess = true`. Call it with `(DMSPlayerObject, machine or block object)`.
- OBSERVED: a project that has no native `MainSequence` module (for example a plain Blueprint-only editor project) cannot author a child of `DMSWidgetBase` directly.
- OBSERVED: a Blueprint-only editor project can ship a widget with a plain `UserWidget` parent in a pak. The game loads it as an ordinary UMG widget (OBSERVED: the Mining Unit window, a UE 5.7.4 UMG cook, loaded in the game).
- OBSERVED: that kind of widget can be shown by adding it to the viewport yourself. The widget subsystem, the `OnWindowOpened` HUD hook and `CurrentWindow` do not know about it.

### 9.2 What works without the base class

OBSERVED, from the Mining Unit window (a cooked UMG pak plus a Lua loader):

- Load the class from Lua through the asset registry (`AssetRegistryHelpers:GetAsset(FAssetData)`). `LoadAsset` did not work for this.
- Create the widget with `WidgetBlueprintLibrary:Create`, then `AddToViewport(Z)` **first**. After that set the anchors (`SetAnchorsInViewport`), the alignment (`SetAlignmentInViewport`) and the position (`SetPositionInViewport`). Set before `AddToViewport` they silently do nothing, because the canvas slot does not exist yet.
- Sizes passed to a `SizeBox` are Slate units, that is pixels divided by the DPI scale. A viewport size read through `WidgetLayoutLibrary` may not return plain numbers in UE4SS. Check the types and log them.
- A `SizeBox` pasted from the editor needs `bOverride_WidthOverride` and `bOverride_HeightOverride` set to true.
- Layout-only widgets (named `Row_*`) cannot be found from Lua with `GetWidgetFromName`. Mark them **Is Variable** in the editor.
- A text read from an `FText` of a box that is not visible crashed the game once (after a modal pick). Read edit boxes only on the visible page.
- Call `IsValid` before any method on a widget. Do not keep a Lua reference to a closed window: it becomes an invalid object and a native call on it crashes.
- Load the class when you open the window, in the game thread, not at mod start. At mod start the engine, the asset registry and the game thread are not ready. A load failure at start with a one-time gate left the mod without its window.
- Do not use soft-path calls from `KismetSystemLibrary`. They crashed UE4SS.
- Hook `BPC_MachineInteraction_C:Interact` (section 8, row 1) and open your widget there. Read `self.MachineObject` to know which machine was used.
- Input stays enabled while your window is open unless you disable it. Use `AllowCharacterInput(false)` when it opens and `AllowCharacterInput(true)` when it closes. Otherwise input can stay disabled.
- Your Lua window is not a game window. `FirePrimary` is not blocked, and `OnWindowOpened` is not called (INFERRED: from section 3.1).

### 9.3 Making a cooked widget a real game window

- INFERRED, not tested: **cloning a vanilla window package** gives an openable window with vanilla logic. Vanilla windows already derive from `DMSWidgetBase`. Use a same-length rename (see section 10.2), including the sites `Default__X_C`, `ExecuteUbergraph_X` and `BndEvt__X_...`. Confidence about 85 percent.
- INFERRED, not tested: re-parenting your own widget to `DMSWidgetBase` by byte patching is a long job. It needs new imports for the super class, a real `SetupWidget` function that returns true (the editor's Python cannot author graph nodes or out parameters), the Window class default, and a fix for the unversioned property header indices. The indices shift because the base class adds its own properties before the `UserWidget` ones. Any inherited `UserWidget` property in your class defaults breaks. Confidence about 40 percent.
- INFERRED: same-length edits to a cloned vanilla window (title text, button text, colours, visibility defaults) are easy. Adding new widgets to its tree needs new exports and is hard without tooling.
- INFERRED: a cloned `WBP_Container` keeps the vanilla graph. You can hide the creative panel and the logistics filters through visibility defaults.
- INFERRED: the creative panel of `WBP_Container` can be repurposed. It sends `SetCreativeContainer{ContainerID, bCreative, bConsume, Item}` to the controller. You can hook that native call and swallow it. If you do not swallow it, the container becomes a creative infinite source for everyone.

## 10. Custom building blocks

### 10.1 Parts of a block

OBSERVED:

| Layer | Asset | What it holds |
|---|---|---|
| Sim class (saved, lockstep) | `Blueprints/BuildingBlocks/BPO_X`, parent `BuildingBlockObject` | Display name (string table key), icon, item cost, hull, weight, thrust, connectors, colliders, ship collisions, block bounds, mount points, grid transform, default machines. Imports its actor class `BP_X_C`. No mesh. |
| Actor (visual, local) | `Blueprints/BuildingBlocks/BP_X`, parent `BuildingBlock` | `StaticMeshComponent`s `ExteriorMesh` and `InteriorMesh`, optional light and door. `OnSpawn(Sim, Reason)` casts to the BPO and calls `SetPaletteMaterials`, `SetMaterialByName`, `PlayBuildAnimation`. |
| Mesh | `Meshes/BuildingBlocks/SM2_X_Ext`, `_Int` (and `_Glass`) | Cooked static mesh. |
| Material | Imports inside the mesh | Slots such as Colour, DarkGrey, Grey, Metal, White, Stickers. Instances like `MI_PaintedMetal_*` with parent `MM_Interior`. |
| Buildable data asset (machines only) | `Data/Buildables/DAT_X` | Name, display name, icon, item cost, class, preview mesh, detail widgets. |

- OBSERVED: blocks, machines and items are all static meshes (518 static mesh packages, 15 skeletal meshes: characters and xeno enemies).
- OBSERVED: 109 `BuildingBlock` actors follow the pattern `OnSpawn`, `PlayBuildAnimation`, `BP_SetAsDesignerBlock(bShowHolograms)`, `RefreshMaterials`, `SetShowBuildingGuides`, `ShowConnectorPreview`, `HideConnectorPreview`, `SpawnDoor` (docks). Machines (83) have `OnSpawn`, `SetWorking(bool)`, `SetMachineStatus`, `SetPortEnabled`, `OnProgressStart`, `RefreshLowPower`, `RefreshAllPorts`, `SetDisassemblyHighlight(bool, EBuildingContext)`.
- OBSERVED: all 117 block sim classes derive from `BuildingBlockObject` directly (one from `ShipThruster`) with zero functions. They are pure data.
- OBSERVED: `BuildingBlockObject` own properties, in unversioned header index order: 0 DisplayName, 1 Icon, 2 Description, 3 FunctionalType, 4 ItemCost, 5 bAllowRotate, 6 MirrorClass, 7 bStationBlock, 8 bShipBlock, 9 bLockedInDemo, 10 Hull, 11 Weight, 12 Thrust, 13 BlockDetails, 14 Connectors, 15 Colliders, 16 ShipCollisions, 17 BlockBounds, 18 MountPoints, 19 bCapturePlayerAnchor, 20 RespawnLocation, 21 bSafeRespawn, 22 PlacementOffset, 23 GridTransform, 24 DefaultMachines, 25 DefaultMachineIDs, 26 AttachedMachines, 27 AttachedDecorations, 28 PaletteID. The parent class properties start at 29.
- OBSERVED: a block cell is 800 x 800 uu wide (8 m), the horizontal origin is the cell centre (mesh bounds measured). Block bounds are about plus or minus 3.9985 units. One sim unit is 100 UE units (INFERRED).
- OBSERVED: the machine cell is finer than the block cell. Default machine positions use machine units: the dock sits at `(16,0)`, rooms about plus or minus 13.
- OBSERVED: `BP_BuildPreviewActor.SetPreviewMeshByName(Name)` sets the placement ghost mesh through a `MeshMap`.
- INFERRED: `ESpawnReason` values `0` and `1` skip the construction material effect (loaded or streamed in). Any other value plays it (newly built). `SetBuildProgress(float)` drives the material scalar `Progress`.

### 10.2 Cloning a block

- OBSERVED: the sim class (`BPO_X`) cannot be authored in a Blueprint-only editor project, because its parent `BuildingBlockObject` lives in `/Script/MainSequence`. The only route is to clone a vanilla `BPO` and patch bytes.
- OBSERVED: the clone is a **same-length byte patch**. Pick a donor block with the footprint you want, and rename it to a name of exactly the same length. Replace the name in the name table (both the ASCII path and the short object name). Stale name hash bytes after a rename are accepted by the game.
- OBSERVED: the same rule applies to the actor clone. Rename `BP_X` to a same-length name. Update the actor import in the clone's `BPO`. In the `BP` package replace the two mesh paths with same-length names.
- OBSERVED: a rename that changes the export or name count needs a proper re-serialise. Same-length patching is the only safe edit without a schema.
- OBSERVED: a clone that keeps the donor `BPO` keeps the donor's footprint, colliders and connectors for free. Only a new footprint needs new collider numbers.
- OBSERVED: the Mining Unit block is a clone of the workstation block (the sim class), with its own actor whose `BPC_MachineInteraction` opens the mod's window. The actor side is only `DefaultSceneRoot`, `BPC_MachineInteraction` and an `OnSpawn` that sets `MachineObject`. No tick, no HUD code.
- OBSERVED: the sim stores the class path in the save. Once a save has a placed block of your class, removing the pak or renaming the class path breaks that save (how exactly: UNKNOWN).

{: .warning }
Never rename a shipped class path. Keep the pak mounted. Test on a throwaway save.

### 10.3 Registry and research unlock

- OBSERVED: all content is registered at `BP_GameState.ReceiveBeginPlay` in `RegisterAll()`, through `DMSGameSubsystem.GetRegistry()`. The data assets are `DAT_ItemRegistry`, `DAT_RecipeRegistry`, `DAT_ResearchRegistry`, `DAT_BuildableRegistry`, `DAT_CharacterIdRegistry`, the seven bullet data assets and `DAT_MinorDamage`. They are read **once**.
- OBSERVED: a pak that overrides a registry data asset at the same path takes effect. The higher pak chunk number wins (proven for the building block registry).
- INFERRED: a new item, recipe or buildable must be in those arrays. Two routes: a pak override of the registry data asset, or calling `DMSRegistry.Register*` with an extra data asset after `RegisterAll`.
- OBSERVED: a new block also needs a research unlock, or a cheat or dev menu unlock for first tests. The Mining Unit block did this with a registry patch plus a research array patch (names only change).
- OBSERVED: `BuildingBlockObject.GetDefaultMachine` reads the machines from `BuildBlueprint.Blocks[].DefaultMachines`.

### 10.4 Default machines

OBSERVED layout, in a `BPO` class defaults (unversioned bytes):

- `Connectors` (index 14) elements are the struct `BuildBlueprintEntry` with 5 properties (Class, Transform, MachineID, PaletteID, SerializedMachineConfig).
- `DefaultMachines` (index 24) elements are the struct `MachineSpawnData` with 4 properties (Class, Transform, Machine, MachineID).
- A transform is `GridTransform {GridLocation IntPoint, Height, GridRotation}`. In the bytes, a mask byte says which fields are zero, and only the present fields follow.
- Vanilla `MachineSpawnData` elements omit `MachineID`. The id is assigned when the machine spawns. `DefaultMachineIDs` is filled by the game and saved.
- OBSERVED: for vanilla blocks with a default machine, the machine class import is inside the `CreateBefore` list of the main export.
- INFERRED: there is no overlap check for default machines. The plaza trade console sits at the block centre.

{: .warning }
**Do not append a machine to the `Connectors` array.** An earlier attempt did this for a container, and the game crashed when the designer applied the edit. Connector entries are `BuildingBlockObject` classes. A `ContainerObject` class fails that cast, and a null pointer is read in the designer apply path (INFERRED, 75 percent). Put a machine in `DefaultMachines` (index 24) instead. The faulting address was never captured (no crash dump survived).

- OBSERVED: a second suspect of that crash: the clone also renamed the actor import against an unchanged actor package. That leaves a dangling actor class. Keep the actor import in step with the actor clone package (INFERRED, 35 percent).
- OBSERVED: the layout fix. In the unversioned header, add the fragments for index 24 (and 29), grow the header, insert the array after `bSafeRespawn`, add the names, imports and the preload entry, and grow the export's serial size. A new element is `80 09 0c` (machine and id zero), a class reference, then a transform (`80 07 07` for cell `(0,0)`, or `80 07 06` plus X and Y).
- UNKNOWN: whether the fixed layout builds without crash in the game. It is implemented, not yet tested in a run.

### 10.5 Actor, mesh and material pipeline

- OBSERVED: a mesh made in a UE 5.7.4 Blueprint-only project, cooked with the versioned flag, loaded in the game for UMG. A static mesh has never been loaded this way (UNKNOWN until tested).
- INFERRED: a new mesh is cooked, then the cloned actor imports are retargeted to it by same-length name.
- OBSERVED: new materials are a trap. The 5.7.4 cook emits project shader archives, which must not be packed (they would shadow the game's). A new base material has no shader maps in the game, expect black output or a crash. A material instance whose parent is a game material and which uses static switch variants has the same problem.
- INFERRED: reference only game materials, through stand-in assets created at the exact game path in the editor project and **not** packed. Safe parents that exist in the game: `MM_Interior`, `M_BasicEmissive`, `M_BasicUnlit`. `/Engine/EngineMaterials/WorldGridMaterial` also exists.
- INFERRED: to keep the player colour palette, name the material slots like the donor (Colour, DarkGrey, Grey, Metal, White).
- INFERRED: import with Nanite off and UV0 present. The game's block materials sample a trim sheet, so a plain cube UV will not match its colours. Use an unlit or emissive game material for a flat colour.
- INFERRED: the placement ghost for machines is a separate `PreviewMesh` on the buildable data asset.
- UNKNOWN: whether the game uses Nanite.

### 10.6 Pak chunk naming and packing

- OBSERVED: the game uses IoStore containers (`.utoc`, `.ucas` and a stub `.pak`). Pack mods with `retoc to-zen`, which writes the same triple. `retoc verify` checks container sanity only, not that the game loads it.
- OBSERVED: mod containers are named `pakchunk<N>-<Name>_P`. Vanilla chunks are `pakchunk0`, `pakchunk10`, `pakchunk1000`. The Mining Unit window uses `pakchunk98-MiningUnitUI_P`, a cloned block pack used `pakchunk99-ClonedBlock_P`. Slot `97` was chosen for a mesh pak. Install into the game's `Content/Paks` folder with the game closed.
- OBSERVED: for a path that is new (not a vanilla path), mount order does not matter. For a same-path override, the higher chunk number wins.
- OBSERVED: a container has one `--version`. Packages extracted from the game (legacy extract) carry 5.6-style headers and need `--version UE5_6 --no-parallel` (`UE5_7` fails with a buffer error on those). Packages cooked in the 5.7.4 editor use `UE5_7`. Do not mix them in one container. Use two containers.
- OBSERVED: an asset reached by a hard reference from an already loaded package needs no asset registry entry (blocks reach the actor through the registry, the mesh through the actor). A UMG pak loaded by path was found by Lua without one (via the asset registry helper; see section 9.2).
- OBSERVED: do not use unversioned cooks. The working pipeline uses versioned cooks.
- OBSERVED: a `LogicMods` folder exists in `Content/Paks` and is not used by these mods.

{: .warning }
**Co-op.** The sim is lockstep and the class paths are in the save. Every player needs the same block pak mounted (INFERRED), or the client cannot load the class. A UMG-only pak with no sim classes is safe. A pure mesh override that leaves the sim numbers unchanged is harmless to a client without it. A client without the pak joining a host with it is UNKNOWN and not supported.

## 11. Block windows: what a native container block needs

OBSERVED, from a design for a block with its own inventory:

- The natural native pair is a **container machine in `DefaultMachines`** (a `ContainerObject`) plus `WBP_Container`, opened through the machine's own interaction. No Lua opener is needed.
- Cloning the actor (for example the large container actor) lets you change its mesh. Keep its sim actor import identical (the vanilla large container imports its actor class from the package `BP_Container` and works in vanilla; a clone must keep that identical).
- `WBP_Container` shows the creative panel only when `PlayerObject.bCreative` is true. `RefreshCreative` decides it with a jump on that flag.
- A cloned `WBP_StationInventory` gives the station console look but binds the shared 24-slot frame inventory, so it has no stack bonus.
- INFERRED: a block that must show an inventory with a custom size can use a container with a larger `StackSizeBonusMultiplier`. The multiplier is fixed at spawn (see the inventories page).

## 12. Gotchas

OBSERVED unless noted.

1. Never call a Blueprint stub or custom event on a class default object, or on an object without a persistent frame. The stub writes into that frame and the ubergraph reads it. Use a live instance (from `FindAllOf` or the hook's `self`). This holds for events with parameters too.
2. Never call or hook `ExecuteUbergraph_<Asset>`. The argument is a raw bytecode offset. A wrong value jumps into the middle of a statement (INFERRED crash). It fires for every event of that Blueprint. Hook the named stub (`OnSpawn`, `ReceiveBeginPlay`, `InpActEvt_*`).
3. Do not call `BndEvt__` or `InpActEvt_` stubs by hand unless you pass real parameters.
4. `ToggleWidget` and `ShowWindow` with a null player or object only work for windows that need none (`WBP_WarpDestination` passes a null object). `WBP_TradeConsole` passes a non-null frame.
5. Do not open windows from a delayed callback after the world changed. This is the same access violation class as other delayed-callback crashes.
6. Do not call `FindFirstOf` from a delayed callback.
7. `OnShipDocked` fires for all ship actors. Compare the ship with `PlayerObject.PilotedShip`. Never write sim objects inside the hook.
8. Do not spawn a `BP_*Object` sim class outside the game's own paths (ids and saves). An actor Blueprint (`BP_X`) can be spawned locally but has no simulation object (`GetSimulationObject` is null). The casts are guarded, so it does not crash (guards read, no run test).
9. The `IA_Interact` stubs differ. `_58` runs every frame while held, `_59` is the press. For machines hook `BPC_MachineInteraction_C:Interact` (one call per use).
10. Same-length byte patching is the only safe Blueprint asset edit without a schema. A schema file for the unversioned properties would allow more.
11. Do not trust asset tool defaults for class defaults without a schema. Read component defaults at runtime.
12. Close your own window through the shell (`CloseWindow` or `CloseSelf`) or call `AllowCharacterInput(true)`. Otherwise input can stay disabled.
13. A vanilla window's `SetupWidget` cast failure is silent. The rest of the window stays at its defaults.
14. Do not call the creative natives to unlock things on a save you care about (section 5.4).

## Build-specific reference

Image base `0x140000000`. Each native function has an exec thunk (the address that Blueprint calls) and a native body.

| Item | Thunk | Native |
|---|---|---|
| `DMSPlayerController:DockCommand` | `0x144212790` | `0x144261d90` |
| `DMSPlayerController:ToggleWidget` | `0x144214030` | `0x144264370` |
| `DMSPlayerController:ShowWidget` | `0x144213cf0` | `0x144263d70` |
| `DMSPlayerController:AddInventoryInteraction` | `0x144211ac0` | wrapper call `0x14659d6e0` |
| `DMSPlayerController:AddInputCommand` | `0x144211920` | `0x1442609d0` |
| `DMSPlayerController:SetStartPiloting` | `0x1442117e0` | not resolved |
| `DMSPlayerController:EnterBuildingMode` | `0x144212920` | `0x144262020` |
| `DMSPlayerController:ExitBuildingMode` | `0x144212a80` | not resolved |
| `DMSPlayerController:ConfirmBuild` | `0x1442126a0` | not resolved |
| `WidgetSubsystem:ShowWindow` | `0x14420ec50` | `0x1442700c0` |
| `WidgetSubsystem:ToggleWindow` | `0x14420ede0` | `0x144270560` |
| `WidgetSubsystem:HideWindow` | `0x14420e3f0` | `0x14426f880` |
| `WidgetSubsystem:AddSceneCompMarker` | `0x14420ddc0` | `0x14426f410` |
| `WidgetSubsystem:CloseCurrentWindow` | `0x14420de90` | not resolved |
| `DMSWorldSubsystem:SaveGame` | `0x1441cdfe0` | - |
| `DMSWorldSubsystem:LoadExistingGame` | `0x1441cd3b0` | - |
| `DMSWorldSubsystem:StartNewGame` | `0x1441ce2a0` | - |
| `DMSWorldSubsystem:GetDMSWorld` | `0x1441cd000` | - |
| `DMSActorBase:GetSimulationObject` | `0x1441bd370` | `0x14425e860` |
| `DMSPlayerShipObject:CanWarp` | `0x1441c7c00` | `0x1442861a0` |
| `DMSPlayerShipObject:CollisionTest` | `0x1441c7c30` | not resolved |
| `InteractionComponent:Interact` (Blueprint event stub) | `0x1441d8b50` | Blueprint implemented |
| `InteractionComponent:CanInteract` (Blueprint event stub) | `0x1441d8a90` | Blueprint implemented |
| `InteractionComponent:BP_GetInteractionText` (Blueprint event stub) | `0x1441d8a30` | Blueprint implemented |
| `BuildingBlockObject:GetDefaultMachine` | `0x1441aad70` | `0x144272370` |
| Widget setup wrapper (writes outer and the two objects, calls `SetupWidget`) | - | `0x144252310` |
| `ShowWindow` inner function (cache lookup and create) | - | `0x14426f5d0` |
| Widget create | - | `0x143356030` |
| `SetupWidget` call helper | - | `0x1441ccbb0` |
| `UDMSWidgetBase` layout (offsets) | outer `+0x330`, `InObject1` `+0x338`, `InObject2` `+0x340`, Window flag byte `+0x328` (also reads `+0x329`, `+0x348`), widget vtable slot at `+0x2c0` | |
| Cached widgets in the widget subsystem | offset `+0x40` | |
| `BuildingBlockObject` property table | `0x1472f44f0` | |

## Unknown

- Whether a real `DMSWidgetBase` child can be produced from a Blueprint-only project by byte patching (about 40 percent, section 9.3).
- Defaults of components and class defaults: which window each machine's `Widget Class` is, widget visibilities, interaction text. Needs a schema for the unversioned properties or a runtime read.
- Order of the refresh type numbers beyond `CONFIG = 1`.
- Which trigger event maps to each of the three `IA_Interact` stubs.
- The camera ray's trace channel and the default interaction distances.
- The `IA_Escape` path to the pause menu, and how `WBP_ShipStatus` is opened.
- Values of `ESpawnReason` (only `0` and `1` versus other is inferred).
- Whether the fixed default-machine layout builds in the game without a crash, and the faulting address of the earlier crash.
- Whether a cooked static mesh from a mod pak loads, and whether the game uses Nanite.
- Whether the game accepts a modified `RefreshCreative` jump in a cloned `WBP_Container` (no checksum seen).
- Whether a client without a block pak can join a host that has it.
- Which creative and dev settings entries are visible in the shipping UI (a class default).
- What happens to a save when the pak of a placed block is removed.
