---
title: Units and hacks
parent: Lua API
grand_parent: Mod authors
nav_order: 9
---

# Units and hacks

`units` converts MSCore's raw coordinates to metres. `hacks` lists the workarounds MSCore registers.

## units

{: .warning }
**Not verified in game:** no consumer yet.

| Field or function | Args | Returns | Notes |
|---|---|---|---|
| `units.RAW_PER_M` | - | `65536` | Raw units per metre. |
| `units.CHUNK_M` | - | `2048` | Metres per chunk. |
| `units.abs_pos` | `chunk, loc` | `{X,Y,Z}` in metres | `chunk` = `{X,Y,Z}` integers, `loc` = `{X,Y,Z}` raw numbers. |
| `units.dist_m` | `a, b` | number (metres) | Straight-line distance between two `{X,Y,Z}` points. |

## hacks

| Function | Args | Returns | Notes |
|---|---|---|---|
| `hacks.add` | `{name, what, why, verified_build}` | the table | Registers a workaround in this Lua state. |
| `hacks.list` | - | list | All registered workarounds. |

MSCore registers six entries:

- `click_hook`
- `valid_before_call`
- `linear_colors`
- `shipmove_typeid`
- `map_pin_hook`
- `teleport_warp`

## Example

```lua
local MSCore = require("mscore")

local p = MSCore.units.abs_pos({ X = 1, Y = 0, Z = 0 }, { X = 65536, Y = 0, Z = 0 })  -- X = 2049

for _, h in ipairs(MSCore.hacks.list()) do
    print(h.name .. "\n")
end
```

## Gotchas

- `loc` and `arr` values are raw. Divide by `units.RAW_PER_M` to get metres.
- `hacks.add` registers only in your own Lua state.
