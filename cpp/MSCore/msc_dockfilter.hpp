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

        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.StationConsoleInput"));
        if (!c) { err = STR("StationConsoleInput class not found"); return false; }
        UObject* cdo = c->GetClassDefaultObject();
        if (!alive(cdo)) { err = STR("StationConsoleInput default object missing"); return false; }

        UFunction* fn = nullptr;
        for (const TCHAR* nm : { STR("CreateInputSetFilter"), STR("SetFilter"), STR("CreateInputSetFilterItem") })
        {
            fn = cdo->GetFunctionByNameInChain(nm);
            if (fn) break;
        }
        if (!fn)
        {
            // NEEDS-THINKING: report what exists instead of guessing
            std::wstring names;
            for (UFunction* f : c->ForEachFunction())
            {
                const std::wstring n = f->GetName();
                if (n.find(STR("Filter")) != std::wstring::npos) names += n + STR(" ");
            }
            err = STR("no SetFilter factory on StationConsoleInput; Filter functions: ") + (names.empty() ? std::wstring(STR("(none)")) : names);
            return false;
        }

        // params by type: first int = index, first object = Item, first bool = bLoading, object return = command
        FProperty* rp = fn->GetReturnProperty();
        if (!rp || !CastField<FObjectProperty>(rp)) { err = STR("SetFilter factory has no object return value (instance method?)"); return false; }
        FProperty *ip = nullptr, *op = nullptr, *bp = nullptr;
        for (FProperty* p : fn->ForEachProperty())
        {
            if (p == rp) continue;
            if (!ip && CastField<FIntProperty>(p)) ip = p;
            else if (!op && CastField<FObjectProperty>(p)) op = p;
            else if (!bp && CastField<FBoolProperty>(p)) bp = p;
        }
        if (!ip || !op || !bp) { err = STR("SetFilter factory params not found (need int, Item object, bool)"); return false; }

        const size_t psize = static_cast<size_t>(fn->GetParmsSize());
        const size_t ioff = static_cast<size_t>(ip->GetOffset_Internal());
        const size_t ooff = static_cast<size_t>(op->GetOffset_Internal());
        const size_t boff = static_cast<size_t>(bp->GetOffset_Internal());
        const size_t roff = static_cast<size_t>(rp->GetOffset_Internal());
        if (ioff >= 0x400 || ooff >= 0x400 || boff >= 0x400 || roff >= 0x400) { err = STR("SetFilter param offsets out of range"); return false; }

        std::vector<uint8_t> buf(psize + 64, 0);
        const int32_t idx = index;
        std::memcpy(buf.data() + ioff, &idx, sizeof(idx));
        std::memcpy(buf.data() + ooff, &item, sizeof(item));
        buf[boff] = bLoading ? 1 : 0;

        cdo->ProcessEvent(fn, buf.data());
        UObject* cmd = *reinterpret_cast<UObject**>(buf.data() + roff);
        if (!alive(cmd)) { err = STR("SetFilter factory returned a null command"); return false; }

        return send_typed(receiver, cmd, kStationConsoleTypeId, err);
    }
}
