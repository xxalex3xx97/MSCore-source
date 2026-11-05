# MSCore

Shared core for Main Sequence Demo UE4SS mods. It has three parts:

- **Native DLL** (`cpp\MSCore`, UE4SS C++ mod): sends ship commands through the game's own `AddInputCommand` route, reads sectors and jump data, arms the map-pin hook, and runs the mod-update checks.
- **Lua library** (`mods\MSCore\Scripts\mscore.lua`): safe UObject access, UMG helpers, a 250 ms tick loop, and read/command wrappers for ships, orders, filters, warp, docks and items.
- **Mod manager** (`cpp\manager`, `MSCoreManager.exe`): installs, updates, rolls back and removes mods from GitHub releases. Every download is checked against SHA-256 and size.

Used by MiningBenchMod and MSDevMenu. Lua API 1, native API 3, version 0.6.0.

Status: the selfcheck log is the check. Run the game with MSCore installed and read `Mods\MSCore\selfcheck.log`. There is no automated game test suite.

## Install

1. Close the game.
2. Copy `mods\MSCore` to `<game>\Windows\MainSequence\Binaries\Win64\ue4ss\Mods\MSCore` (or run the manager).
3. In `...\ue4ss\Mods\mods.txt`, add `MSCore : 1` above your mods.
4. Start the game, load a world, wait about 8 s, then read `Mods\MSCore\selfcheck.log`.

Each UE4SS mod has its own Lua state, so each mod requires its own copy of `mscore.lua`. Add this to the top of your mod's `Scripts\main.lua`:

```lua
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")  -- this mod's Scripts/ dir
package.path = ((SRC or ""):gsub("\\", "/")) .. "../../MSCore/Scripts/?.lua;" .. package.path
local MSCore = require("mscore")
MSCore.require(1)  -- errors if the installed API is older than 1
```

See `docs\` for the mod author guide (Lua API and native API reference).

## API summary

- `safe`: `valid(o)` (call before any UObject method), `live(...)`, `objName`, `rd(f)`, `try`, `val`, `nameOf`.
- `ui`: UMG helpers (`mk`, `setText`, `onClick`, `rgba`, `smallFont`, `hadd`). `events.on("tick", fn)` / `events.off(id)`: one 250 ms loop.
- `resolve`: game class and path names, in one table.
- `ships`: `list`, `mine`, `get`, `is_docked`, `hull`, `pilot_console`, `warp_fuel`. Read-only, cached 5 s.
- `orders`, `filters`, `warp`, `steer`, `cmd`: commands. Each returns `ok, err`. No native DLL gives `false, "native DLL not loaded"`.
- `warp.sectors()`, `docks()`, `find_item(res)`: world lookups in pure Lua.
- `loader`: `status()`, `updates()`, `on_update(fn)`.
- `native` (`MSCoreNative_*`): the DLL functions the Lua wrappers call.
- C exports for native mods: `MSCore_api()` returns 2, plus `MSCore_*` functions (`add_input`, `warp_send`, `sectors`, `find_item`, ...). Structs `MsFilterRow` and `MsSector` are in `cpp\MSCore\msc_api.hpp`.
- `hacks`: registry of every workaround, with the build it was verified on.

## Mod updates

- Each mod ships `mods\<Mod>\mscore.mod` (`name`, `repo` as `owner/repo`, `version`).
- On game start the DLL only checks `https://github.com/<repo>/releases/latest/download/manifest.txt`. It never downloads.
- A download happens only when you click Update in the manager. The zip is checked (SHA-256 and size) and staged. It is applied on the next DLL start. Old files go to `Mods\MSCore\backup\`.

## Build

Requirements: Visual Studio 2022 Build Tools (C++ and CMake), and an RE-UE4SS source tree that builds the `cppmods` set.

Environment overrides (set them when your paths differ from the defaults):

| Variable | Used by | Default (author's machine) |
|---|---|---|
| `MS_UE4SS_ROOT` | `build.ps1`, `scripts\release.ps1` | `F:\MSDemoMods\build\RE-UE4SS` |
| `MS_GAME_DIR` | `build.ps1`, `scripts\release.ps1` | `F:\SteamLibrary\steamapps\common\Main Sequence Demo` |
| `MS_LUAC` | `scripts\check.ps1` | luac on PATH, else F:\MSDemoMods-public\tests\out\luac.exe |

PowerShell example: `$env:MS_UE4SS_ROOT = 'D:\src\RE-UE4SS'`.

- `.\build.ps1`: build the DLL (no install).
- `.\build.ps1 -Install`: build and copy the DLL and Lua into the game. Close the game first. The old DLL and Scripts are backed up to `build\backups\`.
- `.\scripts\build-manager.ps1`: build `MSCoreManager.exe`.
- `.\scripts\test-loader.ps1`: build and run the loader tests (no network).
- `.\scripts\check.ps1`: `luac -p` on every mod Lua file.
- `.\scripts\release.ps1 -Version x.y.z`: build the release zips and manifest into `build\release\`.

## License

MIT. See `LICENSE`.
