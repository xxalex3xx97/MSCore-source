---
title: Home
nav_order: 1
layout: default
---

# MSCore

MSCore is a shared base for mods of the **Main Sequence Demo** game.

**Search the docs:** press `/` or use the search box at the top of the sidebar.

## The three parts

| # | Part | File | What it does |
|---|---|---|---|
| 1 | Mod manager | `MSCoreManager.exe` | Installs MSCore and UE4SS, adds mods from GitHub, checks for updates, rolls back and removes mods. |
| 2 | Lua helpers | `mscore.lua` | Safe wrappers around the game's objects, UI helpers, a 250 ms tick loop, ship and dock lookups, command helpers for mods. |
| 3 | Native part | `MSCore.dll` (C++) | Sends ship commands, orders, warp and filter commands. Exports a C API for other native mods. |

Current version: **0.7.0** (Lua API 3, native API 4; separate counters).

## Quick links

| Area | Start here | Also see |
|---|---|---|
| Players | [Getting started]({{ site.baseurl }}{% link players/getting-started.md %}) | [Safety]({{ site.baseurl }}{% link players/safety.md %}), [Troubleshooting]({{ site.baseurl }}{% link players/troubleshooting.md %}) |
| Mod authors | [Quickstart]({{ site.baseurl }}{% link mod-authors/quickstart.md %}) | [Mod authors overview]({{ site.baseurl }}{% link mod-authors.md %}) |
| API (Lua) | [Lua API]({{ site.baseurl }}{% link mod-authors/lua-api.md %}) | [Quick reference]({{ site.baseurl }}{% link quick-reference.md %}) |
| API (native) | [Native C API]({{ site.baseurl }}{% link mod-authors/native-api.md %}) | [Glossary]({{ site.baseurl }}{% link glossary.md %}) |
| Publishing | [Publishing a mod]({{ site.baseurl }}{% link mod-authors/publishing.md %}) | [Release checklist]({{ site.baseurl }}{% link mod-authors/publishing.md %}#release-checklist) |
| History | [Changelog]({{ site.baseurl }}{% link changelog.md %}) | |

## Downloads are binaries only

The GitHub releases contain compiled files only: `MSCoreManager.exe`, the MSCore zip, the UE4SS zip and `manifest.txt`. No source code is published in the releases.

## Status markers

Some parts were built and tested only offline. Those parts are marked with a warning on each page:

{: .warning }
**Not verified in game:** the code exists and was checked outside the game. It does not mean the feature is broken.

## Requirements

- Main Sequence Demo (Steam install).
- Windows.
- Internet access for the manager and for update checks (GitHub only).
