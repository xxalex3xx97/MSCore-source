---
title: Scan and chunks
parent: Lua API
grand_parent: Mod authors
nav_order: 10
---

# Scan and chunks

`MSCore.scan` lists the ships, resource asteroids or stations in the world. `MSCore.chunks` keeps the chunks around a point loaded, so far-away objects stay in the world.

{: .warning }
**Not verified in game.** Both need the native DLL (native API 3).

## scan

`MSCore.scan(kind, f)` returns a list of rows, or `nil, reason`.

| Argument | Values | Notes |
|---|---|---|
| `kind` | `"ship"`, `"asteroid"`, `"station"` | Any other value returns `nil, "unknown kind"`. |
| `f.max_age_ms` | number, default `60000` | The native cache of each kind is reread when it is older than this. `0` rereads now. |
| `f.type` | string, default `""` | Case-insensitive match on the row `type`, `oname` or any ore name. Empty means no filter. |
| `f.sys` | number, default `0` | Solar system id. `0` means all systems. |
| `f.loaded_only` | boolean | Asteroids only: keep rock actors that are in the world. |

### Kinds

- `ship`: every live ship.
- `asteroid`: every live resource asteroid save object. `loaded` says whether its rock actor is in the world.
- `asteroid` with `loaded_only`: only the rock actors in the world. `loaded` is `true` on every row.
- `station`: every dock object.

### Row fields

| Field | Type | Who has it | Notes |
|---|---|---|---|
| `id` | number | all | Save object id. |
| `kind` | number | all | `0` ship, `1` asteroid, `2` station. |
| `sys` | number | all | Solar system id. |
| `ch` | `{X,Y,Z}` | all | Chunk. Integers. |
| `loc` | `{X,Y,Z}` | all | Chunk-relative location, raw. |
| `world` | `{X,Y,Z}` or `nil` | all | Actor location in UE units. `nil` when not readable. |
| `name` | string | all | Display name. Ships and stations. |
| `type` | string | all | Ship and station: class name. Asteroid: display name of the first ore. |
| `loaded` | boolean | asteroid | `true` when the rock actor is in the world. |
| `docked` | boolean | ship | `DockID` is not 0. |
| `piloted` | boolean | ship | The player pilots this ship. |
| `dock_id` | number | ship | `DockID`. |
| `hull` | number or `nil` | ship | Current hull. `nil` when unknown. |
| `max_hull` | number or `nil` | ship | `MaxHealth`. `nil` when unknown. |
| `order_idx` | number or `nil` | ship | `CurrentOrderIndex`. `nil` when none. |
| `order_dock` | number | ship | `DockID` of the current order. |
| `order` | string or `nil` | ship | Class name of the current order. `nil` when none. |

`'|'` and newlines in names are replaced with spaces by the native side, so the fields stay aligned.

### Example

```lua
local MSCore = require("mscore")

local rows, why = MSCore.scan("asteroid", { type = "Iron", sys = 6, loaded_only = true })
if not rows then
    print("scan failed: " .. tostring(why) .. "\n")
else
    for _, r in ipairs(rows) do
        print(r.id .. " " .. r.type .. "\n")
    end
end
```

## chunks

A chunk cube is 27 chunks: the centre chunk and its 26 neighbours. The native side sets level 1 on each chunk that has a Chunk object, and skips the player's own zone (the chunks within 2 of the player's chunk).

| Function | Args | Returns | Notes |
|---|---|---|---|
| `MSCore.chunks.hold` | `owner, sys, ch` | `ok, err` | `owner` is an integer you choose. `ch` is `{X,Y,Z}` chunk integers. `ok` is `true` for a new set and for an unchanged cube. Calling it again with a new `ch` moves the cube: the new chunks are set first, then the old ones no other owner holds are cleared. |
| `MSCore.chunks.release` | `owner` | `ok, err` | Drops the owner's cube. `ok` is `true` also when the owner had none. |

Rules:

- Each owner holds one cube. Chunks held by several owners are cleared when the last one releases them.
- At most 150 distinct chunks are held at once. A request over the cap returns `false` with a `cap:` reason and drops that owner's cube.
- If the natives are not found, or a native call fails, chunk hold turns off for the rest of the session and every held chunk outside the player's zone is cleared. `chunk_ready` / the `chunks` check reports why.

Check it with selfcheck. The `chunks` check only reads: it does not set anything.
