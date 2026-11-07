---
title: Quick reference
nav_order: 4
---

# Quick reference

The most used calls, grouped. Each name links to the full page in the [Lua API]({{ site.baseurl }}{% link mod-authors/lua-api.md %}) or the [Native C API]({{ site.baseurl }}{% link mod-authors/native-api.md %}).

## Start a mod

```lua
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")
package.path = ((SRC or ""):gsub("\\", "/")) .. "../../MSCore/Scripts/?.lua;" .. package.path
local MSCore = require("mscore")
MSCore.require(1)
```

| Call | What it does |
|---|---|
| `MSCore.require(n)` | Errors when the installed Lua API is older than `n`. |
| `MSCore.logger(tag)` | Returns `log, try`. `try(label, f, ...)` returns `ok, result`. |
| `MSCore.api`, `MSCore.version` | `3` and `"0.7.3"`. |
| `MSCore.native` | Native wrappers, or `nil` when the DLL is not loaded. |

## Game objects

| Call | What it does |
|---|---|
| `MSCore.safe.live(...)` | First valid object for the class names given. `nil` if none. |
| `MSCore.safe.valid(o)` | `true` only for a live object. Check before any method call. |
| `MSCore.safe.rd(f)` | `tostring(f())`, or `"?"` on error or `nil`. |
| `MSCore.safe.nameOf(x)` | Object name as a string. |
| `MSCore.resolve.player`, `.controller`, `.ship`, ... | Class and path names. Pass lists to `safe.live` with `table.unpack`. |

## Ticks and loader

| Call | What it does |
|---|---|
| `MSCore.events.on("tick", fn)` | Runs `fn()` every 250 ms on the game thread. Returns an id. |
| `MSCore.events.off(id)` | Removes the callback. |
| `MSCore.events.running()` | `true` once the shared loop has started. |
| `MSCore.loader.status()` | Rows `{ name, installed, remote, state }`. |
| `MSCore.loader.updates()` | Rows with `state == "update"`. |
| `MSCore.loader.on_update(fn)` | Calls `fn(row)` once per mod with an update. |

## UI

| Call | What it does |
|---|---|
| `MSCore.ui.mk(cls, outer)` | Creates `/Script/UMG.<cls>`. |
| `MSCore.ui.setText(tb, s)` | Sets a TextBlock's text. |
| `MSCore.ui.rgba(r, g, b, a)` | sRGB 0 to 255 in, `{ R, G, B, A }` out. |
| `MSCore.ui.onClick(btn, fn)` | Calls `fn()` on click. |

## Ships, commands and steering

| Call | What it does |
|---|---|
| `MSCore.ships.list()` | Every ship, `{ id, name, obj }`. Cached 5 s. |
| `MSCore.ships.mine()` | Ships with a mining bench or miner module block. |
| `MSCore.ships.get(id)` | One ship entry. |
| `MSCore.ships.hull(id)` | `{ current, max }`. |
| `MSCore.cmd.send(receiver, cmdObj)` | Sends a command. Needs the DLL. |
| `MSCore.steer.aim(id, yaw, pitch)` | View offset in degrees. Needs the DLL. |
| `MSCore.steer.stop(id)` | Same as `aim(id, 0, 0)`. |
| `MSCore.steer.throttle(id, delta)` | Throttle step. Needs the DLL. |

## Orders, filters and warp

| Call | What it does |
|---|---|
| `MSCore.orders.autopilot(sid, on)` | Autopilot on (`true`) or off (`false`). |
| `MSCore.orders.skip_to(sid, idx)` | Returns `ok, err, before, after`. |
| `MSCore.orders.new(receiver, cls)` | Creates an order. Does not return the id. |
| `MSCore.filters.send(sid, idx, item, amount, action, remove)` | `action`: `0` LIMIT, `1` BUFFER. |
| `MSCore.warp.send(sid, sys, ch, arr, wrap, set_target)` | Teleport warp. Undock first. |
| `MSCore.warp.sectors()` | Every sector, cached 5 s. |
| `MSCore.docks()` | Every dock. Not cached. Call on user action only. |
| `MSCore.find_item(res)` | Item object, or `nil, why`. |

## Native

| Call | What it does |
|---|---|
| `MSCore.native.version()` | Native version string. |
| `MSCore.native.check(name)` | Read-only world check. Returns `ok, detail`. |
| `MSCore.native.pin_ensure()` | Call every tick until it returns `true`. |
| `MSCore_api()` (C) | Returns `4`. Check before other exports. |
| `GetProcAddress(h, "MSCore_add_input")` (C) | Resolve an export from `main.dll`. |

## Manager buttons

| Button | Use it to |
|---|---|
| **Install/Repair MSCore** | Install or repair MSCore and UE4SS. |
| **Add mod...** | Add `owner/repo` or a GitHub link. |
| **Check** | Look for updates. Downloads nothing. |
| **Update selected** / **Update all** | Stage an update. Applied at the next game start. |
| **Rollback** | Restore the last backup. |
| **Remove mod** | Remove a mod's files. |

{: .note }
Close the game before install, add, remove or rollback. See [Getting started]({{ site.baseurl }}{% link players/getting-started.md %}).
