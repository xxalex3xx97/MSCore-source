---
title: Troubleshooting
parent: Players
nav_order: 3
---

# Troubleshooting

## Quick answers (FAQ)

| Problem | Cause | Fix |
|---|---|---|
| The manager says "Game is running - close it first". | A `MainSequence*` process runs from the game folder. | Close Main Sequence Demo completely. Check Task Manager for a leftover `MainSequence` process, then try again. |
| The manager does not find the game. | Auto-detection found nothing. | Click **Browse game folder** and pick the `Main Sequence Demo` folder. It must contain `Windows\MainSequence\Binaries\Win64`. |
| The manager shows an old version after a new release. | GitHub served a cached manifest, or the release is not marked Latest. Manager versions before 0.5.1 could drop the cache-busting query string. | Click **Check** again. Make sure the release is marked **Latest** on GitHub. Update the manager to 0.5.1 or later. See [Stale cache](#stale-cache). |
| The mod does not load. | `mods.txt` has no line for the mod, or the mod folder has no `enabled.txt`. | Check that `ue4ss\Mods\mods.txt` has `MSCore : 1` and `<ModName> : 1`. If the mod folder has no `enabled.txt`, add the line to `mods.txt` yourself. Check the UE4SS log for errors from your mod. |
| Some MSCore features do nothing. Lua calls return `native DLL not loaded`. | The native DLL is missing. It must be at `ue4ss\Mods\MSCore\dlls\main.dll`. | Click **Install/Repair MSCore**. The selfcheck log shows this as `BROKEN`. |
| `selfcheck.log` has a `BROKEN` line. | The check failed or raised an error. | Send that line when you ask for help. |
| `selfcheck.log` has a `CHANGED` line. | The game exe size is not the one MSCore was verified with. The game was probably updated. | Check the manager for an MSCore update. MSCore may not work until it is updated. |
| A mod update does not apply. | Updates are staged and applied when the game starts. | Close the game fully, start it again, and check the version in the manager. The manager log shows the staging lines. |

## Where the logs are

| Log | Where | What it holds |
|---|---|---|
| Manager log | `%TEMP%\MSCoreManager.log` (usually `C:\Users\<you>\AppData\Local\Temp\MSCoreManager.log`) | Every manager action: `install`, `add`, `remove`, errors. Lines are added at the end. |
| UE4SS log | The `ue4ss` folder in the game (`Windows\MainSequence\Binaries\Win64\ue4ss`) | Output of UE4SS and every mod, including `[MSCore]` lines. The file name is `UE4SS.log` by UE4SS default. |
| Selfcheck log | `ue4ss\Mods\MSCore\selfcheck.log` | Result of the MSCore self check. Written about 8 seconds after a world loads. |

{: .warning }
**Not verified in game:** the UE4SS log file name and location for this install.

## Reading selfcheck.log

Each line has one status:

| Status | Meaning |
|---|---|
| `OK` | The check passed. |
| `BROKEN` | The check failed or raised an error. Send this line when you ask for help. |
| `SKIP` | The check needs something that is not loaded yet (for example no world). |
| `CHANGED` | The game exe size is not the one MSCore was verified with. The game was probably updated. |

The last line is a summary: `SUMMARY ok=... broken=... changed=... skip=...`.

## Stale cache

The manager asks GitHub for `manifest.txt` with a `?t=<time>` query string. That makes GitHub return the current file and not an old cached copy.

If the manager shows an old version after a new release:

1. Click **Check** again.
2. Make sure the new release is marked **Latest** on GitHub. The manager reads `releases/latest`.
3. Before version 0.5.1, the query string could be dropped and an old manifest could be read. Update the manager to 0.5.1 or later.
