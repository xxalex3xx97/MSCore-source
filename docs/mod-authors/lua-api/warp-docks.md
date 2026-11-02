---
title: Warp and docks
parent: Lua API
grand_parent: Mod authors
nav_order: 7
---

# Warp and docks

`warp` sends a ship on a teleport warp and lists sectors. `docks` lists docks. `find_item` looks up an item.

{: .warning }
**Not verified in game:** through MSCore (API 2). `docks` and `find_item` are also not verified through MSCore.

## warp

Needs the native DLL.

| Function | Args | Returns | Notes |
|---|---|---|---|
| `warp.send` | `sid, sys, ch, arr, wrap, set_target` | `ok, err` | Teleport warp for ship `sid` into solar system `sys`. `ch` = chunk `{X,Y,Z}`. `arr` = raw arrival location `{X,Y,Z}`. `wrap` = wrap in piloting on/off. `set_target` = set the jump target. |
| `warp.pinned_sector` | - | number | Last map-pinned sector id. `0` when none or no DLL. |
| `warp.sectors` | - | list | Every `SolarSystemSector`, cached 5 s. Each entry: `id`, `sys`, `ch` (`{X,Y,Z}`), `he` (half extents `{X,Y,Z}`), `name`, `warp_dist` (or `nil`), `ores` (list of item UObjects). |

The warp follows the manual warp the player sends: `SetPilotingInput` on, `ShipTeleportInput`, `SetPilotingInput` off. An instant warp used no warp fuel in the MiningBench in-game test.

## docks

| Function | Args | Returns | Notes |
|---|---|---|---|
| `MSCore.docks` | - | list of `{ id, name, obj }` | Every dock. Not cached: each call runs `FindAllOf`. Call it only on a user action. |

## find_item

| Function | Args | Returns | Notes |
|---|---|---|---|
| `MSCore.find_item` | `res` | item object, or `nil, why` | Matches the object name, the display name (case-insensitive) or a numeric ID. Zero matches or several matches return `nil` with the reason. The item list is cached 5 s. |

## Example

```lua
local MSCore = require("mscore")

local iron, why = MSCore.find_item("Iron")
if not iron then
    print(why .. "\n")
end

for _, sec in ipairs(MSCore.warp.sectors()) do
    print(sec.id .. " " .. sec.name .. "\n")
end
```

## Gotchas

- Undock the ship before you warp. MSCore does not check the dock state itself. The mining mod undocks a ship before it warps.
- `docks` is not cached and runs `FindAllOf` on every call. Call it only on a user action.
- `find_item` returns `nil` plus a reason for zero or several matches. Print the reason.
