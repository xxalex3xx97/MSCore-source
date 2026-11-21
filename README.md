# MSCore

Shared core for Main Sequence Demo UE4SS mods. It has three parts:

- **Native DLL** (`cpp\MSCore`, UE4SS C++ mod): sends ship commands through the game's own `AddInputCommand` route, reads sectors and jump data, arms the map-pin hook, and runs the mod-update checks.
- **Lua library** (`mods\MSCore\Scripts\mscore.lua`): safe UObject access, UMG helpers, a 250 ms tick loop, and read/command wrappers for ships, orders, filters, warp, docks and items.
- **Mod manager** (`cpp\manager`, `MSCoreManager.exe`): installs, updates, rolls back and removes mods from GitHub releases. Every download is checked against SHA-256 and size.

Used by MiningBenchMod and MSDevMenu. Lua API 3, native API 5 (separate counters), version 0.8.1.

Status: the selfcheck log is the check. Run the game with MSCore installed and read `Mods\MSCore\selfcheck.log`. There is no automated game test suite.

## Install

Use **MSCoreManager.exe**. It installs UE4SS and MSCore, keeps them current and adds mods for you. Do not copy files by hand unless you have to.

1. Close the game.
2. Download `MSCoreManager.exe` from the [latest release](https://github.com/xxalex3xx97/MSCore/releases/latest) and run it.
3. Click **Install/Repair MSCore**, then **Add mod...** for each mod.
4. Start the game, load a world, wait about 8 s, then read `Mods\MSCore\selfcheck.log`.

Manual install (only if the manager cannot be used):

1. Close the game.
2. Copy `mods\MSCore` to `<game>\Windows\MainSequence\Binaries\Win64\ue4ss\Mods\MSCore`.
3. In `...\ue4ss\Mods\mods.txt`, add `MSCore : 1` above your mods.

Each UE4SS mod has its own Lua state, so each mod requires its own copy of `mscore.lua`. Add this to the top of your mod's `Scripts\main.lua`:

```lua
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")  -- this mod's Scripts/ dir
package.path = ((SRC or ""):gsub("\\", "/")) .. "../../MSCore/Scripts/?.lua;" .. package.path
local MSCore = require("mscore")
MSCore.require(1)  -- errors if the installed API is older than 1
```

See `docs\` for the mod author guide (Lua API and native API reference).

## How to use

### For players

Close the game before every manager action (it refuses while the game runs).

| I want to... | Do this in MSCoreManager.exe |
|---|---|
| Install | **Install/Repair MSCore** (installs UE4SS and MSCore, adds `MSCore : 1` to `mods.txt`) |
| Add a mod | **Add mod...**, then type `owner/repo` or a GitHub link |
| See new versions | **Check** (also runs at start). Downloads nothing |
| Update | Select the mod, **Update selected**, or **Update all**. Applied on the next game start |
| Roll back | Select the mod, **Rollback**, then restart the game |
| Remove | Select the mod, **Remove mod** (MSCore itself cannot be removed) |
| Turn a mod off or on | Select the mod, **Disable/Enable**, then restart the game |

`Add mod...` examples (any repo with a release that has `manifest.txt`):

```text
xxalex3xx97/MSMiningUnit
https://github.com/xxalex3xx97/MSMiningUnit
```

The manager tells you to add `<ModName> : 1` to `ue4ss\Mods\mods.txt` when the mod zip has no `enabled.txt`.

Command line (same actions, no window): `MSCoreManager.exe --install`, `--add owner/repo`, `--remove <ModName>`, `--update <ModName>`, `--self-update`, `--version`. The headless log is `%TEMP%\MSCoreManager.log`.

#### Read selfcheck.log

Start the game, load a world, wait about 8 s, then open `Mods\MSCore\selfcheck.log`:

```text
MSCore 0.8.15 api 3 selfcheck 2026-11-21 20:15:03
core.api                 OK       api=3 v0.8.15
safe.live player         SKIP     no player
exe size                 OK       size=149329408
SUMMARY ok=40 broken=0 changed=0 skip=2
```

| Status | Meaning |
|---|---|
| `OK` | Check passed |
| `SKIP` | Needs something not loaded yet (for example no world) |
| `BROKEN` | Check failed. Send that line when you ask for help |
| `CHANGED` | The game exe size is not the verified one: the game was probably updated. Check the manager for an MSCore update |

There is no `DISABLED` status in `selfcheck.log`. A module that switched itself off (fail closed) is logged in the UE4SS log instead, for example:

```text
[MSCore] COMPAT CAPS rocks=off:disabled (runtime)
```

`ok` is the module works, `off:<why>` is the module is off for this session. Other mods keep running.

#### Kill switches

| What | How |
|---|---|
| Turn a whole mod off | **Disable/Enable** in the manager (files move to `Mods\MSCore\disabled\<mod>`) |
| Designer guard (native hook) | Create an empty `Mods\MSCore\designer_guard_off.txt` before starting the game |
| A module at runtime (mods) | `MSCore.module_disable("blocks")`. Names: `widgets`, `blocks`, `hologram`, `items`, `fleets`, `designer_guard`, `rockreg`, `scan` |

### For mod authors

#### Minimal Lua mod

`ue4ss\Mods\MyMod\Scripts\main.lua`. MSCore must be listed above your mod in `mods.txt`:

```text
MSCore : 1
MyMod : 1
```

```lua
local SRC = debug.getinfo(1, "S").source:match("^@(.*[/\\])")
package.path = ((SRC or ""):gsub("\\", "/")) .. "../../MSCore/Scripts/?.lua;" .. package.path
local MSCore = require("mscore")
MSCore.require(3)  -- errors if the installed Lua API is older than 3

local id = MSCore.events.on("tick", function()   -- every 250 ms, game thread
    for _, s in ipairs(MSCore.ships.list()) do    -- { id, name, obj }, cached 5 s
        print(("[MyMod] ship %d %s\n"):format(s.id, s.name))
    end
    MSCore.events.off(id)                         -- run once
end)
```

Commands return `ok, err` (`false, "native DLL not loaded"` without the DLL):

```lua
local ok, err = MSCore.orders.autopilot(ship_id, true)   -- on
if not ok then print("[MyMod] " .. tostring(err) .. "\n") end
-- also: orders.new / update / delete / skip_to, filters.send, warp.send, steer.throttle
```

`events.on` only accepts `"tick"`. Always call `MSCore.safe.valid(o)` before a method on a game object.

#### Native (C++) mod

Load `ue4ss\Mods\MSCore\dlls\main.dll`, resolve `MSCore_api` first, then the exports you need:

```cpp
typedef int (*MSCore_api_fn)(void);
typedef int (*MSCore_throttle_fn)(int, int, wchar_t*, int);

HMODULE h = LoadLibraryW(dll_path);   // path to MSCore\dlls\main.dll
auto api = reinterpret_cast<MSCore_api_fn>(GetProcAddress(h, "MSCore_api"));
if (!api || api() < 16) return false;   // stop when missing or too old

auto throttle = reinterpret_cast<MSCore_throttle_fn>(GetProcAddress(h, "MSCore_throttle"));
wchar_t err[256] = {0};
if (throttle && !throttle(ship_id, 1, err, 256)) { /* err holds the reason */ }
```

Call exports from the game thread only. Require only the API number your mod needs (see `docs\mod-authors\native-api.md`).

#### mscore.mod

Put `mods\MyMod\mscore.mod` in your release zip so MSCore can check for updates (it never downloads):

```text
name=MyMod
repo=youruser/MyMod
version=0.1.0
```

The release must also carry `manifest.txt` (`releases/latest/download/manifest.txt`). `scripts\release.ps1` builds it for MSCore. Publishing steps: `docs\mod-authors\publishing.md`.

## API summary

- `safe`: `valid(o)` (call before any UObject method), `live(...)`, `objName`, `rd(f)`, `try`, `val`, `nameOf`.
- `ui`: UMG helpers (`mk`, `setText`, `onClick`, `rgba`, `smallFont`, `hadd`). `events.on("tick", fn)` / `events.off(id)`: one 250 ms loop.
- `resolve`: game class and path names, in one table.
- `ships`: `list`, `mine`, `get`, `is_docked`, `hull`, `pilot_console`, `warp_fuel`. Read-only, cached 5 s.
- `orders`, `filters`, `warp`, `steer`, `cmd`: commands. Each returns `ok, err`. No native DLL gives `false, "native DLL not loaded"`.
- `warp.sectors()`, `docks()`, `find_item(res)`: world lookups in pure Lua.
- `loader`: `status()`, `updates()`, `on_update(fn)`.
- `native` (`MSCoreNative_*`): the DLL functions the Lua wrappers call.
- C exports for native mods: `MSCore_api()` returns 5 (0.8.0 station block natives are Lua only), plus `MSCore_*` functions (`add_input`, `warp_send`, `sectors`, `find_item`, ...). Structs `MsFilterRow` and `MsSector` are in `cpp\MSCore\msc_api.hpp`.
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
