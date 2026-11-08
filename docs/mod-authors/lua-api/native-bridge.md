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
| `native.version` | `()` | string | The native version (`"0.8.0"`). |
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
| `native.check` | `(name)` | `ok, detail` | Read-only world checks. Names: `classes`, `controller`, `sectors`, `arrival`, `jump_cost`, `pin_hook`, `filters_read`, `find_item`, `find_dock`, `scan`, `chunks`. |
| `native.loader_status` | `()` | string | One line per mod: `name\|installed\|remote\|state`. |
| `native.scan` | `(kind, max_age_ms, type, sys, loaded_only)` | string | One line per row, `\n` ended. Numbers as in `MSCore_scan`. Use `MSCore.scan` instead; it parses the lines. |
| `native.chunk_hold` | `(owner, sys, X, Y, Z)` | `ok, err` | Holds the chunk cube for `owner`. `ok` is true for a set and for an unchanged cube. |
| `native.chunk_release` | `(owner)` | boolean | Drops the owner's cube. |
| `native.read_station` | `(stationId)` | `ok, text` | Raw station text (one `B` or `M` line per block or machine, tab separated). 0.8.0: `B` lines have a 7th column, the full class path. Prefer `factory_native.read_station`. |
| `native.read_inventory` | `(invId)` | `ok, text` | Raw inventory text (one `S` line per slot: `S item amount max`, `item` empty for an empty slot). Prefer `factory_native.read_inventory`. |
| `native.inv_split` | `(invId, src, amount, dst)` | `true` or `false, err` | 0.7.6: move `amount` items from slot `src` to empty slot `dst` of the same Inventory. Slots are 0-based. Local edit, not a game command (co-op desync, single-player only). Prefer `factory_native.inv_split`, which returns `true` or `nil, err`. |
| `native.add_block` | `(station, classPath, gx, gy, h, rot)` | `ok, err` | 0.8.0: AddBlockInput (type id `30`) to the station. `classPath` is the full class path from a `B` line. Prefer `factory_native.add_block`. |
| `native.remove_block` | `(station, classPath, gx, gy, h, rot)` | `ok, err` | 0.8.0: RemoveBlockInput (type id `31`), same args as `add_block`. Removes the first pending entry matching class, X, Y, Height and Rotation. Prefer `factory_native.remove_block`. |
| `native.pending` | `(station, apply, reset, primary, player, editing)` | `ok, err` | 0.8.0: PendingBlueprintInput (type id `33`). `apply` and `reset` are `0` or `1`, exactly one set. `apply` needs `primary` and `player` > 0. `editing` is a block id or `0`. Prefer `factory_native.pending`. |
| `native.read_block_class` | `(classPath)` | `ok, text` | 0.8.0: class defaults of one BuildingBlockObject class (`K` line, then one `I` line per cost item). Prefer `factory_native.read_block_class`. |
| `native.refund_enable` | `(bool)` | string | Probe log switch: `true` validates, installs the hooks and turns the `PROBE ...` log lines on; `false` turns them off. Default off (also on when `Mods/MSCore/refund_probe.txt` exists). Returns the status. |
| `native.redirect_enable` | `(bool)` | string | 0.7.5: Blueprint Unit refund / build-charge redirect. **On by default** at MSCore start when the load validation passes; `false` (or the file `Mods/MSCore/refund_redirect_off.txt`) turns it off. Returns the status. |
| `native.redirect_status` / `native.refund_status` | `()` | string | `off`, `on: redirect=on probe_log=off groups=yes` or `COMPAT refund failed: <why>`. |
| `native.redirect_stats` | `()` | string | `refund=<n> refund_fallback=<n> charge=<n> skipped=<n> group_made=<n> group_fail=<n> errors=<n>`. |
| `native.refund_log` | `()` | string | Last 200 log lines (`REDIRECT refund ...`, `REDIRECT charge ...`, `COMPAT ...`, and `PROBE ...` when the probe is on). |

**Known limitation: co-op.** The redirect decides from sim state only, but the game is lockstep: a client without the hook would put refund/charge in different inventories and desync. Every player needs the same MSCore version with the redirect on, and the pak with `BP_BPUnitStoreObj_01_C`. Designer and teardown paths are untested.

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
