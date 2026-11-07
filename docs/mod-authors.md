---
title: Mod authors
nav_order: 3
has_children: true
---

# Mod authors

MSCore gives your UE4SS Lua mod and your native mod shared helpers, so every mod uses one copy.

| Page | Read it when |
|---|---|
| [Quickstart]({{ site.baseurl }}{% link mod-authors/quickstart.md %}) | You want the first working mod. |
| [Lua API]({{ site.baseurl }}{% link mod-authors/lua-api.md %}) | You need a Lua function. Overview plus one page per module group. |
| [Native C API]({{ site.baseurl }}{% link mod-authors/native-api.md %}) | You call `MSCore_*` exports from a C++ mod. |
| [Patching paks]({{ site.baseurl }}{% link mod-authors/patching-paks.md %}) | You ship a cooked pak: chunk names, retoc, registry and research patches. |
| [Publishing a mod]({{ site.baseurl }}{% link mod-authors/publishing.md %}) | You build a zip, a manifest and a GitHub release. |
| [Quick reference]({{ site.baseurl }}{% link quick-reference.md %}) | You want the most used calls on one page. |
| [Glossary]({{ site.baseurl }}{% link glossary.md %}) | You meet a term you do not know. |

{: .note }
Each UE4SS mod has its own Lua state. Each Lua mod must `require` its own copy of `mscore.lua` from `Mods\MSCore\Scripts`. Do not share Lua tables between mods.
