---
title: Quickstart
parent: Mod authors
nav_order: 1
---

# Quickstart

## 1. Install MSCore for testing

Follow [Getting started]({{ site.baseurl }}{% link players/getting-started.md %}) for the manager, or copy the `Mods\MSCore` folder into `ue4ss\Mods\` by hand.

## 2. Order in mods.txt

`ue4ss\Mods\mods.txt` lists the mods in load order. MSCore must load before any mod that uses it:

```text
MSCore : 1
MyMod : 1
```

The manager writes `MSCore : 1` as the first entry. If you edit the file by hand, keep MSCore above your mod.

## 3. Header for your main.lua

Put this at the top of your mod's `Scripts/main.lua`. It finds MSCore next to your mod folder:

```lua
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")  -- this mod's Scripts/ dir
package.path = ((SRC or ""):gsub("\\", "/")) .. "../../MSCore/Scripts/?.lua;" .. package.path
local MSCore = require("mscore")
MSCore.require(1)  -- errors if the installed Lua API is older than 1
```

`MSCore.require(n)` raises an error when the installed API is older than `n`. Pass the API number your mod needs.

## 4. Use a helper

```lua
local id = MSCore.events.on("tick", function()  -- every 250 ms, game thread
    local p = MSCore.safe.live(table.unpack(MSCore.resolve.player))
    if p then MSCore.events.off(id) end
end)
```

`events.on` only accepts `"tick"` in API 1. `events.off(id)` removes the callback. See [Events and loader]({{ site.baseurl }}{% link mod-authors/lua-api/events-loader.md %}).

## 5. Check it runs

Start the game, load a world, wait about 8 seconds, then read `Mods\MSCore\selfcheck.log`. Every line should be `OK` or `SKIP`. See [Troubleshooting]({{ site.baseurl }}{% link players/troubleshooting.md %}) for `BROKEN` and `CHANGED`.

## Rules to follow

- Call `MSCore.safe.valid(o)` before any method on a game object. A call on a dead object can crash the game, and `pcall` cannot catch that.
- Use `MSCore.safe.live(...)` to find an object. It returns the first valid match, or `nil`.
- Read the game's class names from `MSCore.resolve`. Do not hard-code them in your mod.
- Command functions send game input. Only call them on purpose.

Full list of functions: [Lua API]({{ site.baseurl }}{% link mod-authors/lua-api.md %}). Short list: [Quick reference]({{ site.baseurl }}{% link quick-reference.md %}).
