-- mscore_contract: the ONLY place with game widget names/paths (L0 contract). Mods ask mscore_wbp by logical name.
local C = {}
C.controller_class = "DMSPlayerController"
C.window_prefix = "WBP_"
C.close_ctl, C.close_self, C.hide = "CloseWidgetOrPause", "CloseSelf", "HideWidget"
C.show_widget = "ShowWidget"          -- DMSPlayerController::ShowWidget(WidgetClass, o1, o2): open without toggling
C.player_obj_prop = "PlayerObject"    -- controller property (fallback FindFirstOf(C.classes.player_obj))
-- add-to-fleet popup recipe (captured in game): WidgetSubsystem:ShowPopup(WBP_TagSelectPopup_C, shipObject, nil, false)
C.fleet_assign = { how = 'popup', subsystem_fn = 'ShowPopup', arg = 'ship' }
C.widgets = {
  inventory_grid   = { path = "/Game/Blueprints/Widgets/Display/Inventory/WBP_Inventory",  cls = "WBP_Inventory_C" },
  item_amount      = { path = "/Game/Blueprints/Widgets/Display/Inventory/WBP_ItemAmount", cls = "WBP_ItemAmount_C" },
  fleet_ship_entry = { path = "/Game/Blueprints/Widgets/Display/WBP_FleetShipEntry",       cls = "WBP_FleetShipEntry_C" },
  pause_menu       = { path = "/Game/Blueprints/Widgets/Menus/WBP_PauseMenu",              cls = "WBP_PauseMenu_C", open_fn = "SetupWidget" },
  fleet_window     = { path = "/Game/Blueprints/Widgets/Windows/WBP_Fleet",                 cls = "WBP_Fleet_C" },
  fleet_tag_popup  = { path = "/Game/Blueprints/Widgets/Popups/WBP_TagSelectPopup",         cls = "WBP_TagSelectPopup_C" },
}
-- game class names by logical name. Mirror of kCls* in cpp/MSCore/msc_contract.hpp (tests/test_class_contract.py compares them). One entry per line.
C.classes = {
  asteroid        = "SmallResourceAsteroidObject",
  asteroid_actor  = "BP_GenericResourceAsteroid_C",
  floating_ore    = "BP_FloatingOre_C",
  station         = "StationObject",
  player_station  = "BP_StationObject_C",
  container       = "ContainerObject",
  container_actor = "BP_Container_2_C",
  item_crate      = "BP_ItemCrateObject_C",
  ship            = "DMSPlayerShipObject",
  fleet_tag       = "FleetTag",
  player_obj      = "DMSPlayerObject",
}
-- blocks module names (registry class, research class and dir, default category)
-- ship/item lookup for mscore_wbp standalone mode (a mod state loads only mscore_wbp): FindAllOf class + ship id property
C.ship_class, C.ship_id_prop, C.item_class = C.classes.ship, "ID", "Item"
-- timings (mscore_wbp): hologram picture lifetime in s, render budget (max renders per window of s), missing item icon retry in s
C.holo_ttl, C.holo_per_window, C.holo_window, C.items_refresh = 30, 2, 0.25, 30
-- hologram refresh is OFF: re-rendering an existing widget (SetShip again / ship scan for it) crashed the game in-game (2x). Pictures are created
-- once per ship per session. Do not enable without an in-game proof. holo_debug = HOLO STEP log markers; set false after the crash hunt.
C.holo_refresh = false
C.holo_debug = false
C.blocks = { registry = "BuildingBlockRegistry", research_class = "ResearchTechnology", research_dir = "/Game/Data/Research/", category = "Functional Blocks" }
return C
