# MSCore isolation

How MSCore keeps game-fragile code contained. Source of truth for the rules: the isolation architecture note (vault MiningBenchMod/tasks).

## Layers

- **L0 game binding**: the only code that knows game names, offsets, patterns and /Game paths (msc_pattern, msc_natives, msc_scan, msc_ue, msc_cmd, msc_factory, msc_rename, msc_refund, msc_station, msc_chunks, msc_pin, msc_warp, msc_orders; Lua mscore_wbp, mscore_blocks, mscore_factory, mscore_contract (the only Lua file with widget names)), msc_contract.hpp (class names, command ids).
- **L1 module registry**: msc_caps.hpp. One cap row per module: `name`, ok or off with `why`.
- **L2 stable API**: C exports (`MSCore_api`) and Lua API (`mscore`). Logical names and caps, no game names.
- **L3 mods** (MiningBenchMod, MiningUnitUI, BPU, MSDevMenu, BlockPak): no game coupling. They call L2 only.

Shared infrastructure, not modules: msc_pattern, msc_ue, msc_caps, msc_loader, msc_api.

## Modules

| module | caps |
|---|---|
| chunkhold | chunkhold (natives + chunks) |
| refund | refund, refund_groups, refund_lookup, refund_offsets |
| split | split |
| widgets | widgets (Lua, mscore_wbp + mscore_contract; resolve by contract path, then by name; module_disable("widgets")) |
| blocks | blocks (Lua, mscore_blocks + contract; module_disable("blocks")) |
| rocks | rocks (native scan of asteroids, class names from the contract; module_disable("scan")) |
| ships | ships (native ship shields, msc_ships.hpp; module_disable("ships")) |
| flight | flight (native ship flight state, actor frame, throttle, power, zero, console; msc_flight.hpp; module_disable("flight")) |
| misc | exports without a module of their own |

## Module contract

1. **deps**: `caps_dep(name, dep)`. dep off makes name off with why `dep:<dep>`.
2. **resolve()**: once at startup on the game thread. Patterns, learned vtables, by-name props. Hardcoded values only when in the contract table and verified.
3. **selftest()**: read-only.
4. **caps**: `caps_add(name, ok, why)`. Fail closed. Startup line `COMPAT CAPS name=ok|off:why`.
5. **kill switch**: `module_disable(name, why)`. Cap off, one log line `COMPAT CAPS name=off:why (runtime)` per name. Exports of the module return their failure value.
6. **disabled hook**: a hook of a disabled module is a plain call-through.

`module_ok(name)` reads the cap (missing row = false).

## Crash rules

- **R1**: every raw read/write goes through one SEH helper (`ms_rd` / `ms_wr` in msc_ue.hpp). No bare `*(T*)(p+off)`.
- **R2**: every `MSCORE_API` body has try/catch(...). A failure calls `module_disable` for that module only.
- **R3**: writes only after selftest passed. Reads fail soft.
- **R4**: a cmd id is written only after the layout is verified.
- **R5**: Lua calls are pcall-wrapped by mscore.lua. An error disables that module only.
- **R6**: old mod / new MSCore and new mod / old MSCore both degrade: check api_level and caps. A nil export means the feature is off. New MSCore only adds exports.

## SEH and C++ EH

- `__try` lives in small functions with POD arguments only. MSVC rejects `__try` in a function that has C++ objects with destructors (C2712).
- `try/catch(...)` in an export does not catch access violations. SEH helpers do that.

## Adding an export

Wrap the body in try/catch(...) and pass the module to `module_disable` in the catch. `export_call` already does this with the module as its last argument (default `misc`).
