---
title: Orders and filters
parent: Lua API
grand_parent: Mod authors
nav_order: 6
---

# Orders and filters

`orders` manages ship orders and autopilot. `filters` sets logistic filter rows. Both need the native DLL, and every function returns `ok, err`.

{: .warning }
**Not verified in game:** through MSCore (Lua API 3).

## orders

| Function | Args | Returns | Notes |
|---|---|---|---|
| `orders.new` | `receiver, cls` | `ok, err` | Creates an order of class `/Script/MainSequence.<cls>` for `receiver`. The new order id is not returned. |
| `orders.update` | `oid, target, item, cmp, amount` | `ok, err` | Updates order `oid`. `item` is a UObject or `nil`. `target`, `cmp` and `amount` are integers. |
| `orders.delete` | `sid, oid` | `ok, err` | Deletes top-level order `oid` of ship `sid`. |
| `orders.autopilot` | `sid, on` | `ok, err` | Turns the ship autopilot on (`true`) or off (`false`). |
| `orders.skip_to` | `sid, idx` | `ok, err, before, after` | Jumps to order index `idx`. `before` and `after` are the current order index around the call. |

## filters

| Function | Args | Returns | Notes |
|---|---|---|---|
| `filters.send` | `sid, idx, item, amount, action, remove` | `ok, err` | Sets logistic filter row `idx` of ship `sid`. `item` is a UObject or `nil` (empty row). `action`: `0` = LIMIT, `1` = BUFFER. `remove = true` removes the row. |

## Example

```lua
local MSCore = require("mscore")

local s = MSCore.ships.mine()[1]
if s then
    local ok, err = MSCore.orders.autopilot(s.id, true)
    if not ok then print(err .. "\n") end

    local ok2, err2, before, after = MSCore.orders.skip_to(s.id, 0)
    print("order index " .. tostring(before) .. " -> " .. tostring(after) .. "\n")
end
```

## Gotchas

- `orders.new` does not return the new order id.
- `orders.autopilot` takes a boolean. The native `autopilot` takes `0` or `1` (see [Native bridge]({{ site.baseurl }}{% link mod-authors/lua-api/native-bridge.md %})).
