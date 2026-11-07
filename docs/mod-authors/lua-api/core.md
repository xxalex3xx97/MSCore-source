---
title: Core
parent: Lua API
grand_parent: Mod authors
nav_order: 1
---

# Core

Top-level `MSCore` fields, `MSCore.require` and `MSCore.logger`.

## Fields

| Field | Type | Notes |
|---|---|---|
| `MSCore.api` | number | `3` |
| `MSCore.version` | string | `"0.7.3"` |
| `MSCore.verified_build` | number | `149200896`, the game exe size MSCore was verified with. |
| `MSCore.native` | table or `nil` | Wrappers for the `MSCoreNative_*` globals. `nil` when the DLL is not loaded. |

## Functions

| Function | Args | Returns | Notes |
|---|---|---|---|
| `MSCore.require(n)` | `n`: API number | `MSCore` | Errors when `n` is not a number or is larger than `MSCore.api`. |
| `MSCore.logger(tag)` | `tag`: string prefix | `log, try` | See the two helpers below. |
| `log(...)` | any values | - | Prints `tag` followed by all arguments joined with spaces. |
| `try(label, f, ...)` | `label`, function `f`, extra args | `ok, result` | Calls `f` with `pcall`. Logs `label ok` or `label FAIL: <error>`. |

`log` and `try` come from `MSCore.logger`. They are not fields of `MSCore`.

## Example

```lua
local MSCore = require("mscore")
MSCore.require(1)

local log, try = MSCore.logger("[MyMod] ")
local ok, v = try("read hull", function() return 1 + 1 end)
log("ok =", tostring(ok), "value =", tostring(v))
```

## Gotchas

- Put `MSCore.require(n)` at the top of `main.lua`. It stops the mod early when the installed API is older.
- Check `MSCore.native` before you use it: `if MSCore.native then ... end`. It is `nil` when the DLL is missing.
- `safe.try` in [Safe and objects]({{ site.baseurl }}{% link mod-authors/lua-api/safe-objects.md %}) does the same job as `try`.
