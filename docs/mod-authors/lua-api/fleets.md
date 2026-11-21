---
title: Fleets
parent: Lua API
grand_parent: Mod authors
---

# Fleets: MSCore.fleets

Source: `mods\MSCore\Scripts\mscore_fleets.lua`. Also loadable alone: `require("mscore_fleets")`. Reads need the native DLL with API 13. Reads use a 1 s Lua cache (`TTL`). A refresh asks the native side for a snapshot up to 3000 ms old; `fleets.list(true)` passes `max_age_ms = 0` (rescan now, no cache). At 0 the native side has no cache. After any write (create, rename, delete, assign, unassign) the next read is forced (`max_age_ms = 0`), then the 1 s TTL applies again.

| Function | Params | Returns |
|---|---|---|
| `fleets.list` | - | `{ {id, name, parent, ships={ship ids}}, ... }` sorted by name, or `nil, "off:<why>"` |
| `fleets.of_ship` | `ship_id` | `{fleet ids}` or `nil, "off:<why>"` |
| `fleets.caps` | - | `"ok"`, `"off:<why>"` or `"off:native too old"` |
| `fleets.write_caps` | - | `"ok"` only when native caps say `fleets_write=ok`, else `"off:unverified"` |
| `fleets.create` | `name` | `true` or `false, why` |
| `fleets.rename` | `id, name` | `true` or `false, why` |
| `fleets.delete` | `id` | `true` or `false, why` |
| `fleets.assign` / `fleets.unassign` | `fleet_id, ship_id` | `true` or `false, why` |
| `fleets.invalidate` | - | drops the Lua cache |
| `fleets.disable` | `why` | turns the module off (kill switch `module_disable("fleets")`) |
| `fleets.probe` / `fleets.capture` | - / `on` | dev tools; capture only logs `FleetTagInput` commands |

Write functions return `false, "off:..."` until caps `fleets_write=ok`.
