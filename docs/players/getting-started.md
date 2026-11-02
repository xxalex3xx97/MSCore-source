---
title: Getting started
parent: Players
nav_order: 1
---

# Getting started

## 1. Download the manager

Open the [releases page](https://github.com/xxalex3xx97/MSCore/releases) and download `MSCoreManager.exe` from the latest release. The file is a binary program. There is no installer.

## 2. Close the game

The manager refuses to install, add, remove or roll back while the game runs. Close Main Sequence Demo first.

## 3. Start the manager

The manager looks for the game folder by itself:

- It reads the Steam install path from the registry.
- It reads every Steam library listed in `steamapps\libraryfolders.vdf`.
- It looks for a folder whose name starts with `Main Sequence` under `steamapps\common` that contains `Windows\MainSequence\Binaries\Win64`.

The folder it finds is remembered in `%APPDATA%\MSCoreManager\game.txt`.

If it finds nothing, click **Browse game folder** and pick the `Main Sequence Demo` folder. The folder must contain `Windows\MainSequence\Binaries\Win64`. The manager refuses other folders.

## 4. Install MSCore

Click **Install/Repair MSCore**. The manager:

- copies the UE4SS files (`dwmapi.dll` and `ue4ss\`) into the game folder;
- keeps an existing `UE4SS-settings.ini` and only creates it when it is missing;
- installs MSCore into `ue4ss\Mods\MSCore`;
- adds the line `MSCore : 1` to `ue4ss\Mods\mods.txt`.

Running Install again repairs the files. The manager keeps one `MSCore : 1` line in `mods.txt` and puts it at the top.

## 5. Add a mod from a GitHub link

Click **Add mod...** and type either:

- `owner/repo`, or
- a GitHub link such as `https://github.com/owner/repo`.

The manager downloads that repository's latest release, checks the file hash and size, and installs it. If the mod zip has no `enabled.txt`, the manager tells you to add `<ModName> : 1` to `mods.txt` yourself.

## 6. Update, roll back and remove

- **Check** looks for newer versions. The manager also checks when it starts. The state of each mod shows in the list (`uptodate`, `update`, and so on).
- **Update selected** or **Update all** downloads the new version. Files are staged first and applied the next time the game starts. If the game is running, the manager says that the update applies on the next launch.
- **Rollback** restores the files kept from the last update of the selected mod. Restart the game after a rollback.
- **Remove mod** deletes only the files that the manager installed for that mod. MSCore itself cannot be removed this way. The game must be closed.

The manager never downloads anything when the game starts. It only checks. Downloads happen after you click a button.

## Buttons at a glance

| Button | What it does | Game must be closed? |
|---|---|---|
| **Install/Repair MSCore** | Copies UE4SS and MSCore into the game folder and adds `MSCore : 1` to `mods.txt`. Running it again repairs the files. | Yes |
| **Add mod...** | Installs the latest release of `owner/repo` or a GitHub link. | Yes |
| **Check** | Looks for newer versions. Downloads nothing. | No (not stated) |
| **Update selected** | Downloads the new version of the selected mod. Applied at the next game start. | Applied on next start |
| **Update all** | Same as Update selected, for every mod with an update. | Applied on next start |
| **Rollback** | Restores the files kept from the last update of the selected mod. Restart the game after. | Yes |
| **Remove mod** | Deletes only the files the manager installed for that mod. MSCore cannot be removed this way. | Yes |
| **Browse game folder** | Lets you pick the `Main Sequence Demo` folder when the manager does not find it. | No (not stated) |

## 7. Start the game

Load a world and wait about 8 seconds. MSCore writes `ue4ss\Mods\MSCore\selfcheck.log`. Open it to see whether the check passed. See [Troubleshooting]({{ site.baseurl }}{% link players/troubleshooting.md %}) if it did not.

{: .warning }
**Not verified in game:** the in-game update notice line in the UE4SS log. The manager update flow was tested with the game closed.
