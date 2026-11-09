-- Plain Lua 5.4 tests for MSCore.events tick loop (no game). Run: lua test_events.lua  (from any cwd)
local dir = (arg and arg[0] or ""):match("^(.*)[/\\]") or "."
package.path = dir .. "/../Scripts/?.lua;" .. package.path

local pass, fail = 0, 0
local function check(name, cond)
    if cond then
        pass = pass + 1
        print("PASS " .. name)
    else
        fail = fail + 1
        print("FAIL " .. name)
    end
end

-- load mscore.lua with every UE4SS global stubbed (MSCore* names stay nil); fakes = globals set for this case.
-- _G metatable and fakes are restored on every path (pass or error in the body)
local function withStubs(fakes, body)
    setmetatable(_G, { __index = function(_, k)
        if k == "LoopInGameThreadWithDelay" or k == "CancelDelayedAction" then return nil end
        if type(k) == "string" and not k:match("^MSCore") then return function() end end
        return nil
    end })
    local keys = {}
    for k, v in pairs(fakes) do keys[#keys + 1] = k; rawset(_G, k, v) end
    local ok, err = pcall(body)
    for _, k in ipairs(keys) do rawset(_G, k, nil) end
    setmetatable(_G, nil)
    if not ok then check("case ran without error: " .. tostring(err), false) end
end

-- ===== (a) LoopInGameThreadWithDelay present: game-thread timer, no LoopAsync
do
    local gtMs, gtCb, gtCalls, asyncUsed = nil, nil, 0, false
    local cancelled
    withStubs({
        LoopInGameThreadWithDelay = function(ms, f) gtMs = ms; gtCb = f; gtCalls = gtCalls + 1; return 7 end,
        CancelDelayedAction = function(h) cancelled = h end,
        LoopAsync = function() asyncUsed = true end,
    }, function()
        local MSC = dofile(dir .. "/../Scripts/mscore.lua")
        check("GT: loop registered at 250 ms", gtMs == 250)
        check("GT: LoopAsync not used", asyncUsed == false)
        check("GT: exactly one timer at load", gtCalls == 1)
        check("GT: events.running() true", MSC.events.running() == true)

        local hits = 0
        local id = MSC.events.on("tick", function() hits = hits + 1 end)
        gtCb()
        check("GT: registered tick fn runs from timer callback", hits == 1)

        MSC.events.on("tick", function() end)
        check("GT: second events.on does not start another timer", gtCalls == 1)

        local before = 0
        MSC.events.on("tick", function() error("boom") end)
        MSC.events.on("tick", function() before = before + 1 end)
        gtCb()
        check("GT: erroring tick fn does not stop later fns", before == 1)

        local hitsBefore = hits
        MSC.events.off(id)
        gtCb()
        check("GT: events.off(id) stops that fn", hits == hitsBefore)
        check("GT: no cancel on a plain tick", cancelled == nil)
    end)
end

-- ===== (b) LoopInGameThreadWithDelay absent: falls back to LoopAsync + ExecuteInGameThread
do
    local acb, acbMs
    withStubs({
        LoopAsync = function(ms, f) acbMs = ms; acb = f end,
        ExecuteInGameThread = function(f) f() end,
    }, function()
        check("LA: LoopInGameThreadWithDelay is nil", LoopInGameThreadWithDelay == nil)
        local MSC = dofile(dir .. "/../Scripts/mscore.lua")
        check("LA: fallback loop registered at 250 ms", acb ~= nil and acbMs == 250)

        local hits = 0
        MSC.events.on("tick", function() hits = hits + 1 end)
        local r = acb()
        check("LA: fallback lambda runs registered tick fn", hits == 1)
        check("LA: fallback lambda returns false", r == false)
    end)
end

print(string.format("%d passed, %d failed", pass, fail))
os.exit(fail == 0 and 0 or 1)
