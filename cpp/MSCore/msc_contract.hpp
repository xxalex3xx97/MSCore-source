// FILE: cpp/MSCore/msc_contract.hpp
// WHAT: the command type ids written at input command +0x28 and the game class names by logical name (cls_name). Values only, nothing learned here.
// WHY: one list of ids. msc_cmd.hpp cmd_write_id verifies the layout of each class once before its first write (fail closed).
// USED BY: msc_cmd.hpp, msc_factory.hpp, msc_rename.hpp.
#pragma once
#include <cstdint>
#include <cstring>

namespace
{
    constexpr int kThrottleTypeId = 0x3a;      // ShipMovementInputCommand (throttle), msc_cmd.hpp
    constexpr int32_t kTypeBuild = 0x2e;       // BuildInput
    constexpr int32_t kTypeDisassemble = 0x2f; // DisassembleInput
    constexpr int32_t kTypeAddBlock = 30;      // AddBlockInput (API 5)
    constexpr int32_t kTypePending = 33;       // PendingBlueprintInput (API 5)
    constexpr int32_t kTypeRemoveBlock = 31;   // RemoveBlockInput (API 5)
    constexpr int32_t kTypeInventory = 12;     // InventoryInput
    constexpr int32_t kTypePort = 17;          // MachinePortInput
    constexpr int32_t kTypeSetRecipe = 20;     // SetRecipeInput
    constexpr int32_t kSetTextTypeId = 57;     // SetTextInputCommand (dock rename), msc_rename.hpp

    // game class names by logical name (FindAllOf / class GetName). Mirror of C.classes in mods/MSCore/Scripts/mscore_contract.lua
    // (tests/test_class_contract.py compares them: keep one kCls<CamelCase> constant per line, the key is its snake_case).
    constexpr const char* kClsAsteroid = "SmallResourceAsteroidObject";
    constexpr const char* kClsAsteroidActor = "BP_GenericResourceAsteroid_C";
    constexpr const char* kClsFloatingOre = "BP_FloatingOre_C";
    constexpr const char* kClsStation = "StationObject";
    constexpr const char* kClsPlayerStation = "BP_StationObject_C";
    constexpr const char* kClsContainer = "ContainerObject";
    constexpr const char* kClsContainerActor = "BP_Container_2_C";
    constexpr const char* kClsItemCrate = "BP_ItemCrateObject_C";
    struct ClassName { const char* logical; const char* name; };
    constexpr ClassName kClasses[] = {
        { "asteroid", kClsAsteroid }, { "asteroid_actor", kClsAsteroidActor }, { "floating_ore", kClsFloatingOre },
        { "station", kClsStation }, { "player_station", kClsPlayerStation }, { "container", kClsContainer },
        { "container_actor", kClsContainerActor }, { "item_crate", kClsItemCrate } };
    inline const char* cls_name(const char* logical) { for (const ClassName& c : kClasses) if (std::strcmp(c.logical, logical) == 0) return c.name; return nullptr; }
}
