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
| `ui.onClick` | `btn, fn` | - | Calls `fn()` on the game thread when `btn` is clicked. The handler is registered through `ForceLayoutPrepass`, see the gotchas. A button that is not valid is skipped (0.8.0). |
| `ui.clearClicks` | - | - | 0.8.0: drops the click map and removes the click hook. The removal is deferred 100 ms and a reopen before it cancels it. The click map is emptied, so bind the buttons again with `onClick`. |
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

## Windows: MSCore.wbp

`MSCore.wbp` opens a cooked UMG window (a WBP asset from a `/Game` path) and builds the game's inventory and cost widgets inside it. Source: `mods\MSCore\Scripts\mscore_wbp.lua`. Load it with `require("mscore")`; `MSCore.wbp` is set by `install`. Call these on the game thread (`ExecuteInGameThread`). Every call is pcall-guarded and logs its own failures.

| Function | Args | Returns | Notes |
|---|---|---|---|
| `wbp.open` | `spec, owner` | handle `{ wbp, pc }` or `nil` | `spec` = `"/Game/Path/WBP_X.WBP_X_C"` or the bare package `"/Game/Path/WBP_X"` (adds `_C`) or a class object. `owner` = your PlayerController (or `nil`: the local one is used). While open it blocks camera look; wbp.close restores it. |
| `wbp.close` | `handle` | - | Idempotent. Teardown order: grid `CleanupInventory`, `RemoveFromParent`, then input restore. Runs on a later tick. |
| `wbp.inventory_grid` | `handle, host, inv, other, columns` | grid or `nil` | Adds the game's `WBP_Inventory` into the widget named `host`, sets `Columns` and `ShowSortButton`, then `SetupInventory(inv, other, pc)`. Pass `(unit_inv, player_inv)` for the unit grid and `(player_inv, unit_inv)` for the mirror, as the game does. |
| `wbp.cost_row` | `host, item, need, have, opts` | row or `nil` | Adds the game's `WBP_ItemAmount` row to `host`. Text `have/needx`, red when `have < need`. `opts.refund = true` shows `+N refund` in green. `opts.pc` = controller. |
| `wbp.fmt_int` | `n` | string | `1595` -> `"1,595"`. |
| `wbp.geometry_log` | `widget` | - | One line: desired size and cached geometry. Use it to check layout, not to size. |
| `wbp.Z` | - | `1` | Window Z. Keep it below 2. |

## Example

```lua
local MSCore = require("mscore")
local wbp = MSCore.wbp

-- game thread
local h = wbp.open("/Game/MyMod/WBP_MyWindow", controller)
if h then
  local grid = wbp.inventory_grid(h, "Scr_UnitInv", unit_inv, player_inv, 8)
  wbp.cost_row(widget_host, item, 10, 4, {})
end

-- later, from a button or key handler (also game thread)
wbp.close(h)
```

## Lessons (proven in the Blueprint Unit window)

1. **Plain UserWidget.** The window must be a plain UserWidget created with `Create` and `AddToViewport`. Size and layout belong in the WBP: a full-screen Overlay root with a centred fixed SizeBox. Lua `SetDesiredSizeInViewport` and the anchor setters were accepted but ignored.
2. **Z below 2.** The game's `WBP_CursorItemStack` draws held items at Z 2. A window at Z 2 or higher hides the drag visual. Use `wbp.Z` (1).
3. **Input mode GameAndUIEx.** `UIOnly` makes `PlayerController:IsInputKeyDown(Shift)` return false, so shift-click quick move fails. `wbp.open` uses `GameAndUIEx(p, wbp, 0, true, false)` and falls back to the shorter forms.
4. **ESC and F.** ESC closes the modal first, then the window. F closes the window, except while an edit box has focus or a modal is open.
5. **Read FText once, and only while the widget is valid.** Crash note from the Blueprint Unit work: read the text once, then store the Lua string. Never touch the widget after it is gone.
6. **CleanupInventory before RemoveFromParent.** `wbp.close` does this. The order is mandatory.
7. **Use the game's inventory grid.** `WBP_Inventory_C` with `SetupInventory(inv, other, pc)`. Do not build your own grid.
8. **No invalid calls.** Check `IsValid` before every method call on a UObject. Never call `KismetSystemLibrary` soft-path functions from Lua: it crashes UE4SS.
9. **Headless authoring limits.** An "Is Variable" flag cannot be set from Python. Borrow the variable widgets from a donor asset that already has them, and keep the names the Lua side looks up (`GetWidgetFromName`).
