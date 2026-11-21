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

Mods run in their own Lua state and load only `mscore_wbp` (`local WBP = require "mscore_wbp"`, package.path = `<Mods>/MSCore/Scripts/?.lua`), never `mscore.lua`. Call `WBP.ship_hologram(shipId, {host = panel [, ...]})` and `WBP.item_icon(key)` (they return the texture UObject, or `nil, why`); `WBP.set_ship_hologram(img, shipId [, opts])` and `WBP.set_item_icon(img, key)` are convenience wrappers that apply it with pcall; `MSCore.wbp.*` and `MSCore.items.*` are aliases inside MSCore's own state. Standalone, the ship is found by an own `FindAllOf` cache (at most one per 5 s) and a gap of more than 8 s between calls counts as a stall (3 s settle).

Ownership / lifetime: the mod owns the picture. APPLY the returned texture to your own Image brush immediately, in the same call (`SetBrushFromTexture` or `brush.ResourceObject`, under pcall + IsValid). The Slate brush then holds a strong GC reference, so the picture stays alive even if MSCore drops its hidden widget; an MSCore refresh redraws into the same render target (`SetShip` again), so images already applied update in place. Do not keep the Lua variable across ticks; ask again on every paint. `set_ship_hologram(img, id)` / `set_item_icon(img, key)` do exactly that in one call (use them when you do not need the texture). MSCore never returns a texture that is invalid at the time of the call.

Host contract (GC safety): `opts.host` is a persistent hidden PanelWidget of your window (it lives as long as the window). Each hologram widget is created once per ship, immediately `AddChild`ed to that host and set Collapsed, so the window tree references the widget and its texture and the garbage collector cannot free them while the window lives. Without a valid host nothing is created and the call returns `nil, 'no host'`. Call `WBP.hologram_release(host)` when the window closes AND at the start of every open: it drops all cached widgets, textures, queue and cooldowns (and calls `RemoveFromParent` on the widgets only while that host is still the live one they were parented to); the next open creates fresh widgets (this is the 'refresh on open'). A different host than the cached one counts as a reopened window and releases everything. `hologram_caps()` reports `ok n=<cached widgets>`. Unrooted widgets crashed the game once the cache sat for a while, so they are never created any more.

Refresh is OFF by design (`C.holo_refresh = false` in mscore_contract.lua): rendering an existing widget again (`SetShip` a second time, with or without the ship scan) crashed the game in-game, so a picture is created once per ship per session. `opts.ttl`, `force` and `refresh_ok` are accepted and ignored (`opts.host` is required). If a cached texture turns invalid, MSCore drops that widget (no Destroy) and builds a NEW one through the normal queue and budget; it never calls `SetShip` again on an old widget. Do not enable the constant without an in-game proof. `hologram_stats()` also counts `stale_reuses`. `C.holo_debug` (default true while the crash is hunted) prints `[MSCore] HOLO STEP ...` lines before each risky call; set it false afterwards.

Fallback: if in-game testing shows flicker or a dead picture on refresh, the fallback is a pixel-copy into the caller's own render target (new render target + draw per ship): heavy, not implemented. This has not been tested in game yet.

In-game test checklist (hologram):
- Open the Fleet window; confirm rows and the Details panel show holograms.
- Let a ship leave the list (dock, destroy, leave sector) and refresh after more than 30 s: expect no crash and no grey or black row image.
- Reopen the menu within 30 s: no re-render (selfcheck `wbp.hologram ttl` counters).
- After more than 30 s: still no re-render, the old picture stays (selfcheck `wbp.hologram refresh` asserts this).

Ship hologram notes: caps key `hologram` (`ok` or `off:why`), kill switch `MSCore.module_disable("hologram")` (independent of `widgets`). Warp safety: no build within 10 s of a game stall, at most `holo_per_window` (2) renders per `holo_window` (0.25 s), extra ships wait in a FIFO queue (`pending`). Refresh: OFF (`C.holo_refresh = false` in mscore_contract.lua), so `holo_ttl` = 30 s has no effect. A ship's picture is created once and reused (no second `SetShip`, no new widget, no FindAllOf). A cached texture that turns invalid drops that widget and builds a new one. `hologram_release(host)` on close and at every open drops the cached widgets. `WBP.hologram_stats()` returns `{renders, reuses, created, queued}`. Each mod's own Lua state keeps its own cache, so call again on a later tick while `why` is `pending`.

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
| `wbp.ship_hologram` | `shipId, {host=panel}` | texture UObject, or `nil, why` | Primary. The ship's hologram texture (the game's `WBP_FleetShipEntry` render), cached 30 s, built lazily (one widget per ship). `why` = `pending` (retry on a later paint), `no ship`, `no class`, `no controller`, `bad id`, `off:...`. Apply it to your brush immediately; see Ownership / lifetime. |
| `wbp.hologram_texture` | `shipId [, opts]` | same | Alias of `wbp.ship_hologram`. |
| `wbp.set_ship_hologram` | `img, shipId [, opts]` | `true`, or `false, why` | Convenience: `ship_hologram` + `SetBrushFromTexture` under pcall. Extra `why`: `bad image`, `SetBrush failed`. |
| `wbp.item_icon` | `key` | texture, or `nil, why` | Primary item icon (see warp-docks.md). Apply it to your brush immediately; see Ownership / lifetime. |
| `wbp.item_list` | none | array of {key, name} sorted by name, or nil, why | All game items for pickers. Use wbp.item_icon(key) for each icon. nil,"pending" = not loaded yet, ask again on the next open. |
| `wbp.set_item_icon` | `img, key` | `true`, or `false, why` | Convenience wrapper. |
| `wbp.open_fleets_window` | none | `true`, or `false, why` | Opens the game's own fleet window (ShowWidget of WBP_Fleet with the PlayerObject). Game thread. |
| `wbp.open_fleet_assign` | `shipId` | `true`, or `false, why` | Add-to-fleet popup for one ship: `WidgetSubsystem:ShowPopup(WBP_TagSelectPopup_C, shipObject, nil, false)` (recipe `C.fleet_assign`). Fail-closed: `false, "no class" / "no ship" / "no widget subsystem" / "popup failed"`. Logs `[MSCore.wbp] open_fleet_assign ship=<id> class=<name> ok=<bool>`. Game thread; do not call within ~10 s after a warp. |
| `wbp.fleet_assign_caps` | none | `"ok"` or `"off:<why>"` | `"ok"` when the popup class resolves and the widget subsystem is found. Never opens anything. |
| `wbp.probe_fleet_popup` | none | nothing | Dev capture only, sends nothing: hooks ShowPopup and logs `PROBE` lines (the popup widget class own name, o1/o2 classes) to UE4SS.log. Do not call within ~10 s after a warp (same as the hologram code). |
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

## Widgets by logical name (0.8.9)

Get the game's widgets by logical name. The asset path lives in MSCore's contract table, so mods never see it.

- `wbp.widget(name)`: widget class, or `nil, why`.
- `wbp.instance(name)`: live instance of the widget, or nil.
- `wbp.on_open(name, cb)`: `cb(self)` runs on the game thread after the widget's open function. Returns true when hooked or armed.
- `wbp.close_pause(w)`: closes the pause menu. Returns how it closed, or `nil, why`.
- `wbp.is_window(o)`: true for a WBP_ window instance.
- `wbp.caps()`: `ok`, or `off:why`.
- `MSCore.module_disable("widgets")`: kill switch for the widgets module.

Names: `inventory_grid`, `item_amount`, `fleet_ship_entry`, `pause_menu`.

Check `type(wbp.widget)=='function'`; if absent show 'MSCore too old (needs 0.8.9)' and turn the feature off.
