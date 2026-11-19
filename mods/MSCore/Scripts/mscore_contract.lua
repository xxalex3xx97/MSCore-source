-- mscore_contract: the ONLY place with game widget names/paths (L0 contract). Mods ask mscore_wbp by logical name.
local C = {}
C.controller_class = "DMSPlayerController"
C.window_prefix = "WBP_"
C.close_ctl, C.close_self, C.hide = "CloseWidgetOrPause", "CloseSelf", "HideWidget"
C.widgets = {
  inventory_grid   = { path = "/Game/Blueprints/Widgets/Display/Inventory/WBP_Inventory",  cls = "WBP_Inventory_C" },
  item_amount      = { path = "/Game/Blueprints/Widgets/Display/Inventory/WBP_ItemAmount", cls = "WBP_ItemAmount_C" },
  fleet_ship_entry = { path = "/Game/Blueprints/Widgets/Display/WBP_FleetShipEntry",       cls = "WBP_FleetShipEntry_C" },
  pause_menu       = { path = "/Game/Blueprints/Widgets/Menus/WBP_PauseMenu",              cls = "WBP_PauseMenu_C", open_fn = "SetupWidget" },
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
}
-- blocks module names (registry class, research class and dir, default category)
C.blocks = { registry = "BuildingBlockRegistry", research_class = "ResearchTechnology", research_dir = "/Game/Data/Research/", category = "Functional Blocks" }
return C
