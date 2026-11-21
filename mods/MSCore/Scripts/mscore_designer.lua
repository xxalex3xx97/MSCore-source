-- mscore_designer: status + kill switch + log for the native Designer IsBlockAvailable null guard (cpp/MSCore/msc_designer.hpp).
-- The guard itself is native (installed at MSCore start): the game's Designer::IsBlockAvailable crashes (read 0x110) when the Designer UMG
-- Blueprint passes a null block class. A Lua RegisterHook pre-hook cannot fix that: for a call from the Blueprint VM the native thunk
-- re-reads its arguments from the bytecode, so a mutated hook parameter is not used.
local M = {}
local off

local function nat(name) local f = rawget(_G, name); return type(f) == "function" and f or nil end

-- "ok" | "off:<why>"
function M.caps()
    if off then return "off:" .. off end
    local f = nat("MSCoreNative_designer_guard_status")
    if not f then return "off:native too old" end
    local ok, s = pcall(f)
    s = ok and tostring(s) or "error"
    if s:find("^on ") then return "ok" end
    return "off:" .. s
end

-- "<status> blocked=<n>" or nil
function M.status()
    local f = nat("MSCoreNative_designer_guard_status")
    if not f then return nil end
    local ok, s = pcall(f)
    return ok and tostring(s) or nil
end

-- kill switch: original game bytes back (the null crash returns)
function M.disable(why)
    off = tostring(why or "disabled")
    local f = nat("MSCoreNative_designer_guard")
    if f then pcall(f, false) end
    print("[MSCore] COMPAT CAPS designer_guard=off:" .. off .. " (runtime)\n")
end

-- logs when the guard blocked a call: first 20 changes, then every 100th count
local last, logged = 0, 0
local function poll()
    local s = M.status()
    local n = s and tonumber(s:match("blocked=(%d+)"))
    if not n or n == last then return end
    last = n
    logged = logged + 1
    if logged <= 20 or n % 100 == 0 then
        print("[MSCore] DESIGNER GUARD null block class (n=" .. n .. ") IsBlockAvailable returned false instead of crashing\n")
    end
end

function M.install(MSCore)
    MSCore.designer = M
    if MSCore.events and MSCore.events.on then
        local ticks = 0
        MSCore.events.on("tick", function() ticks = ticks + 1; if ticks % 8 == 0 then pcall(poll) end end)
    end
end

return M
