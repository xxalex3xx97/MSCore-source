---
title: Safety
parent: Players
nav_order: 2
---

# Safety

## Where files come from

- The manager and the mod zips come from **GitHub only**. Downloads are allowed from `github.com` and the GitHub file hosts (`objects.githubusercontent.com`, `release-assets.githubusercontent.com`, `github-releases.githubusercontent.com`). Any other host is refused.
- Only HTTPS is used for downloads.
- Downloads are binaries. Each download is checked against the SHA-256 hash and size in `manifest.txt` before it is used. A mismatch stops the update.

## What the manager writes

The manager writes only inside the game folder:

| Path (inside the game folder) | Written when |
|---|---|
| `Windows\MainSequence\Binaries\Win64\dwmapi.dll` and the `ue4ss` folder | Install/Repair |
| `ue4ss\Mods\<Mod>\` and `ue4ss\Mods\mods.txt` | Install, update, remove |
| `Windows\MainSequence\Content\Paks\` | Mods that ship `.pak` files |
| `ue4ss\Mods\MSCore\` | MSCore's own files, backups and staged updates |

Zip paths must start with `Mods/` or `Paks/`. Paths with `..`, absolute paths and drive letters are refused.

Allowed file types inside a mod zip: `.dll`, `.lua`, `.pak`, `.utoc`, `.ucas`, `.txt`, `.mod`, `.ini`.

## What is never overwritten

These files are never replaced by an update:

- `MiningBench.ini`
- `loader.ini`
- any `*.log` file

## Removing a mod is limited

**Remove mod** deletes only the files listed in that mod's install list. If an update had replaced one of those files, the manager puts the old file back from the backup first. Then it deletes that mod's backup, staged-update folder and its `mods.txt` line.

Files owned by other mods, and your own files, are not touched.

## Old backups

Before an update replaces a file, the old file is kept in `ue4ss\Mods\MSCore\backup\<mod>\`. Rollback uses these copies. Removing the mod deletes this folder.

## The game must be closed

Install, add, remove and rollback all check whether a `MainSequence*` process runs from the game folder. If one does, the action stops with the message "Game is running - close it first". This protects the files the game has open.

## Automatic update checks do not install

The DLL checks for new versions when the game starts. It only writes a log line such as `update available`. It never downloads or stages a file. Only a click in the manager does that.
