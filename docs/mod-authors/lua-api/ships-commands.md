---
title: Ships and commands
parent: Lua API
grand_parent: Mod authors
nav_order: 5
---

# Ships and commands

`ships` lists ships (read-only). `cmd` sends a command object to a ship. `steer` aims the view and throttles a ship.

## ships

Read-only. The ship list is cached for 5 seconds. Every entry is checked with `safe.valid` before use.

{: .warning }
**Not verified in game:** this module.

| Function | Args | Returns | Notes |
|---|---|---|---|
| `ships.list` | - | list of `{ id, name, obj }` | Every ship. `name` is the display name, or `"?"`. |
| `ships.mine` | - | list of `{ id, name, obj }` | Ships with a `BPO_MiningBenchBlock_C` or `BPO_MinerModuleBlock_C` block. |
| `ships.get` | `id` | entry or `nil` | Entry for one ship id. |
| `ships.is_docked` | `id` | boolean, or `nil, reason` | `true` when the ship has a dock and a non-zero `DockID`. |
| `ships.hull` | `id` | `{ current, max }`, or `nil, reason` | From `GetCurrentHull` and `MaxHealth`. A field is `nil` when unreadable. |
| `ships.pilot_console` | `id` | object or `nil` | First `PilotConsoleObject` with `ShipID == id`, otherwise `ParentID == id`. |
| `ships.warp_fuel` | `id` | number, or `nil, reason` | `GetAvailableJumpFuel`. Counts warp fuel items only (1 item = 50 km). Hydrogen and deuterium are not wrapped. |

## cmd

| Function | Args | Returns | Notes |
|---|---|---|---|
| `cmd.send` | `receiver, cmdObj` | `ok, err` | Sends `cmdObj` to `receiver` (a ship id) with `AddInputCommand`. Applies the type id fix (`shipmove_typeid`). Needs the native DLL. |

`cmdObj` is a UObject made with `NewObject` or a `CreateInput*` factory. The mining mod uses this path in game.

## steer

Needs the native DLL.

| Function | Args | Returns | Notes |
|---|---|---|---|
| `steer.aim` | `id, yaw, pitch` | `ok, err` | Sets the view offset in degrees through the ship's pilot console. The raw value is degrees times 65536. |
| `steer.stop` | `id` | `ok, err` | Same as `aim(id, 0, 0)`. The throttle is not changed. |
| `steer.throttle` | `id, delta` | `ok, err` | Sends a throttle step of `delta` (integer) to ship `id`. |

{: .warning }
**Not verified in game:** `aim` and `throttle` through MSCore. The Ctrl+F9 test key in `main.lua` was not confirmed.

## Example

```lua
local MSCore = require("mscore")

for _, s in ipairs(MSCore.ships.mine()) do
    print(s.id .. " " .. s.name .. "\n")
end

local s = MSCore.ships.mine()[1]
if s then
    local ok, err = MSCore.steer.aim(s.id, 10, 0)  -- yaw 10 degrees, pitch 0
    if not ok then print(err .. "\n") end
end
```

## Gotchas

- `cmd` and `steer` need the native DLL. Without it they return `false, "native DLL not loaded"`.
- The ship list from `ships` is cached for 5 seconds.
- `steer.stop` does not change the throttle.
