# DevEval (MSCore dev tool)

Runtime probe/eval for the assistant so game state can be read and commands sent without restarting the game or rebuilding a DLL.
Code: `mods/MSCore/Scripts/deveval.lua`, loaded from `Scripts/main.lua` via `pcall(require)`; a failure never breaks MSCore.
Usage: `USAGE.txt`. Examples: `examples/`.

## Gate (OFF for normal players)
- Active only when `Mods/MSCore/deveval_on.txt` exists (checked at load and every poll; deleting it stops polling, timers and hook loggers).
- `Mods/MSCore/deveval_off.txt` = kill switch, wins over the on-marker.
- The marker is NOT in the repo and never ships; the installer creates it only for dev sessions. No network sockets.
- Hook loggers cannot be removed at runtime safely (UnregisterHook crashed the game): `unhook` only disables them; they are gone on restart/hot reload.

## Installer (dev session)
1. Copy `mods/MSCore/Scripts/*.lua` to live `Mods/MSCore/Scripts/` and `mods/MSCore/deveval/` to `Mods/MSCore/deveval/` (create empty `eval_in/` and `eval_out/`; the mod also tries to create them).
2. Create `Mods/MSCore/deveval_on.txt` (empty). No mods.txt/enabled.txt change: it is part of MSCore (already enabled).

## Hot reload (Lua changes without a game restart)
- Live `ue4ss/UE4SS-settings.ini`: `EnableHotReloadSystem = 1`, `HotReloadKey = R` (CTRL is always required) => **Ctrl+R** reloads all Lua mods.
- Applies without restart: any `.lua` in our mods (MSCore, FleetUI, DevEval ...). DevEval requests need no reload at all.
- Still needs a game restart: native DLLs (`dlls/main.dll`), pak assets, `UE4SS-settings.ini` changes.
- Cautions: do not Ctrl+R with pending ExecuteWithDelay callbacks (crashed once); hooks registered before a reload stay registered, and re-registering the same game function from a new Lua state crashed the game (2026-11-19): see FleetUI `fleet_win.lua` ("hook armed after first paint", never UnregisterHook). Do not register hooks before the world is ready: FleetUI defers its hook until the first successful paint; DevEval `hook()` refuses until the player controller has been live ~8 s (32 ticks).
