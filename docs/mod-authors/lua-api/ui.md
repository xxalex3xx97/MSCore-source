---
title: UI
parent: Lua API
grand_parent: Mod authors
nav_order: 3
---

# UI

Helpers for building simple UMG widgets: create objects, set text, convert colours and add padding.

## Functions

| Function | Args | Returns | Notes |
|---|---|---|---|
| `ui.mk` | `cls, outer` | UMG object | Creates `/Script/UMG.<cls>` (for example `"TextBlock"`), owned by `outer`. |
| `ui.setText` | `tb, s` | - | Sets a TextBlock's text. |
| `ui.onClick` | `btn, fn` | - | Calls `fn()` on the game thread when `btn` is clicked. The handler is registered through `ForceLayoutPrepass`, see the gotchas. |
| `ui.lin` | `c` | number | Converts one sRGB value (0 to 255) to a linear 0 to 1 number. |
| `ui.rgba` | `r, g, b, a` | `{ R, G, B, A }` | `r, g, b` are converted with `lin`. `a` defaults to `1`. |
| `ui.smallFont` | `tb, size` | - | Sets the font size of a text block. Logs one failure line at most. |
| `ui.hadd` | `hb, w` | - | Adds `w` to a horizontal box with 4 px left and right padding. |

## Example

```lua
local MSCore = require("mscore")

local tb = MSCore.ui.mk("TextBlock", controller)
MSCore.ui.setText(tb, "Hello")
local red = MSCore.ui.rgba(255, 0, 0)
```

`controller` is the player controller. Find it with `MSCore.safe.live(table.unpack(MSCore.resolve.controller))`.

## Gotchas

- The click handler uses the `ForceLayoutPrepass` hook. This is a workaround. It is listed in `hacks` as `click_hook` (see [Units and hacks]({{ site.baseurl }}{% link mod-authors/lua-api/units-hacks.md %})).
- `ui.rgba` takes sRGB values from 0 to 255, not 0 to 1.
