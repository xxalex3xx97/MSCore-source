// FILE: cpp/MSCore/msc_dockfilter.hpp
// WHAT: native dock item filter: StationConsoleInput.<SetFilter factory>(index, item, bLoading) on the class default object, then AddInputCommand(receiver, cmd).
// WHY: BlueprintUnit sets dock filters on paste. Included after msc_factory.hpp and msc_orders.hpp (uses send_typed, find_item, first_of, alive).
// GAME FACTS: StationConsoleInput = type id 63; FilterIndex@0x44, FilterItem(Item)@0x48, bLoading@0x50 (vault rea-machine-config.md). Receiver = dock MachineID.
// UNVERIFIED: the factory name and param layout are found by reflection at run time; needs the game to confirm.
#pragma once
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr int32_t kStationConsoleTypeId = 63;

    // WHAT: sets filter slot index of dock receiver to item (name, "" = clear) for loading (bLoading=true) or unloading. true when the command is sent.
    bool set_dock_filter(int receiver, int index, const std::string& itemName, bool bLoading, std::wstring& err)
    {
        if (receiver <= 0) { err = STR("receiver_id must be a positive number"); return false; }
        if (index < 0) { err = STR("filter index must be 0 or more"); return false; }

        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(ctrl)) { err = STR("no DMSPlayerController (player controller not found)"); return false; }

        UObject* item = nullptr;
        if (!itemName.empty())
        {
            std::wstring why, why2;
            item = find_item(itemName, why);
            if (!item) item = find_item("item_" + itemName, why2);
            if (!item) { err = why; return false; }
        }

        // the game itself does this (vault dock-block-filters): spawn a StationConsoleInput object, call its instance setter
        // SetFilter(index, Item, bLoading), AddInputCommand(dock id, cmd). There is no static factory, so make_input builds the object.
        UObject* cmd = make_input(STR("StationConsoleInput"), err);
        if (!cmd) return false;
        UFunction* fn = cmd->GetFunctionByNameInChain(STR("SetFilter"));
        if (!fn) { err = STR("StationConsoleInput has no SetFilter function"); return false; }

        // params by type: first int = index, first object = Item, first bool = bLoading
        FProperty *ip = nullptr, *op = nullptr, *bp = nullptr;
        for (FProperty* p : fn->ForEachProperty())
        {
            if (!ip && CastField<FIntProperty>(p)) ip = p;
            else if (!op && CastField<FObjectProperty>(p)) op = p;
            else if (!bp && CastField<FBoolProperty>(p)) bp = p;
        }
        if (!ip || !op || !bp) { err = STR("SetFilter params not found (need int, Item object, bool)"); return false; }

        const size_t psize = static_cast<size_t>(fn->GetParmsSize());
        const size_t ioff = static_cast<size_t>(ip->GetOffset_Internal());
        const size_t ooff = static_cast<size_t>(op->GetOffset_Internal());
        const size_t boff = static_cast<size_t>(bp->GetOffset_Internal());
        if (ioff >= 0x400 || ooff >= 0x400 || boff >= 0x400) { err = STR("SetFilter param offsets out of range"); return false; }

        std::vector<uint8_t> buf(psize + 64, 0);
        const int32_t idx = index;
        std::memcpy(buf.data() + ioff, &idx, sizeof(idx));
        std::memcpy(buf.data() + ooff, &item, sizeof(item));
        buf[boff] = bLoading ? 1 : 0;

        cmd->ProcessEvent(fn, buf.data());
        return send_typed(receiver, cmd, kStationConsoleTypeId, err);
    }
}
