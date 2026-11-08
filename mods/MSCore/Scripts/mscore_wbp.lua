-- mscore_wbp: helpers for cooked UMG windows (WBP) opened from Lua. Extracted from BlueprintUnitUI (bpu_window.lua).
-- API: M.open(path_or_class, owner) -> handle {wbp=, pc=} | nil
--      M.close(handle)                       idempotent, teardown order: CleanupInventory, RemoveFromParent, input restore
--      M.inventory_grid(handle, host, inv, other_inv, columns) -> WBP_Inventory grid | nil
--      M.cost_row(host, item, need, have, opts) -> WBP_ItemAmount row | nil   (opts.refund, opts.pc)
--      M.fmt_int(n) -> "1,595"; M.geometry_log(widget); M.Z; M.install(MSCore) -> MSCore.wbp = M
-- Call open / inventory_grid / cost_row on the game thread (ExecuteInGameThread). Every game call goes through pcall.
-- Camera: open sets controller CharacterInputEnabled=false (AllowCharacterInput), close restores it only if it was true.
-- Window rules (proven in BlueprintUnitUI): Z below 2 (WBP_CursorItemStack draws held items at Z 2), GameAndUIEx input
-- (UIOnly breaks PC:IsInputKeyDown(Shift) = shift-click quick move), sizing lives in the WBP (Lua size calls are ignored).

local M = {}
local TAG = "[MSCore.wbp] "
local function L(m) print(TAG .. m .. "\n") end
local function ok(o) if o == nil or type(o) ~= "userdata" then return false end local s, v = pcall(function() return o:IsValid() end) return s and v == true end
local function nm(o) if not ok(o) then return "?" end local s, v = pcall(function() return o:GetFName():ToString() end) return s and v or "?" end
local function rd(o, p) local s, v = pcall(function() return o[p] end) if s then return v end return nil end
local function try(tag, f, ...) local s, r = pcall(f, ...) if not s then L("FAIL " .. tag .. ": " .. tostring(r)) end return s end
local function inVp(o) local s, v = pcall(function() return o:IsInViewport() end) return s and v == true end
local function live(o) if o == nil then return false end local s, v = pcall(function() return o:IsValid() end) return s and v == true end   -- no userdata check (fakes in tests)
local function guard(tag, f) return function(...) local s, r = pcall(f, ...) if not s then L(tag .. " FAIL: " .. tostring(r)) return nil end return r end end

local LIB = "/Script/UMG.Default__WidgetBlueprintLibrary"
local INV_PKG = "/Game/Blueprints/Widgets/Display/Inventory/WBP_Inventory"
local INV_CLS = INV_PKG .. ".WBP_Inventory_C"
local AMT_PKG = "/Game/Blueprints/Widgets/Display/Inventory/WBP_ItemAmount"   -- game's cost row (WBP_ItemTotals rows)
local AMT_CLS = AMT_PKG .. ".WBP_ItemAmount_C"
local COST_RED = { R = 0.705208, G = 0.031587, B = 0.031587, A = 1 }          -- game's own
local COST_WHITE = { R = 1, G = 1, B = 1, A = 1 }
local COST_GREEN = { R = 0.1, G = 0.6, B = 0.2, A = 1 }

M.Z = 1          -- window Z. Must stay below 2: the game's WBP_CursorItemStack (AddToViewport 2) draws over us during drag.
M._opens = 0     -- open counter: input is restored on close only if no window opened since
M._cie = { holders = {}, prev = nil, pc = nil }   -- CharacterInputEnabled block: IA_Mouse look is gated on it (rea-window-camera-drag.md)
-- stable identity of a controller: the same UObject can arrive as different Lua wrappers (table/userdata)
local function cie_id(p)
  local s, v = pcall(function() return p:GetAddress() end)
  if s and v ~= nil then return tostring(v) end
  s, v = pcall(function() return p:GetFullName() end)
  if s and v ~= nil then return tostring(v) end
  return tostring(p)
end
function M._cie_acquire(h, p)
  local c = M._cie
  local id = cie_id(p)
  local alive = 0
  for k in pairs(c.holders) do if live(k.wbp) then alive = alive + 1 else c.holders[k] = nil end end   -- window destroyed without close (death/load): drop it
  -- re-read prev only for a new controller: a dead holder may have left CIE=false, re-reading would lose the real prev (stays frozen)
  if alive == 0 and c.pc ~= id then
    local s, v = pcall(function() return p.CharacterInputEnabled end)
    if s and type(v) == "boolean" then c.prev = v else c.prev = nil end   -- nil = unreadable (unknown): release restores true
  end
  c.pc = id
  c.holders[h] = true
  if not pcall(function() p:AllowCharacterInput(false) end) then try("CIE prop", function() p.CharacterInputEnabled = false end) end
end
function M._cie_release(h, p)
  local c = M._cie
  if not c.holders[h] then return end
  c.holders[h] = nil
  for k in pairs(c.holders) do if live(k.wbp) then return end c.holders[k] = nil end   -- another live window still open: keep blocked
  if c.prev ~= false and live(p) then   -- restore unless the value was read as false (unknown = restore)
    if not pcall(function() p:AllowCharacterInput(true) end) then try("CIE prop restore", function() p.CharacterInputEnabled = true end) end
  end
  c.prev, c.pc = nil, nil
end

-- AssetRegistryHelpers fallback for LoadAsset-gated paths (FAssetData table). Game thread only.
local ARH
local function getAsset(pkg, name)
  if not ok(ARH) then ARH = StaticFindObject("/Script/AssetRegistry.Default__AssetRegistryHelpers") end
  if not ok(ARH) then L("ARH invalid"); return nil end
  local sf, r = pcall(function() return ARH:GetAsset({ PackageName = FName(pkg), AssetName = FName(name) }) end)
  L("GetAsset " .. pkg .. "." .. name .. " ok=" .. tostring(sf and ok(r)))
  if sf and ok(r) then return r end
  return nil
end

-- widget by name: GetWidgetFromName, then the is-variable property
local function widget(wbp, name)
  if not ok(wbp) then return nil end
  local s, x = pcall(function() return wbp:GetWidgetFromName(FName(name)) end)
  if s and ok(x) then return x end
  local s2, y = pcall(function() return wbp[name] end)
  if s2 and ok(y) then return y end
  return nil
end

local function localCtl()
  for _, c in ipairs(FindAllOf("DMSPlayerController") or {}) do
    if ok(c) then
      local sl, loc = pcall(function() return c:IsLocalPlayerController() end)
      if sl and loc then return c end
    end
  end
  return nil
end

-- "pkg.Name_C" (class path), or a bare package path "/Game/X/WBP_A" (-> WBP_A_C), or a UClass userdata
local function classOf(spec)
  if type(spec) == "userdata" then return ok(spec) and spec or nil end
  if type(spec) ~= "string" then return nil end
  local pkg, name = spec:match("^(.-)%.([^%.]+)$")
  if not pkg then pkg = spec; name = (spec:match("([^/]+)$") or spec) .. "_C"; spec = pkg .. "." .. name end
  local c = StaticFindObject(spec)
  if ok(c) then return c end
  return getAsset(pkg, name)
end

-- ---------------------------------------------------------------------------------------------
-- window
-- ---------------------------------------------------------------------------------------------
local function open_impl(spec, owner)
  local cls = classOf(spec)
  if not ok(cls) then L("open: class not found " .. tostring(spec)); return nil end
  local p = ok(owner) and owner or localCtl()
  if not ok(p) then L("open: no PlayerController"); return nil end
  local lib = StaticFindObject(LIB)
  if not ok(lib) then L("open: lib invalid"); return nil end
  local wbp
  try("Create", function() wbp = lib:Create(p, cls, p) end)
  if not ok(wbp) then L("open: Create failed for " .. nm(cls)); return nil end
  local h = { wbp = wbp, pc = p, grids = {} }
  try("AddToViewport", function() wbp:AddToViewport(M.Z) end)
  -- input: GameAndUIEx like the vanilla windows. UIOnly breaks shift-click quick move.
  local okIn = try("input GameAndUIEx5", function() lib:SetInputMode_GameAndUIEx(p, wbp, 0, true, false) end)
    or try("input GameAndUIEx4", function() lib:SetInputMode_GameAndUIEx(p, wbp, 0, true) end)
    or try("input UIOnlyEx4", function() lib:SetInputMode_UIOnlyEx(p, wbp, 0, false) end)
  L("input mode ok=" .. tostring(okIn))
  try("mouse on", function() p.bShowMouseCursor = true end)
  try("focusable", function() wbp.bIsFocusable = true end)
  try("user focus", function() wbp:SetUserFocus(p) end)
  try("look ignore", function() p:SetIgnoreLookInput(true) end)
  try("move ignore", function() p:SetIgnoreMoveInput(true) end)
  M._cie_acquire(h, p)
  M._opens = M._opens + 1
  L("open " .. nm(wbp) .. " n=" .. M._opens .. " inVp=" .. tostring(inVp(wbp)))
  return h
end
M.open = guard("open", open_impl)

-- idempotent. Teardown runs on a later tick (caller may be a button / key callback), as BlueprintUnitUI does.
function M.close(h)
  if type(h) ~= "table" or h.closed then return end
  h.closed = true
  local wbp, p, grids, ep = h.wbp, h.pc, h.grids or {}, M._opens
  local function teardown()
    -- MANDATORY ORDER: grid cleanup (removes delegates) first, then RemoveFromParent, then input restore
    for _, g in ipairs(grids) do if ok(g) then pcall(function() g:CleanupInventory() end) end end
    if ok(wbp) then try("RemoveFromParent", function() wbp:RemoveFromParent() end) end
    M._cie_release(h, p)
    if ok(p) and ep == M._opens then   -- no window opened since: restore input
      try("look unignore", function() p:SetIgnoreLookInput(false) end)
      try("move unignore", function() p:SetIgnoreMoveInput(false) end)
      try("mouse off", function() p.bShowMouseCursor = false end)
      try("input GameOnly", function() StaticFindObject(LIB):SetInputMode_GameOnly(p, false) end)
    end
    L("closed")
  end
  if not pcall(ExecuteWithDelay, 50, function() pcall(ExecuteInGameThread, teardown) end) then teardown() end
end

-- ---------------------------------------------------------------------------------------------
-- inventory grid (the game's WBP_Inventory). Argument order = vanilla: unit grid (inv, player), mirror (player, unit).
-- ---------------------------------------------------------------------------------------------
local invCls
local function inventory_grid_impl(h, host, inv, other, columns)
  if type(h) ~= "table" or not ok(h.wbp) then L("inventory_grid: window closed"); return nil end
  if not ok(invCls) then invCls = StaticFindObject(INV_CLS) end
  if not ok(invCls) then invCls = getAsset(INV_PKG, "WBP_Inventory_C") or StaticFindObject(INV_CLS) end
  if not ok(invCls) then L("inventory_grid: WBP_Inventory class not found"); return nil end
  local p = h.pc
  local g
  try("CreateGrid " .. host, function() g = StaticFindObject(LIB):Create(p, invCls, p) end)
  if not ok(g) then L("inventory_grid: create failed for " .. host); return nil end
  pcall(function() g.Columns = columns or 8 end)
  pcall(function() g.ShowSortButton = true end)
  local hb = widget(h.wbp, host)
  if not ok(hb) then L("inventory_grid: host " .. host .. " missing"); return nil end
  try("AddChild " .. host, function() hb:AddChild(g) end)
  if ok(inv) and ok(other) then try("SetupInventory " .. host, function() g:SetupInventory(inv, other, p) end)
  else L("inventory_grid: setup skipped " .. host .. ", inventory invalid") end
  h.grids[#h.grids + 1] = g
  return g
end
M.inventory_grid = guard("inventory_grid", inventory_grid_impl)

-- ---------------------------------------------------------------------------------------------
-- cost row (the game's WBP_ItemAmount). Need row: "have/needx" (red when have < need). opts.refund: "+N refund" (green).
-- ---------------------------------------------------------------------------------------------
function M.fmt_int(n)   -- 1595 -> "1,595", -1595 -> "-1,595"
  n = math.floor(tonumber(n) or 0)
  local s = tostring(math.abs(n)):reverse():gsub("(%d%d%d)", "%1,"):reverse()
  return (n < 0 and "-" or "") .. (s:gsub("^,", ""))
end

local amtCls
local function cost_row_impl(host, item, need, have, opts)
  opts = opts or {}
  if not ok(amtCls) then amtCls = StaticFindObject(AMT_CLS) end
  if not ok(amtCls) then amtCls = getAsset(AMT_PKG, "WBP_ItemAmount_C") or StaticFindObject(AMT_CLS) end
  if not ok(amtCls) then L("cost_row: WBP_ItemAmount class not found"); return nil end
  local p = ok(opts.pc) and opts.pc or localCtl()
  if not ok(p) then L("cost_row: no PlayerController"); return nil end
  if not ok(host) or not ok(item) then L("cost_row: host or item invalid"); return nil end
  local amount, text, col
  if opts.refund then
    amount, text, col = need, "+" .. M.fmt_int(need) .. " refund", COST_GREEN
  else
    amount = need
    text = M.fmt_int(have) .. "/" .. M.fmt_int(need) .. "x"
    col = (have < need) and COST_RED or COST_WHITE
  end
  local row
  try("cost create", function() row = StaticFindObject(LIB):Create(p, amtCls, p) end)
  if not ok(row) then return nil end
  pcall(function() row:SetItemStack({ Item = item, Amount = amount }, nil) end)
  pcall(function() row.AmountText:SetText(FText(text)) end)
  pcall(function() row.AmountText:SetColour({ SpecifiedColor = col, ColorUseRule = 0 }) end)
  pcall(function() row.WBP_Item:SetIconSize(40) end)
  if not try("cost add", function() host:AddChild(row) end) then return nil end
  return row
end
M.cost_row = guard("cost_row", cost_row_impl)

-- ---------------------------------------------------------------------------------------------
-- geometry probe: desired and cached sizes of a widget. One line.
-- ---------------------------------------------------------------------------------------------
local function v2(s, v) if s and type(v) == "userdata" then return tostring(rd(v, "X")) .. "x" .. tostring(rd(v, "Y")) end return "n/a" end
function M.geometry_log(wbp)
  if not ok(wbp) then L("geometry: widget invalid"); return end
  local sd, d = pcall(function() return wbp:GetDesiredSize() end)
  local sg, g = pcall(function() return wbp:GetCachedGeometry() end)
  local ga, gl = "n/a", "n/a"
  if sg and type(g) == "userdata" then
    local sa, a = pcall(function() return g:GetAbsoluteSize() end); ga = v2(sa, a)
    local sl, l = pcall(function() return g:GetLocalSize() end); gl = v2(sl, l)
  end
  L("geometry desired=" .. v2(sd, d) .. " cachedAbs=" .. ga .. " cachedLocal=" .. gl .. " inVp=" .. tostring(inVp(wbp)))
end

function M.install(MSCore) MSCore.wbp = M; return M end

return M
