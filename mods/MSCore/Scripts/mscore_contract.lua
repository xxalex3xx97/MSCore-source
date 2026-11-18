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
return C
