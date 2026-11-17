// FILE: cpp/MSCore/msc_contract.hpp
// WHAT: the command type ids written at input command +0x28 (named constants). Values only, nothing learned here.
// WHY: one list of ids. msc_cmd.hpp cmd_write_id verifies the layout of each class once before its first write (fail closed).
// USED BY: msc_cmd.hpp, msc_factory.hpp, msc_rename.hpp.
#pragma once
#include <cstdint>

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
}
