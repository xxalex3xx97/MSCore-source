---
title: Safe and objects
parent: Lua API
grand_parent: Mod authors
nav_order: 2
---

# Safe and objects

`safe` checks and reads game objects. `resolve` holds the class and path names. Use these instead of your own strings.

## safe

| Function | Args | Returns | Notes |
|---|---|---|---|
| `safe.try` | `label, f, ...` | `ok, result` | Same as `try` from `MSCore.logger`. |
| `safe.val` | `v` | value | For userdata, tries `v:get()` then `v:Get()`. Other values are returned unchanged. |
| `safe.nameOf` | `x` | string | `x:ToString()`, or `tostring(x)` on failure. |
| `safe.valid` | `o` | boolean | `true` only when `o` is userdata and `o:IsValid()` is `true`. Call it before any method on a game object. |
| `safe.objName` | `o` | string | Object name, or `"?"` when `o` is not valid. |
| `safe.live` | `...` class names | object or `nil` | First valid hit of `FindFirstOf` over the class names given. No logging. Safe to call every tick. |
| `safe.rd` | `f` | string | `tostring(f())`, or `"?"` when `f` errors or returns `nil`. |

## resolve

`MSCore.resolve` is the one place for class and path names. Do not repeat these strings in your mod.

| Field | Value |
|---|---|
| `player` | `{ "DMSPlayerObject", "BP_PlayerObject_C" }` |
| `controller` | `{ "DMSPlayerController", "BP_PlayerController_C" }` |
| `controllerPath` | `"/Script/MainSequence.DMSPlayerController"` |
| `invWidget` | `"WBP_CharacterStatus_C"` |
| `missionsWidget` | `"WBP_MissionsPanel_C"` |
| `item` | `"Item"` |
| `research` | `"ResearchTechnology"` |
| `umg` | `"/Script/UMG."` |
| `clickHook` | `"/Script/UMG.Widget:ForceLayoutPrepass"` |
| `ship` | `{ "DMSPlayerShipObject" }` |
| `console` | `"PilotConsoleObject"` |
| `benchBlocks` | `{ "BPO_MiningBenchBlock_C", "BPO_MinerModuleBlock_C" }` |
| `shipProps` | `{ id = "ID", dock = "Dock", dockId = "DockID", maxHull = "MaxHealth" }` |
| `dock` | `"Dock"` |
| `sector` | `"SolarSystemSector"` |
| `hullFns` | `{ "GetCurrentHull" }` |

`player`, `controller`, `ship` and `benchBlocks` are lists of candidate names. Pass them to `safe.live` with `table.unpack`.

## Example

```lua
local MSCore = require("mscore")

local ship = MSCore.safe.live(table.unpack(MSCore.resolve.ship))
if MSCore.safe.valid(ship) then
    local hull = MSCore.safe.rd(function() return ship:GetCurrentHull() end)
    print("hull: " .. hull .. "\n")
end
```

## Gotchas

- Call `safe.valid(o)` before any method on a game object. A call on a dead object can crash the game, and `pcall` cannot catch that.
- `safe.live` returns the first valid match or `nil`. It does not log, so it is safe in the tick loop.
- `safe.rd` never throws. Use it for read-only values that may be missing.
