-- MSCore mod entry. Arms the map-pin hook and runs the selfcheck once ~8 s after a world loads.
local MSCore = require("mscore")
local selfcheck = require("selfcheck")
local ticks, id = 0, nil
-- DevEval (dev only, gated by Mods/MSCore/deveval_on.txt): a failure here never breaks MSCore
local okDE, DevEval = pcall(require, "deveval")
if okDE and type(DevEval) == "table" then
    local okI, eI = pcall(DevEval.init, MSCore)
    if not okI then print("[MSCore] DevEval init failed: " .. tostring(eI)) DevEval = nil end
else
    print("[MSCore] DevEval not loaded: " .. tostring(DevEval)) DevEval = nil
end
MSCore.deveval = DevEval
if DevEval then MSCore.events.on("tick", DevEval.tick) end   -- own tick subscription: the main one is removed after the selfcheck
local pinHooked = false
id = MSCore.events.on("tick", function()
    local R, S = MSCore.resolve, MSCore.safe
    if not (S.live(table.unpack(R.controller)) and S.live(table.unpack(R.player))) then ticks = 0 return end
    -- map pin hook: retried every tick until the controller and MapPinInputCommand class exist. Needs the DLL, hence the guard.
    if not pinHooked and MSCore.native then pinHooked = MSCore.native.pin_ensure() end
    ticks = ticks + 1
    if ticks >= 32 then   -- ~8 s in a world, then run once
        MSCore.events.off(id)
        selfcheck.run(MSCore)
    end
end)
-- input capture probe (no-op unless Mods/MSCore/strafe_probe_on.txt exists): arms its hook / polls, prints the rate line
MSCore.events.on("tick", function() if MSCore.native and MSCore.native.capture_tick then MSCore.native.capture_tick() end end)
print("[MSCore] loaded v" .. MSCore.version .. " api " .. MSCore.api .. ", selfcheck runs ~8 s after a world is loaded\n")
