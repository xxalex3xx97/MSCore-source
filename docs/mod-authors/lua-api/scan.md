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
| `f.max_age_ms` | number, default `5000` | The native cache of each kind is reread when it is older than this, capped at `5000` (a larger value is treated as `5000`). `0` rereads now. |
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

### Lua fallback

If a native scan walk faults, the native scan is off for the rest of the session (kill switch). `MSCore_scan_ok` returns `0` then. `MSCore.scan` does not return an empty list in that case: it runs a pure-Lua scan (same row fields, logged once as `scan: native scan off`). The native side reports `ok = false` for this, for an exception and for bad arguments, so the fallback runs for those too.

Fallback parity: the same fields, filters and units as the native rows (`type`, `oname` and ore names for the type filter; `ch`, `loc` raw, `world` in UE units; order fields read from the same properties). Ore lists are capped at 8 as natively.

Cache rules (native):

- A served row is checked before it is returned: its save object is alive with the same id; its actor is still the row's actor (a rock actor by `ObjectId`, a ship or station by its owner's current `Actor`); each ore object still has the id read at scan time. A check that fails drops the actor (`loaded` `0`, `world` `nil`) or the ore.
- Rows are not refreshed on serve. The age cap (5000 ms) is what bounds `loaded`, `world`, `docked`, `piloted`, `hull`, `order*` and new objects. Pass `max_age_ms = 0` for a fresh read.

Known limits (not fixed):

- Rows can be up to 5 s old (see cache rules). Nothing newer than the last walk is shown.
- An ore item with no save id (`ID` < 0) is checked by `-1` only. Ore items have an id in game (they are looked up by `ID` by the mining mod), so this is not expected.
- The Lua fallback's cache age is in whole seconds (`os.time`). Its rows are also read at most once per age window, so they can be up to about 5 s old.
- Fallback rows are not verified in game (same as native).

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

A chunk cube is 27 chunks: the centre chunk and its 26 neighbours. The native side sets level 1 on each chunk that has a Chunk object. A chunk in the player's own zone (the chunks within 2 of the player's chunk) is never set there: it is pending and is set by a later call once the player has left the zone.

| Function | Args | Returns | Notes |
|---|---|---|---|
| `MSCore.chunks.hold` | `owner, sys, ch` | `ok, err` | `owner` is an integer you choose. `ch` is `{X,Y,Z}` chunk integers. `ok` is `true` for a new set, for an unchanged cube, and for an unchanged cube that just added pending chunks. Calling it again with a new `ch` moves the cube: the new chunks are set first, then the old ones no other owner holds are cleared. |
| `MSCore.chunks.release` | `owner` | `ok, err` | Drops the owner's cube. `ok` is `true` also when the owner had none. |

Rules:

- Each owner holds one cube. Chunks held by several owners are cleared when the last one releases them.
- At most 150 distinct chunks are held at once. A request over the cap returns `false` with a `cap:` reason. The owner's existing cube is kept (the request is refused, nothing is moved).
- If the natives are not found, or a native call fails, chunk hold turns off for the rest of the session and every held chunk outside the player's zone is cleared. Chunks in the player's zone are not cleared at that point; they are cleared on `release_all` (mod unload) once the player has left the zone. `chunk_ready` / the `chunks` check reports why.
- Pending chunks (in the player's zone when the cube was set or moved): added on every later `hold` call, including an unchanged centre, and on `release` / `release_all` (the drain). A chunk stays pending while it is in the zone or while it has no Chunk object. The cap still applies to them.
- Known limits: MSCore has no timer, so a pending chunk is only added on a later `hold` or `release` call; a consumer that stops calling `hold` does not get it. A cube chunk with no Chunk object when the cube is set is not retried (only zone chunks are). A clear that falls in the player's zone is deferred the same way.

Check it with selfcheck. The `chunks` check only reads: it does not set anything.
