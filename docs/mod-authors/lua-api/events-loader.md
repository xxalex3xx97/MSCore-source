---
title: Events and loader
parent: Lua API
grand_parent: Mod authors
nav_order: 4
---

# Events and loader

`events` runs callbacks on a shared 250 ms tick. `loader` reads the update status of each mod.

## events

| Function | Args | Returns | Notes |
|---|---|---|---|
| `events.on` | `"tick", fn` | id | Runs `fn()` every 250 ms on the game thread. Only `"tick"` is accepted in API 1. Any other name raises an error. |
| `events.off` | `id` | - | Removes the callback. |
| `events.running` | - | boolean | `true` once the shared loop has started. |

All `events.on` callbacks in one Lua state share one loop. Each callback runs under `pcall`, so an error in one does not stop the others.

### Example

```lua
local MSCore = require("mscore")

local id, n = nil, 0
id = MSCore.events.on("tick", function()
    n = n + 1
    if n == 40 then  -- 40 ticks x 250 ms = about 10 s
        print("10 s passed\n")
        MSCore.events.off(id)
    end
end)
```

## loader

{: .warning }
**Not verified in game:** the in-game notice path. The Lua functions read rows from the native part, and the update check runs in a background thread of the DLL. The manager update flow was tested.

| Function | Args | Returns | Notes |
|---|---|---|---|
| `loader.status` | - | list of rows | Row = `{ name, installed, remote, state }`. Empty list when the scan has not run or the DLL is missing. |
| `loader.updates` | - | list of rows | Only rows where `state == "update"`. |
| `loader.on_update` | `fn` | - | Calls `fn(row)` once per mod with an update. Fires on the first tick after every mod has been checked. |

`state` is one of:

| State | Meaning |
|---|---|
| `pending` | The check has not finished yet. |
| `uptodate` | Installed version matches the remote version. |
| `update` | The remote version is newer. |
| `skipped:min_native` | The installed MSCore native version is older than the mod's `min_mscore_native`. |
| `error:<msg>` | The check failed. `<msg>` gives the reason. |

The DLL only checks. It never downloads.

### Example

```lua
local MSCore = require("mscore")

MSCore.loader.on_update(function(row)
    print("update for " .. row.name .. ": " .. row.installed .. " -> " .. row.remote .. "\n")
end)
```

## Gotchas

- `events.on` only takes `"tick"` in API 1. Other names raise an error.
- `loader` never downloads. Use the manager to install an update.
