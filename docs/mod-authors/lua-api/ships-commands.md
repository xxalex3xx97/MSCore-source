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
| `cmd.send_typed` | `receiver, cmdObj, typeId` | `ok, err` | Typed send (native API 4). Writes `typeId` at `cmdObj+0x28` when that int is `0`, then `AddInputCommand(receiver, cmdObj)`. Needs the native DLL with `MSCore_api() >= 4`. |

`cmdObj` is a UObject made with `NewObject` or a `CreateInput*` factory. The mining mod uses this path in game.

Type ids for `send_typed`: BuildInput `0x2e`, DisassembleInput `0x2f`, InventoryInput `12`, SetRecipeInput `20`, MachinePortInput `17`, SetConfigInput `21`. Receiver `0` for BuildInput, DisassembleInput and InventoryInput (the world). Machine id for SetRecipeInput, MachinePortInput and SetConfigInput.

## factory_native (API 4)

Thin wrappers over the factory natives. The glue table is `MSCore.factory_native` (not `MSCore.factory`). Needs the native DLL with API 4.

| Function | Args | Returns | Notes |
|---|---|---|---|
| `factory_native.find_buildable` | `datName` | Buildable object, or `nil` | Buildable data asset by name, e.g. `"DAT_Refinery"`. |
| `factory_native.place` | `station, datName, x, y, h, rot, cfgBytes` | `ok, err` | BuildInput at the world (receiver 0). `cfgBytes` = raw bytes as a Lua string, `""` or `nil` for none. |
| `factory_native.disassemble` | `id, mass` | `ok, err` | DisassembleInput for target `id`. `mass` `0` or `1`. |
| `factory_native.set_recipe` | `machine, recipeObj, amount, cfgBytes` | `ok, err` | SetRecipeInput on machine `machine`. `cfgBytes` is `""` or 4 bytes (int32 `Config`). |
| `factory_native.port` | `machine, port` | `ok, err` | MachinePortInput: toggles port `port` of `machine`. |
| `factory_native.inv_move` | `prim, sec, slot, action, receiver` | `ok, err` | InventoryInput sent to `receiver` (the player object id). Without `receiver` it is 0, which the game silently drops. `action` `2` = quick move. |
| `factory_native.read_station` | `stationId` | `blocks, machines` or `nil, err` | Live station Blueprint (native read, parsed). `blocks` = `{class,gx,gy,h,rot}`; `machines` = `{id,cls,bcls,x,y,h,rot,cfg}`, `cfg` `nil` when unreadable. |
| `factory_native.read_inventory` | `invId` | `slots` or `nil, err` | Live Inventory `ItemSlots` (native read). `slots` = `{item,amount,max}` per slot, `item` `""` = empty slot, `max` = `Item.MaxStackSize`. |
| `factory_native.inv_split` | `invId, src, amount, dst` | `true` or `nil, err` | 0.7.6: local move of `amount` items from slot `src` to the EMPTY slot `dst` of one Inventory (0-based slots). Same inventory only, `amount` <= the stack cap. Not a game command: co-op clients do not see it (desync accepted). Needs MSCore 0.7.6 DLL. |

{: .warning }
Receiver `0` is silently dropped by the game; pass the player object's id as receiver (fixed in MSCore 0.7.5). See [Inventories, refunds and builds]({{ site.baseurl }}{% link mod-authors/game-internals/inventories-refunds-builds.md %}).

{: .warning }
**Not verified in game:** the factory natives and `send_typed`.

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
