---
title: Publishing a mod
parent: Mod authors
nav_order: 4
---

# Publishing a mod

A mod is published as a GitHub release. The MSCore manager finds it by the repository name, reads `manifest.txt` from the latest release and installs the zip.

{: .important }
**Downloads are binaries only.** Publish compiled files (zip, manifest, exe). Do not attach source code to the release. Do not copy source into the public repository.

## mscore.mod

Put this file in your mod folder as `Mods\<Mod>\mscore.mod`:

```text
name=MyMod
repo=owner/MyMod
version=0.1.0
```

| Key | Meaning |
|---|---|
| `name` | Mod name. Used as the folder name and in `mods.txt`. |
| `repo` | GitHub `owner/repo`. Used for update checks. |
| `version` | Installed version, semver `x.y.z`. |

The DLL scans every `Mods\<Mod>\mscore.mod`. The old `force_install` key is ignored.

## manifest.txt

Attach `manifest.txt` to the release. Keys, one per line, `key=value`:

| Key | Required | Meaning |
|---|---|---|
| `name` | yes | Mod name. |
| `version` | yes | Release version. |
| `zip` | yes | Zip file name in the same release. |
| `sha256` | yes | SHA-256 of the zip, lower case hex. |
| `size` | recommended | Zip size in bytes. The download must match it. |
| `min_mscore_native` | no | Oldest MSCore native version allowed. Older installs are shown as `skipped:min_native`. |

Example for a mod (the release script writes the real values):

```text
name=MyMod
version=0.1.0
zip=MyMod-0.1.0.zip
sha256=<64 lowercase hex characters>
size=<zip size in bytes>
min_mscore_native=0.5.0
```

MSCore's own manifest also has `ue4ss_version`, `ue4ss_zip`, `ue4ss_sha256` and `ue4ss_size` for the UE4SS zip. Your mod does not need them.

`manifest.txt` is read from:

```text
https://github.com/<owner>/<repo>/releases/latest/download/manifest.txt?t=<time>
```

So the release must be marked **Latest**.

## Zip layout

The zip has top-level folders `Mods/` and `Paks/` only:

```text
MyMod-0.1.0.zip
├── Mods/
│   └── MyMod/
│       ├── mscore.mod          (written by release-mod.ps1)
│       ├── enabled.txt         (empty file)
│       ├── Scripts/
│       │   └── main.lua
│       └── dlls/
│           └── main.dll
└── Paks/                       (only for mods that ship pak files)
    ├── MyMod-P.pak
    ├── MyMod-P.utoc
    └── MyMod-P.ucas
```

Rules:

- `Mods/...` goes to `ue4ss\Mods\...`.
- `Paks/...` goes to `Windows\MainSequence\Content\Paks\`.
- Paths with `..`, absolute paths, drive letters and empty segments are refused.
- Allowed file types: `.dll`, `.lua`, `.pak`, `.utoc`, `.ucas`, `.txt`, `.mod`, `.ini`. Any other type stops the install.
- Files named `MiningBench.ini`, `loader.ini` and `*.log` are never overwritten by an update.
- `enabled.txt` is an empty file in the mod folder. Include it so the mod is loaded. If it is missing, the manager tells the player to add `<Mod> : 1` to `mods.txt`.

## Build the release with the scripts

The release scripts are development tools. They are not in the public repositories.

### MSCore

`scripts\release.ps1` (MSCore source tree) builds MSCore and writes `build\release\<Version>\`:

```powershell
powershell -File scripts\release.ps1 -Version 0.5.1
```

Outputs: `MSCore-<ver>.zip`, `ue4ss-<ver>.zip`, `MSCoreManager.exe` and `manifest.txt`. It refuses a version that does not match `mscore.mod`.

### Other mods

`scripts\release-mod.ps1` takes explicit file pairs and refuses source and user data:

```powershell
powershell -File scripts\release-mod.ps1 -Name MyMod -Repo owner/MyMod -Version 0.1.0 `
    -Files @('C:\build\main.dll=Mods/MyMod/dlls/main.dll', 'C:\build\main.lua=Mods/MyMod/Scripts/main.lua') `
    -Empty @('Mods/MyMod/enabled.txt') -OutDir C:\out\MyMod\0.1.0
```

Rules in `release-mod.ps1`:

- Zip paths must start with `Mods/` or `Paks/`.
- Refused file endings: `.cpp .hpp .h .c .cc .py .ps1 .js .md .ini .log .bat .sln .vcxproj`. The script refuses `.ini` even though the loader accepts it. Do not package `.ini` files with this script.
- Refused: text dump files inside `Mods/<Mod>/dlls/`.
- Do not list `Mods/<Mod>/mscore.mod` in `-Files`. The script writes it.
- Duplicate zip paths stop the script.
- The script re-reads the zip listing and stops if there are extra or missing entries.

The script writes `manifest.txt` with `name`, `version`, `zip`, `sha256`, `size` and `min_mscore_native` (default `0.5.0`).

## Release checklist

1. Bump `version` in `mscore.mod` (MSCore) or in your own `mscore.mod`.
2. For MSCore, bump the version string in `mscore.lua`, the native version and `mscore.mod` together. Rebuild.
3. Run the release script. Check the file list it prints.
4. Install the zip into a test copy of the game folder with the manager (`MSCoreManager.exe --game <Win64> --source <release folder> --install`, or `--add owner/repo`). Confirm `mods.txt` has one line for your mod.
5. Start the game, check `selfcheck.log` has no `BROKEN` line.
6. Create a GitHub release with tag `v<version>` (for example `v0.1.0`). Attach `manifest.txt`, the zip(s), and for MSCore the `MSCoreManager.exe`.
7. Mark the release as **Latest**.
8. Check the manifest URL opens in a browser.
9. Click **Check** in the manager and confirm an older install shows `update`.

## How updates, rollback and remove work

| Action | What happens |
|---|---|
| **Check** | Reads `manifest.txt` and compares `version` with the installed `mscore.mod`. Result: `uptodate`, `update`, `skipped:min_native` or `error:<msg>`. Nothing is downloaded. |
| **Update** | Downloads the zip, checks `sha256` and `size`, and extracts it to `ue4ss\Mods\MSCore\updates\<mod>\<version>\`. It adds a line to `ue4ss\Mods\MSCore\pending.txt`. The next DLL start applies it: each old file is copied to `ue4ss\Mods\MSCore\backup\<mod>\` and the new file is moved in. |
| **Rollback** | Copies the backup files back. A current file is renamed to `<file>.old` first. Restart the game afterwards. |
| **Remove** | Deletes only the files in the mod's install list, restores backups where an update replaced a file, removes the mod's backup, staged update and `mods.txt` line. It refuses MSCore. The game must be closed. |

{: .warning }
**Not verified in game:** the in-game update notice path and the loader log lines. The manager update, add and remove flows were tested with the game closed.

## Caching

GitHub serves release assets through a CDN that caches them. The manager adds `?t=<unix time>` to the manifest URL so it always reads the current file. Version 0.5.1 fixed a bug where that query string was dropped. Before 0.5.1, an old manifest could be read after a new release. See [Stale cache]({{ site.baseurl }}{% link players/troubleshooting.md %}#stale-cache).

Publish the new release before you tell players to check for updates. Assets already in the CDN keep their old content for a while after a re-upload.
