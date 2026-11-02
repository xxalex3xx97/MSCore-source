---
title: Native bridge
parent: Lua API
grand_parent: Mod authors
nav_order: 8
---

# Native bridge

`MSCore.native` is `nil` when the DLL is not loaded. Otherwise it has the fields below. Each one is the raw `MSCoreNative_*` global. For the C side of the same exports, see [Native C API]({{ site.baseurl }}{% link mod-authors/native-api.md %}).

## Functions

| Function | Args | Returns | Notes |
|---|---|---|---|
| `native.version` | `()` | string | The native version (`"0.5.1"`). |
| `native.alive` | `(obj)` | boolean | `false` for `nil`, numbers and dead objects. |
| `native.send_cmd` | `(receiver, cmd)` | `ok, err` | Sends a command. |
| `native.view_send` | `(cid, yawDeg, pitchDeg)` | `ok, err` | Sets the view offset. |
| `native.throttle` | `(sid, delta)` | `ok, err` | Sends a throttle step. |
| `native.offsets` | `()` | string | `typeid_off=0x28 throttle_id=0x3a view_raw_per_deg=65536` |
| `native.check_offsets` | `()` | `ok, detail` | Checks the offsets above. |
| `native.new_order` | `(receiver, cls)` | `ok, err` | Creates an order. |
| `native.update_order` | `(oid, target, item, cmp, amount)` | `ok, err` | Updates an order. |
| `native.delete_order` | `(sid, oid)` | `ok, err` | Deletes an order. |
| `native.autopilot` | `(sid, on)` where `on` is `0` or `1` | `ok, err` | Takes the number form. |
| `native.skip_to` | `(sid, idx)` | `ok, err, before, after` | Skips to an order index. |
| `native.filter_send` | `(sid, idx, item, amount, action, remove)` | `ok, err` | Sets a filter row. |
| `native.warp_send` | `(sid, sys, cx, cy, cz, ax, ay, az, wrap, set_target)` | `ok, err` | Teleport warp. `cx, cy, cz` = chunk, `ax, ay, az` = raw arrival. |
| `native.pinned` | `()` | number | Sector id, `0` = none. |
| `native.pin_ensure` | `()` | boolean | `true` once the map pin hook is armed. Call every tick until it returns `true`. |
| `native.check` | `(name)` | `ok, detail` | Read-only world checks. Names: `classes`, `controller`, `sectors`, `arrival`, `jump_cost`, `pin_hook`, `filters_read`, `find_item`, `find_dock`. |
| `native.loader_status` | `()` | string | One line per mod: `name\|installed\|remote\|state`. |

## Example

```lua
local MSCore = require("mscore")

if MSCore.native then
    print("native " .. MSCore.native.version() .. "\n")
    local ok, detail = MSCore.native.check("controller")
    print(tostring(ok) .. " " .. tostring(detail) .. "\n")
else
    print("native DLL not loaded\n")
end
```

## Gotchas

- `autopilot` takes `0` or `1`. The Lua `orders.autopilot` converts a boolean for you.
- Call `pin_ensure` every tick until it returns `true`. The map controller may not exist when the mod loads.
- `check` only reads. It does not change the game.
