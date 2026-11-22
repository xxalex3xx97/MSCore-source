-- Capture A/D/Shift/Ctrl flight input: hooks the controller input setters and watches the piloted ship's strafe-ish props.
-- Run while piloting a ship, 8+ s after world load (hook() refuses before). Then press keys and read eval_out.txt.
local C = "/Script/MainSequence.DMSPlayerController:"
for _, fn in ipairs({ "SetStrafingInput", "SetRotationImpulseInput", "SetThrottleInput", "SetMovementInput", "SetSprinting" }) do
    log("hook " .. fn .. " -> " .. tostring(hook(C .. fn)))   -- logs self + raw args to eval_out.txt
end
-- ship = first of ships(); edit to pick another by e.id
local ship
for _, e in ipairs(ships()) do ship = ship or e.obj end
if not ship then log("no ship found") return end
-- watch every property whose name mentions strafe/impulse/throttle (names discovered via the class chain)
local names = {}
local ok, cls = pcall(function() return ship:GetClass() end)
while ok and cls and obj_ok(cls) do
    pcall(function() cls:ForEachProperty(function(p)
        local n = p:GetFName():ToString()
        if n:lower():find("strafe") or n:lower():find("impulse") or n:lower():find("throttle") then names[#names + 1] = n end
    end) end)
    ok, cls = pcall(function() return cls:GetSuperStruct() end)
end
log("watching: " .. table.concat(names, ", "))
watch("ship_input", function()
    local r = {}
    for _, n in ipairs(names) do r[#r + 1] = n .. "=" .. fmt(get(ship, n)) end
    return table.concat(r, " ")
end, 100)
-- later: stop("ship_input"); unhook("/Script/MainSequence.DMSPlayerController:SetStrafingInput")
