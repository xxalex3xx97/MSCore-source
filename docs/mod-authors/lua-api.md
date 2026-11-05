---
title: Lua API
parent: Mod authors
nav_order: 2
has_children: true
---

# Lua API

Lua API version: **1**. Module version: **0.6.0**. Load it with `require("mscore")` after the path header in the [Quickstart]({{ site.baseurl }}{% link mod-authors/quickstart.md %}).

## Modules at a glance

| Module | Purpose | Page |
|---|---|---|
| `MSCore` (core) | API version, `require`, `logger` | [Core]({{ site.baseurl }}{% link mod-authors/lua-api/core.md %}) |
| `safe`, `resolve` | Check and read game objects; class and path names | [Safe and objects]({{ site.baseurl }}{% link mod-authors/lua-api/safe-objects.md %}) |
| `ui` | Create and style simple UMG widgets | [UI]({{ site.baseurl }}{% link mod-authors/lua-api/ui.md %}) |
| `events`, `loader` | Shared tick loop; mod update status | [Events and loader]({{ site.baseurl }}{% link mod-authors/lua-api/events-loader.md %}) |
| `ships`, `cmd`, `steer` | List ships, send commands, aim and throttle | [Ships and commands]({{ site.baseurl }}{% link mod-authors/lua-api/ships-commands.md %}) |
| `orders`, `filters` | Orders and logistic filters (native) | [Orders and filters]({{ site.baseurl }}{% link mod-authors/lua-api/orders-filters.md %}) |
| `warp`, `docks`, `find_item` | Warp, sectors, docks and item lookup | [Warp and docks]({{ site.baseurl }}{% link mod-authors/lua-api/warp-docks.md %}) |
| `scan`, `chunks` | World scan of ships, asteroids and stations; keep a chunk cube loaded (native) | [Scan and chunks]({{ site.baseurl }}{% link mod-authors/lua-api/scan.md %}) |
| `native` | Raw wrappers for the native DLL | [Native bridge]({{ site.baseurl }}{% link mod-authors/lua-api/native-bridge.md %}) |
| `units`, `hacks` | Coordinate helpers; registered workarounds | [Units and hacks]({{ site.baseurl }}{% link mod-authors/lua-api/units-hacks.md %}) |

## Which module do I need?

- Check that a game object is still alive before I call a method on it → `safe` ([Safe and objects]({{ site.baseurl }}{% link mod-authors/lua-api/safe-objects.md %})).
- Get a game class or path name without typing it myself → `resolve` ([Safe and objects]({{ site.baseurl }}{% link mod-authors/lua-api/safe-objects.md %})).
- Run code every 250 ms on the game thread → `events` ([Events and loader]({{ site.baseurl }}{% link mod-authors/lua-api/events-loader.md %})).
- Know which mods have an update → `loader` ([Events and loader]({{ site.baseurl }}{% link mod-authors/lua-api/events-loader.md %})).
- Build a text block, colour or button → `ui` ([UI]({{ site.baseurl }}{% link mod-authors/lua-api/ui.md %})).
- List my ships or send them a command → `ships`, `cmd`, `steer` ([Ships and commands]({{ site.baseurl }}{% link mod-authors/lua-api/ships-commands.md %})).
- Set orders, autopilot or logistic filters → `orders`, `filters` ([Orders and filters]({{ site.baseurl }}{% link mod-authors/lua-api/orders-filters.md %})).
- Warp a ship, list sectors, find a dock or an item → `warp`, `docks`, `find_item` ([Warp and docks]({{ site.baseurl }}{% link mod-authors/lua-api/warp-docks.md %})).
- Find ships, asteroids or stations and their state, or keep a far chunk loaded → `scan`, `chunks` ([Scan and chunks]({{ site.baseurl }}{% link mod-authors/lua-api/scan.md %})).
- Call a native export directly from Lua → `native` ([Native bridge]({{ site.baseurl }}{% link mod-authors/lua-api/native-bridge.md %})).
- Convert raw coordinates to metres, or list workarounds → `units`, `hacks` ([Units and hacks]({{ site.baseurl }}{% link mod-authors/lua-api/units-hacks.md %})).

## Conventions

- **`ok, err` pairs:** `ok` is `true` or `false`, `err` is a string. When there is no native DLL, every native-backed function returns `false, "native DLL not loaded"`.
- **Coordinates:** chunk (`ch`) values are integers. Locations (`loc`, `arr`) are raw fixed-point numbers. Divide by `units.RAW_PER_M` to get metres.
- **Caching:** read-only lookups use a 5 second cache unless the page says otherwise.
- **Source:** all names are in `mods/MSCore/Scripts/mscore.lua` unless the page says otherwise.

## Table layout

Function tables use four columns:

| Column | Meaning |
|---|---|
| Function | The name to call. |
| Args | The arguments, in order. `-` means none. |
| Returns | The values returned. `-` means no return value is documented. |
| Notes | Behaviour, limits and status. |
