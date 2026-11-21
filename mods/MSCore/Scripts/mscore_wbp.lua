-- mscore_wbp: helpers for cooked UMG windows (WBP) opened from Lua. Extracted from BlueprintUnitUI (bpu_window.lua).
-- API: M.open(path_or_class, owner) -> handle {wbp=, pc=} | nil
--      M.item_list() -> {{key=,name=},...} sorted by name | nil, why   (see item icons block)
--      M.close(handle)                       idempotent, teardown order: CleanupInventory, RemoveFromParent, input restore
--      M.inventory_grid(handle, host, inv, other_inv, columns) -> WBP_Inventory grid | nil
--      M.cost_row(host, item, need, have, opts) -> WBP_ItemAmount row | nil   (opts.refund, opts.pc)
--      M.fmt_int(n) -> "1,595"; M.geometry_log(widget); M.Z; M.install(MSCore) -> MSCore.wbp = M
--      M.open_fleets_window() -> true | false, why        the game's own fleet window (ShowWidget(WBP_Fleet, PlayerObject))
--      M.open_fleet_assign(shipId) -> true | false, why   add-to-fleet popup (ShowPopup(cls, ship, nil, false))
--      M.fleet_assign_caps() -> 'ok' | 'off:<why>';  M.probe_fleet_popup() dev capture: logs PROBE lines to UE4SS.log, sends nothing
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
local okC, C = pcall(require, "mscore_contract")
local CW = okC and C.widgets or {}
local state = { off = nil, fail = {}, cache = {} }
local function compat(m) print("[MSCore] COMPAT CAPS widgets=" .. m .. "\n") end
function M.disable(why)
  if state.off then return end
  state.off = tostring(why or "disabled"); compat("off:" .. state.off .. " (runtime)")
end
-- pcall wrapper for the contract API: a throw switches the widgets module off (R5), never propagates
local function gd(tag, f)
  return function(...)
    if state.off then return nil, "off:" .. state.off end
    local r = table.pack(pcall(f, ...))
    if not r[1] then M.disable("error in " .. tag .. ": " .. tostring(r[2])); return nil, "off:" .. state.off end
    return table.unpack(r, 2, r.n)
  end
end

local LIB = "/Script/UMG.Default__WidgetBlueprintLibrary"
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
  for _, c in ipairs(FindAllOf(okC and C.controller_class or "") or {}) do
    if ok(c) then
      local sl, loc = pcall(function() return c:IsLocalPlayerController() end)
      if sl and loc then return c end
    end
  end
  return nil
end

-- game's own close first, then widget CloseSelf, then HideWidget. returns how | nil, why
M.close_pause = gd("close_pause", function(w)
  local p = localCtl()
  if ok(p) and pcall(function() p[C.close_ctl](p) end) then return C.close_ctl end
  if ok(w) and pcall(function() w[C.close_self](w) end) then return C.close_self end
  if ok(p) and ok(w) and pcall(function() p[C.hide](p, w, false) end) then return C.hide end
  return nil, "all close paths failed"
end)

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

local function resolve_impl(name)
  local e = CW[name]
  if not e then return nil, "unknown widget " .. tostring(name) end
  local c = state.cache[name]
  if ok(c) then return c end
  c = StaticFindObject(e.path .. "." .. e.cls)
  if not ok(c) then c = getAsset(e.path, e.cls) end
  if not ok(c) then local s, f = pcall(FindObject, "WidgetBlueprintGeneratedClass", e.cls); if s then c = f end end
  if not ok(c) then
    local s, i = pcall(FindFirstOf, e.cls)
    if s and ok(i) then local s2, k = pcall(function() return i:GetClass() end); if s2 then c = k end end
  end
  if ok(c) then state.cache[name] = c; state.fail[name] = nil; return c end
  state.fail[name] = "not found"
  return nil, "not found"
end
M.widget = gd("widget", resolve_impl)
M.instance = gd("instance", function(name)
  local e = CW[name]; if not e then return nil end
  local i = FindFirstOf(e.cls); return ok(i) and i or nil
end)
M.is_window = gd("is_window", function(o)
  if not ok(o) then return false end
  local cn = o:GetClass():GetFName():ToString()
  return cn:sub(-2) == "_C" and cn ~= "WidgetBlueprintGeneratedClass" and nm(o):find("^" .. ((C and C.window_prefix) or "\0")) ~= nil
end)
-- ---------------------------------------------------------------------------------------------
-- ship hologram: the game's WBP_FleetShipEntry renders a ship into e.ShipHologram; we hand out that texture.
-- Own kill switch (hologram), separate from widgets. Warp safety: no build within 10 s of a game stall (FindAllOf / Create
-- crash after a warp or load), max C.holo_per_window renders per C.holo_window s (FIFO queue), one entry per ship id, entries are never destroyed (reference dropped).
-- ---------------------------------------------------------------------------------------------
-- Standalone (mod state, only mscore_wbp loaded, holo.core == nil): ships come from an own FindAllOf cache (max one per 5 s);
-- no tick event exists there, so a gap > 8 s between calls (caller repaints ~1/s while pending) counts as a new open / stall => 3 s settle.
local holo = { off = nil, ent = {}, miss = {}, hold = 0, buildAt = -math.huge, tickAt = nil, pc = nil, core = nil, shipAt = -math.huge, shipMap = {}, lastCall = nil }
-- STEP markers for the crash hunt (C.holo_debug, read live); never on the cheap reuse path
local function hstep(msg)
  if not (C and C.holo_debug) then return end
  pcall(print, "[MSCore] HOLO STEP " .. tostring(msg) .. "\n")
end
M._hstep = hstep
M.refresh_enabled = (C and C.holo_refresh) == true   -- documented OFF constant; tests may toggle
holo.gone = {}   -- standalone: consecutive successful scans a ship id with a widget was absent from
-- shared stall guard (ships + items): standalone has no tick, so a gap > 8 s between calls = new open / stall => 3 s settle.
-- returns true while FindAllOf / Create must not run. Every call counts as activity.
local function stalled(now)
  if not holo.core and (not holo.lastCall or now - holo.lastCall > 8) then holo.hold = now + 3 end
  holo.lastCall = now
  return now < holo.hold
end
local function standaloneShips()
  local now = os.clock()
  if now - holo.shipAt >= 5 and now >= holo.hold then
    holo.shipAt = now
    local map, n = {}, 0
    hstep("ship scan FindAllOf id=" .. tostring(holo.cur))
    local s, l = pcall(FindAllOf, C and C.ship_class or "")
    if s and l then
      for _, o in ipairs(l) do
        if live(o) then local id = rd(o, C.ship_id_prop); if type(id) == "number" then map[id] = o; n = n + 1 end end
      end
    end
    if n > 0 then   -- failed / empty scan: keep the last good map and every widget
      holo.shipMap = map
      for k in pairs(holo.ent) do
        if map[k] then holo.gone[k] = nil
        else
          holo.gone[k] = (holo.gone[k] or 0) + 1
          if holo.gone[k] >= 2 then holo.ent[k], holo.at[k], holo.gone[k] = nil, nil, nil end   -- drop the reference, no Destroy
        end
      end
    end
  end
  return holo.shipMap
end
local function holoTex(e)
  local s, t = pcall(function() return e.ShipHologram.Brush.ResourceObject end)
  if s and live(t) then return t end   -- re-validated on every read: never hand out a texture that is invalid right now
  return nil
end
function M.hologram_disable(why)
  if holo.off then return end
  holo.off = tostring(why or "disabled"); compat("hologram=off:" .. holo.off .. " (runtime)")
end
-- timings live in mscore_contract (C.holo_ttl / holo_per_window / holo_window); these are only fallbacks
local HOLO_TTL, HOLO_MAX, HOLO_WIN = (C and C.holo_ttl) or 30, (C and C.holo_per_window) or 2, (C and C.holo_window) or 0.25
M.HOLO_TTL = HOLO_TTL
holo.at, holo.queue, holo.winAt, holo.used, holo.qat = {}, {}, -math.huge, 0, {}    -- render time per id, FIFO of ids waiting for budget, window start, renders in window
holo.stats = { renders = 0, reuses = 0, created = 0, queued = 0, stale_reuses = 0 }
function M.hologram_stats() local s = holo.stats; return { renders = s.renders, reuses = s.reuses, created = s.created, queued = s.queued, stale_reuses = s.stale_reuses } end
-- pure: is a picture due (never rendered = age nil, force, or age >= ttl)
function M.holo_due(age, ttl, force) return force == true or age == nil or age >= (ttl or HOLO_TTL) end
-- pure: what to do with a ship's picture. 'create' = never rendered, or the cached texture is INVALID (drop that widget, build a NEW one);
-- 'reuse' = valid texture, return it as is. 'render' (SetShip again on the existing widget) only when M.refresh_enabled (C.holo_refresh,
-- OFF: it crashed the game) AND refresh_ok AND due. ttl / force / refresh_ok are otherwise accepted and ignored.
function M.holo_decide(age, ttl, force, refresh_ok, hasValidTex)
  if age == nil or not hasValidTex then return "create" end
  if M.refresh_enabled and refresh_ok == true and M.holo_due(age, ttl, force) then return "render" end
  return "reuse"
end
-- pure: drop queued ids that are not alive or were queued longer than maxAge s ago (qat[id] = enqueue time)
function M.holo_purge(queue, alive, qat, now, maxAge)
  local q = {}
  for _, v in ipairs(queue) do
    if alive[v] and now - (qat[v] or now) <= maxAge then q[#q + 1] = v end
  end
  return q
end
function M.holo_take(queue, id, used, max)
  local q, has = {}, false
  for _, v in ipairs(queue) do q[#q + 1] = v; if v == id then has = true end end
  if not has then q[#q + 1] = id end
  for i = 1, math.min(max - used, #q) do
    if q[i] == id then table.remove(q, i); return true, q end
  end
  return false, q
end
-- drop every Lua reference to cached hologram widgets (no Destroy, nothing is called on them)
local function releaseRefs() holo.ent, holo.at, holo.miss, holo.queue, holo.qat, holo.gone = {}, {}, {}, {}, {}, {} end
-- hologram_release(host): call on window close AND at the start of every open (a fresh open creates new widgets, never SetShip on an old one).
-- RemoveFromParent is called on the cached widgets only while `host` is the live host they were parented to; otherwise references are just dropped.
function M.hologram_release(host)
  local n = 0
  for _ in pairs(holo.ent) do n = n + 1 end
  hstep("release n=" .. n)
  pcall(function()
    if holo.hostId and ok(host) and cie_id(host) == holo.hostId then
      for _, e in pairs(holo.ent) do pcall(function() e:RemoveFromParent() end) end
    end
  end)
  releaseRefs()
  holo.host, holo.hostId = nil, nil
end
local function hologram_texture_impl(id, opts)
  if holo.off then return nil, "off:" .. holo.off end
  if type(id) ~= "number" then return nil, "bad id" end
  opts = opts or {}
  holo.cur = id
  -- GC safety: widgets are parented to the consumer's persistent hidden host (its window tree keeps them and their textures alive).
  -- No host = no hologram (never create unrooted widgets). A different host = window reopened: every cached widget is released.
  local host = opts.host
  if not ok(host) then return nil, "no host" end
  local hid = cie_id(host)
  if holo.hostId ~= hid then
    if holo.hostId then releaseRefs() end
    holo.host, holo.hostId = host, hid
  end
  local now = os.clock()
  stalled(now)
  local e, at = holo.ent[id], holo.at[id]
  local tex = e and holoTex(e) or nil
  local dec = M.holo_decide(at and now - at or nil, opts.ttl, opts.force, opts.refresh_ok, tex ~= nil)
  if tex and dec == "reuse" then   -- no ship scan / SetShip / FindAllOf on this path
    holo.stats.reuses = holo.stats.reuses + 1
    if M.holo_due(at and now - at or nil, opts.ttl, false) then holo.stats.stale_reuses = holo.stats.stale_reuses + 1 end
    return tex
  end
  if e and at and not tex then   -- texture missing or invalid right now: just built = wait, else drop the widget and build a new one (below)
    if now - at <= 5 then return nil, "pending" end
  end
  if now < holo.hold or (holo.miss[id] or 0) > now then return nil, "pending" end
  local alive, ship = {}, nil
  if holo.core and holo.core.ships then
    for _, s in ipairs(holo.core.ships.list()) do
      alive[s.id] = true
      if s.id == id then ship = s.obj end
    end
  else
    for k, o in pairs(standaloneShips()) do
      alive[k] = true
      if k == id then ship = o end
    end
  end
  if holo.core and next(alive) ~= nil then   -- (standalone prunes inside standaloneShips)
    for k in pairs(holo.ent) do if not alive[k] then holo.ent[k] = nil; holo.at[k] = nil end end   -- drop the reference, no Destroy
  end
  holo.queue = M.holo_purge(holo.queue, alive, holo.qat, now, 10)
  if not ok(ship) then holo.miss[id] = now + 5; return nil, "no ship" end
  if e and dec == "create" then   -- invalid cached texture: never SetShip a second time on an old widget
    hstep("drop widget id=" .. id)
    holo.ent[id], holo.at[id], e = nil, nil, nil   -- drop the reference, no Destroy
  end
  if not e then
    hstep("resolve class id=" .. id)
    local cls = M.widget("fleet_ship_entry")
    if not ok(cls) then return nil, "no class" end
    hstep("localCtl id=" .. id)
    if not ok(holo.pc) then holo.pc = localCtl() end
    if not ok(holo.pc) then return nil, "no controller" end
  end
  if now - holo.winAt >= HOLO_WIN then holo.winAt, holo.used = now, 0 end
  local was = #holo.queue
  local allowed, q = M.holo_take(holo.queue, id, holo.used, HOLO_MAX)
  holo.queue = q
  if not allowed then
    if #q > was then holo.stats.queued = holo.stats.queued + 1; holo.qat[id] = now end
    return nil, "pending"
  end
  holo.used = holo.used + 1
  if not e then
    hstep("Create id=" .. id)
    try("holo create", function() e = StaticFindObject(LIB):Create(holo.pc, M.widget("fleet_ship_entry"), holo.pc) end)
    if not ok(e) then holo.miss[id] = now + 5; return nil, "pending" end
    hstep("host AddChild id=" .. id)
    local rooted = pcall(function() host:AddChild(e) end)
    if not rooted then holo.miss[id] = now + 5; return nil, "pending" end   -- never keep an unrooted widget
    pcall(function() e:SetVisibility(1) end)   -- Collapsed
    holo.ent[id] = e
    holo.stats.created = holo.stats.created + 1
  end
  -- SetShip runs only on a widget created in this call (or when refresh is enabled, dec == "render")
  hstep("SetShip id=" .. id)
  try("holo SetShip", function() e:SetShip(ship) end)
  holo.at[id] = now
  holo.stats.renders = holo.stats.renders + 1
  hstep("read ShipHologram texture id=" .. id)
  tex = holoTex(e)
  if tex then return tex end
  return nil, "pending"
end
local function hg(f)
  return function(...)
    if holo.off then return nil, "off:" .. holo.off end
    local r = table.pack(pcall(f, ...))
    if not r[1] then M.hologram_disable("error in hologram: " .. tostring(r[2])); return nil, "off:" .. holo.off end
    return table.unpack(r, 2, r.n)
  end
end
-- WBP.ship_hologram(shipId [, {host = panel}]) -> texture UObject | nil, why   (primary; hologram_texture = alias)
-- HOST: opts.host = a persistent hidden panel of the consumer's window; widgets are AddChild'ed to it (GC-rooted, Collapsed). No host = nil,'no host'.
-- Call hologram_release(host) on window close and at every open.
-- REFRESH IS OFF (C.holo_refresh=false): re-rendering an existing widget crashed the game. A picture is created once per ship per session;
-- opts.ttl / force / refresh_ok are accepted and ignored. An INVALID cached texture drops that widget and builds a NEW one (queue/budget).
-- OWNERSHIP / LIFETIME: the mod owns the picture. APPLY the returned texture to YOUR Image brush IMMEDIATELY, in the same call
-- (SetBrushFromTexture / brush.ResourceObject, under pcall + IsValid). The Slate brush then holds a strong GC reference, so the picture
-- stays alive even if MSCore drops its hidden widget; an MSCore refresh redraws INTO the same render target (SetShip again), so images
-- already applied update in place. Do not keep the Lua variable across ticks; ask again on every paint. set_ship_hologram(img, id) /
-- set_item_icon(img, key) do exactly that in one call (use them when you do not need the texture).
-- Fallback: if in-game testing shows flicker or a dead picture on refresh, the fallback is a pixel-copy into the caller's own render
-- target (new render target + draw per ship): heavy, not implemented. Not yet tested in game.
M.ship_hologram = hg(hologram_texture_impl)
M.hologram_texture = M.ship_hologram
M._holoTex = holoTex   -- test hook
-- applies right where it gets the texture, in the same call, under pcall (no texture is kept)
function M.set_ship_hologram(img, id, opts)
  if not live(img) then return false, "bad image" end
  local tex, why = M.ship_hologram(id, opts)
  if not tex then return false, why end
  if not pcall(function() img:SetBrushFromTexture(tex, true) end) then return false, "SetBrush failed" end
  return true
end
function M.hologram_caps()
  if holo.off then return "off:" .. holo.off end
  local n = 0
  for _ in pairs(holo.ent) do n = n + 1 end
  return "ok n=" .. n
end
local function holoTick()
  local now = os.clock()
  if holo.tickAt and now - holo.tickAt > 1.5 then holo.hold = now + 10 end   -- game stall = warp / load
  holo.tickAt = now
end

-- ---------------------------------------------------------------------------------------------
-- item icons (works with mscore_wbp alone): item_icon(key) -> texture | nil, why ; set_item_icon(img, key) -> true | false, why.
-- key = item object name (e.g. ITEM_TitaniumOre). One FindAllOf builds the name map; textures are kept for the session, a missing key refreshes the map at most every C.items_refresh (30) s.
-- ---------------------------------------------------------------------------------------------
local ITEMS_REFRESH = (C and C.items_refresh) or 30
local items = { off = nil, map = nil, at = -math.huge, tex = {}, list = nil, listMap = nil }
local function itemMap(force)
  if (force or (not items.map and os.clock() - items.at >= ITEMS_REFRESH)) and not stalled(os.clock()) then   -- stall guard: no FindAllOf, keep the cached map (may be nil); an empty map is never kept, retry at most every ITEMS_REFRESH s
    hstep("items FindAllOf Item map")
    local s, all = pcall(FindAllOf, C and C.item_class or "")
    local map = {}
    if s and all then
      for _, it in ipairs(all) do
        if live(it) then
          local n = nm(it)
          if n ~= "?" and not n:find("Default__", 1, true) then map[n] = it end
        end
      end
    end
    items.map, items.at = next(map) and map or nil, os.clock()
  end
  return items.map
end
function M.items_disable(why)
  items.off = tostring(why or "disabled")
  print("[MSCore] COMPAT CAPS items_icon=off:" .. items.off .. " (runtime)\n")
end
function M.items_caps() return items.off and ("off:" .. items.off) or "ok" end
function M.item_icon(key)
  if items.off then return nil, "off:" .. items.off end
  local s, tex, why = pcall(function()
    if live(items.tex[key]) then return items.tex[key] end
    local m = itemMap(false)
    if not m then return nil, "pending" end
    local it = m[key]
    if not it and os.clock() - items.at >= ITEMS_REFRESH then it = (itemMap(true) or m)[key] end
    if not it then return nil, "no item " .. tostring(key) end
    hstep("items read Icon key=" .. tostring(key))
    local t = rd(it, "Icon")
    if live(t) then items.tex[key] = t; return t end
    return nil, "no icon"
  end)
  if not s then M.items_disable("error: " .. tostring(tex)) return nil, "off:" .. items.off end
  return tex, why
end
-- item_list() -> array of {key=, name=} sorted by name (lower-case), or nil, why. Built from itemMap (no extra FindAllOf), cached until the map is replaced.
function M.item_list()
  if items.off then return nil, "off:" .. items.off end
  local s, r, why = pcall(function()
    local m = itemMap(false)
    if not m then return nil, "pending" end
    if items.list and items.listMap == m then return items.list end
    hstep("items read DisplayName list")
    local out = {}
    for key, it in pairs(m) do
      if not key:find("Filter", 1, true) then
        local sn, label = pcall(function() return it.DisplayName:ToString() end)
        if not (sn and type(label) == "string" and label ~= "") then label = (key:gsub("^ITEM_", "")) end
        out[#out + 1] = { key = key, name = label }
      end
    end
    table.sort(out, function(a, b) return a.name:lower() < b.name:lower() end)
    items.list, items.listMap = out, m
    return out
  end)
  if not s then M.items_disable("error: " .. tostring(r)) return nil, "off:" .. items.off end
  return r, why
end
-- item_icon(key) -> texture | nil, why: same ownership/lifetime contract as ship_hologram (apply it to your brush immediately, ask again per paint)
function M.set_item_icon(img, key)
  if not live(img) then return false, "bad image" end
  local t, why = M.item_icon(key)
  if not t then return false, why end
  if not pcall(function() img:SetBrushFromTexture(t, true) end) then return false, "SetBrush failed" end
  return true
end

-- cb(self) runs on the game thread after the widget's open function. true = hooked now or armed for retry
M.on_open = gd("on_open", function(name, cb)
  local e = CW[name]
  if not e or not e.open_fn then return false end
  local full = e.path .. "." .. e.cls
  local s = pcall(RegisterHook, full .. ":" .. e.open_fn, function() end, function(self) pcall(cb, self) end)
  if s then return true end
  local armed = false
  for _, n in ipairs({ full, e.cls }) do
    if pcall(NotifyOnNewObject, n, function() pcall(ExecuteInGameThread, function() M.on_open(name, cb) end) end) then armed = true end
  end
  L("COMPAT hook " .. name .. (armed and " armed (class not loaded yet)" or " off"))
  return armed
end)
function M.caps()
  if state.off then return "off:" .. state.off end
  local bad = {}
  for n in pairs(state.fail) do bad[#bad + 1] = n end
  table.sort(bad)
  if #bad > 0 then return "off:unresolved " .. table.concat(bad, ",") end
  return "ok"
end
-- read-only: resolve every contract widget, print the COMPAT line. allok, detail
function M.selftest()
  if state.off then return false, "killed: " .. state.off end
  local names = {}
  for n in pairs(CW) do names[#names + 1] = n end
  table.sort(names)
  local bad = {}
  for _, n in ipairs(names) do local c = M.widget(n); if not c then bad[#bad + 1] = n end end
  compat(M.caps())
  if #bad == 0 then return true, #names .. " widgets resolved" end
  return false, "unresolved: " .. table.concat(bad, ",")
end

-- ---------------------------------------------------------------------------------------------
-- game fleet UI
-- ---------------------------------------------------------------------------------------------
M.open_fleets_window = gd("open_fleets_window", function()
  local cls, why = M.widget("fleet_window")
  if not cls then return false, why or "no class" end
  local p = localCtl()
  if not ok(p) then return false, "no controller" end
  local po = rd(p, C.player_obj_prop)
  if not ok(po) then local s, o = pcall(FindFirstOf, C.classes.player_obj); po = s and o or nil end
  if not ok(po) then return false, "no player object" end
  if not pcall(function() p[C.show_widget](p, cls, po, nil) end) then return false, "ShowWidget failed" end
  return true
end)

local function fleetWinInst()   -- live instance or nil; never keep the handle
  local i = M.instance("fleet_window")
  if not ok(i) then return nil end
  local s, vp = pcall(function() return i:IsInViewport() end)
  local sv, vis = pcall(function() return i:IsVisible() end)
  if s and vp == true and (not sv or vis ~= false) then return i end   -- cached hidden WBP_Fleet_C stays in viewport
  return nil
end
M.fleets_window_open = gd("fleets_window_open", function() return fleetWinInst() ~= nil end)
M.close_fleets_window = gd("close_fleets_window", function()
  local w = fleetWinInst(); if not w then return true end
  local p = localCtl()
  if pcall(function() w[C.close_self](w) end) then L("[MSCore.wbp] close_fleets_window via CloseSelf"); return true end
  if ok(p) and pcall(function() p[C.hide](p, w, false) end) then L("[MSCore.wbp] close_fleets_window via HideWidget"); return true end
  return false, "close failed"
end)

-- popup class: contract widget first, else a loaded WidgetBlueprintGeneratedClass named *TagSelect* / *FleetTag* (scanned once, cached, logged)
local popupCand
function M.fleet_popup_candidates()
  if popupCand then return popupCand end
  local out = {}
  local s, l = pcall(FindAllOf, "WidgetBlueprintGeneratedClass")
  if s and l then
    for _, c in ipairs(l) do
      if live(c) then
        local sn, n = pcall(function() return c:GetFName():ToString() end)
        if sn and type(n) == "string" and (n:find("TagSelect", 1, true) or n:find("FleetTag", 1, true)) then out[#out + 1] = { name = n, cls = c } end
      end
    end
    popupCand = out
    local names = {}
    for i, c in ipairs(out) do names[i] = c.name end
    L("[MSCore.wbp] fleet popup candidates: " .. (#names > 0 and table.concat(names, ", ") or "none"))
  end
  return out
end
local function popupClass()
  local c = M.widget("fleet_tag_popup")
  if ok(c) then return c end
  local best
  for _, e in ipairs(M.fleet_popup_candidates()) do
    if live(e.cls) and (not best or (e.name:find("TagSelect", 1, true) and not best.name:find("TagSelect", 1, true))) then best = e end
  end
  return best and best.cls or nil
end
local function widgetSub()
  local s, o = pcall(FindFirstOf, "WidgetSubsystem")   -- live instance, never the CDO
  if s and ok(o) then return o end
  return nil
end
local function clsName(c)
  local s, v = pcall(function() return c:GetFName():ToString() end)
  return s and tostring(v) or "?"
end

-- recipe C.fleet_assign (captured in game): WidgetSubsystem:ShowPopup(popupClass, shipObject, nil, false). Do not call within ~10 s after a warp.
M.open_fleet_assign = gd("open_fleet_assign", function(shipId)
  local R = C.fleet_assign
  if not R then return false, "no recipe" end
  if type(shipId) ~= "number" then return false, "bad ship id" end
  local cls = popupClass()
  if not cls then return false, "no class" end
  local ship
  if holo.core and holo.core.ships then
    for _, s in ipairs(holo.core.ships.list()) do if s.id == shipId then ship = s.obj end end
  else
    ship = standaloneShips()[shipId]
    if not ok(ship) then   -- first click right after the window opened: scan throttle/settle hold; a user click may scan now (not during a warp/stall hold)
      holo.shipAt = -math.huge
      if holo.hold - os.clock() <= 3 then holo.hold = 0 end
      ship = standaloneShips()[shipId]
    end
  end
  if not ok(ship) then return false, "no ship" end
  local sub = widgetSub()
  if not sub then return false, "no widget subsystem" end
  if not (ok(ship) and ok(sub) and ok(cls)) then return false, "stale object" end   -- re-validate right before the call
  local s = pcall(function() sub[R.subsystem_fn](sub, cls, ship, nil, false) end)
  L("[MSCore.wbp] open_fleet_assign ship=" .. shipId .. " class=" .. clsName(cls) .. " ok=" .. tostring(s))
  if not s then return false, "popup failed" end
  return true
end)

function M.fleet_assign_caps()
  if not C.fleet_assign then return "off:no recipe" end
  local s, r = pcall(function()
    if not popupClass() then return "off:no class" end
    if not widgetSub() then return "off:no widget subsystem" end
    return "ok"
  end)
  return s and r or "off:error"
end

-- dev capture only, logs and sends nothing: ShowPopup events log the widget class's OWN name + o1/o2 classes.
-- Do not call within ~10 s after a warp (same as the hologram code).
local probeArmed, tagHooked = false, false
function M.probe_fleet_popup()
  if probeArmed then return end
  probeArmed = true
  local function cn(o)   -- a null object param still arrives as a wrapper: never call GetClass without IsValid
    local okv, valid = pcall(function() return o:get():IsValid() end)
    if not (okv and valid) then return "nil" end
    local s, v = pcall(function() return o:get():GetClass():GetFName():ToString() end)
    return s and v or "?"
  end
  local function wn(o)   -- the widget class object itself
    local okv, valid = pcall(function() return o:get():IsValid() end)
    if not (okv and valid) then return "nil" end
    local s, n = pcall(function() return o:get():GetFName():ToString() end)
    local s2, f = pcall(function() return o:get():GetFullName() end)
    return (s and n or "?") .. " full=" .. (s2 and tostring(f) or "?")
  end
  local function hookTag()
    if tagHooked then return end
    if pcall(RegisterHook, "/Game/Blueprints/Widgets/Popups/WBP_TagSelectPopup.WBP_TagSelectPopup_C:SetupWidget", function(self, o1, o2)
      L("PROBE TagSelect SetupWidget o1=" .. cn(o1) .. " o2=" .. cn(o2))
    end, function() end) then
      tagHooked = true
      L("PROBE hooked TagSelect SetupWidget")
    end
  end
  pcall(RegisterHook, "/Script/MainSequence.WidgetSubsystem:ShowPopup", function(self, wc, o1, o2)
    L("PROBE ShowPopup widget=" .. wn(wc) .. " o1=" .. cn(o1) .. " o2=" .. cn(o2))
    hookTag()   -- class may be loaded by now
  end, function() end)
  hookTag()
  L("PROBE armed: in the game open the add-to-fleet UI once, then read UE4SS.log lines PROBE")
end
-- dev: arm the capture at load when Mods/MSCore/probe_fleet_popup.flag exists (delete the flag to stop)
do
  local src = debug.getinfo(1, "S").source:match("^@(.*[/\\])")
  local fh = src and io.open(src .. "../probe_fleet_popup.flag", "r")
  -- arm in ONE Lua state only (every mod loads this file): the first state renames the flag, later states see none
  if fh then fh:close(); if os.rename(src .. "../probe_fleet_popup.flag", src .. "../probe_fleet_popup.flag.armed") then pcall(M.probe_fleet_popup) end end
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
local function inventory_grid_impl(h, host, inv, other, columns)
  if type(h) ~= "table" or not ok(h.wbp) then L("inventory_grid: window closed"); return nil end
  local invCls = M.widget("inventory_grid")
  if not ok(invCls) then L("inventory_grid: widget class not found"); return nil end
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

local function cost_row_impl(host, item, need, have, opts)
  opts = opts or {}
  local amtCls = M.widget("item_amount")
  if not ok(amtCls) then L("cost_row: widget class not found"); return nil end
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

function M.install(MSCore)
  MSCore.wbp = M
  holo.core = MSCore
  if MSCore.events and MSCore.events.on then pcall(MSCore.events.on, "tick", holoTick) end
  return M
end

return M
