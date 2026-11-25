-- Dump the first ship's id, name and all properties (class chain) to eval_out/<request>.txt
local l = ships()
log("ships: " .. #l)
for i, e in ipairs(l) do log(i, e.id, e.name) end
if l[1] then dump(l[1].obj) end
