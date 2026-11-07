-- mscore_inv: inventory helpers over Inventory.ItemSlots. Installs as MSCore.inv (require("mscore_inv").install(MSCore)).
-- Pure parts (stage_moves, moveout_slots, room_from_slots) run in any Lua 5.4. Game reads are guarded (pcall + IsValid).
-- slot = {item=name|nil, amount=n, max=stack size}. Empty slot = amount 0.
-- API: M.slots(inv) -> slots; M.total(inv, item) -> n; M.free_space(inv, item, amount) -> fits, room
--      M.stack_multiplier(inv) -> m, pct (pct = (m-2)*50; m = 2 is +0%); M.find_container(parent_id, class_name) -> obj | nil
--      M.stage_moves(slots, shortfall) -> indices, remaining; M.moveout_slots(slots, items) -> indices, short
--      M.room_from_slots(slots, item, max) -> n

local M = {}

local function valid(o)
    if o == nil or type(o) ~= "userdata" then return false end
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v == true
end

local function read_slot(e)
    local item, max, amount = nil, 1, 0
    local ok = pcall(function()
        local it = e.Item
        if valid(it) then
            item = it:GetFName():ToString()
            local ms = it.MaxStackSize
            if type(ms) == "number" and ms > 0 then max = ms end
        end
        amount = tonumber(e.Amount) or 0
    end)
    if not ok then amount = 0 end
    return { item = item, amount = amount, max = max }
end

local NATIVE_READ  -- factory_native.read_inventory when MSCore 0.7.3+ is installed (set in install)

-- ItemSlots as slot records, in array order (empty slots included)
-- Native first: Lua struct reads of ItemSlots are unreliable in UE4SS. Falls back to the Lua read when native is missing or fails.
function M.slots(inv)
    local out = {}
    if not valid(inv) then return out end
    if NATIVE_READ then
        local okId, id = pcall(function() return inv.ID end)
        local slots = okId and type(id) == "number" and NATIVE_READ(id) or nil
        if slots then
            for i, s in ipairs(slots) do
                out[i] = { item = s.item ~= "" and s.item or nil, amount = s.amount, max = s.max }
            end
            return out
        end
    end
    local ok, arr = pcall(function() return inv.ItemSlots end)
    if not ok or arr == nil then return out end
    pcall(function()
        arr:ForEach(function(_, e) out[#out + 1] = read_slot(e) end)
    end)
    return out
end

function M.total(inv, item)
    local n = 0
    for _, s in ipairs(M.slots(inv)) do
        if s.item == item then n = n + s.amount end
    end
    return n
end

-- room for `item` (stack size max) in slots: free part of its stacks + empty slots x max. Pure.
function M.room_from_slots(slots, item, max)
    local room = 0
    for _, s in ipairs(slots) do
        if s.amount <= 0 then
            room = room + max
        elseif s.item == item then
            room = room + math.max(0, max - s.amount)
        end
    end
    return room
end

-- item = item object (max read from MaxStackSize) or item name (max 1). Returns fits, room.
-- ponytail: Inventory.HasSpaceForItems not called: its struct argument cannot be built from Lua safely
-- (a native AV in it is not caught by pcall). Slot math only, MaxStackSize only (StackMultiplier ignored, conservative).
function M.free_space(inv, item, amount)
    local name, max = item, 1
    if type(item) == "userdata" then
        local ok, n, ms = pcall(function()
            return item:GetFName():ToString(), item.MaxStackSize
        end)
        if ok and type(ms) == "number" and ms > 0 then max = ms end
        name = ok and n or nil
    end
    local room = M.room_from_slots(M.slots(inv), name, max)
    return room >= amount, room
end

-- Inventory.StackMultiplier (read only). Baseline 2 = +0%; bonus pct = (m-2)*50.
function M.stack_multiplier(inv)
    if not valid(inv) then return nil end
    local ok, m = pcall(function() return inv.StackMultiplier end)
    if ok and type(m) == "number" then return m, (m - 2) * 50 end
    return nil
end

-- ContainerObject with ParentID == parent_id and class name == class_name (FindAllOf each call)
function M.find_container(parent_id, class_name)
    local ok, all = pcall(FindAllOf, "ContainerObject")
    if not ok or not all then return nil end
    for _, o in ipairs(all) do
        if valid(o) then
            local okp, pid = pcall(function() return o.ParentID end)
            local okc, cls = pcall(function() return o:GetClass():GetFName():ToString() end)
            if okp and pid == parent_id and okc and cls == class_name then return o end
        end
    end
    return nil
end

-- stage_moves: pick whole slots in order (skip empty) until shortfall is covered. Returns indices, remaining (0 = covered).
function M.stage_moves(slots, shortfall)
    local picks, need = {}, shortfall
    for i, s in ipairs(slots) do
        if need <= 0 then break end
        if s.amount > 0 then
            picks[#picks + 1] = i
            need = need - s.amount
        end
    end
    return picks, math.max(0, need)
end

-- moveout_slots: items = list of {item=name, amount=n}. Per item, pick whole slots holding it in order until n is covered
-- (a slot is picked once). Returns indices (pick order), short = {{item=, amount=}} still missing.
function M.moveout_slots(slots, items)
    local picks, short, used = {}, {}, {}
    for _, want in ipairs(items) do
        local need = want.amount
        for i, s in ipairs(slots) do
            if need <= 0 then break end
            if not used[i] and s.amount > 0 and s.item == want.item then
                used[i] = true
                picks[#picks + 1] = i
                need = need - s.amount
            end
        end
        if need > 0 then short[#short + 1] = { item = want.item, amount = need } end
    end
    return picks, short
end

function M.install(MSCore)
    MSCore.inv = M
    NATIVE_READ = MSCore.factory_native and MSCore.factory_native.read_inventory
    return M
end

return M
