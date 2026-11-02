-- MSCore mod entry. Arms the map-pin hook, runs the selfcheck once ~8 s after a world loads, and binds a dev steer test key.
local MSCore = require("mscore")
local selfcheck = require("selfcheck")
local ticks, id = 0, nil
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
print("[MSCore] loaded v" .. MSCore.version .. " api " .. MSCore.api .. ", selfcheck runs ~8 s after a world is loaded\n")

-- dev action, only on key press (never automatic): aim 5 deg yaw at the first mining ship, stop 1 s later
-- ponytail: key Ctrl+F9 is a guess (no dev menu in MSCore yet); change here if another key is wanted
RegisterKeyBind(Key.F9, { ModifierKey.CONTROL }, function()
    ExecuteInGameThread(function()
        local m = MSCore.ships.mine()[1]
        if not m then print("[MSCore] steer test: no mining ship\n") return end
        local ok, err = MSCore.steer.aim(m.id, 5, 0)
        print("[MSCore] steer test aim ship " .. m.id .. " -> " .. tostring(ok) .. " " .. tostring(err) .. "\n")
        ExecuteWithDelay(1000, function()
            ExecuteInGameThread(function()
                local ok2, err2 = MSCore.steer.stop(m.id)
                print("[MSCore] steer test stop ship " .. m.id .. " -> " .. tostring(ok2) .. " " .. tostring(err2) .. "\n")
            end)
        end)
    end)
end)
