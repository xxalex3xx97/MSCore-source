// FILE: cpp/MSCore/msc_orders.hpp
// WHAT: ship order commands (new, update, delete), autopilot, SkipToOrder, logistic filter rows (send, read), item lookup and dock lookup by name.
// WHY: ported from MiningBenchMod mb_orders, mb_dockjob, mb_filters, mb_docks. Each function sends one game command and says why it failed.
// DEPENDS ON: msc_api.hpp (MsFilterRow), msc_cmd.hpp (alive, put, first_of, add_input), msc_ue.hpp.
// USED BY: dllmain.cpp exports (MSCore_new_order, _update_order, _delete_order, _autopilot, _skip_to, _filter_send, _filters_read, _find_item, _find_dock).
// GAME FACTS: the dock flow is SetShipAutopilot(true) plus SkipToOrder(0) on the existing order (verified in game).
//   An order created with NewShipOrderInput alone did not move the ship (verified). Wait conditions are not attached here (unverified).
#pragma once
#include <cstdlib>

namespace
{
    // WHAT: makes an order of class /Script/MainSequence.<cls_name> with NewShipOrderInput.CreateInputNewShipOrder(InClass) and sends it to receiver.
    // WHY: one factory call for every order kind. Receiver = ship id for a DeliverOrder, DeliverOrder id for a child.
    bool new_order(int receiver, const std::wstring& cls_name, std::wstring& err)
    {
        UClass* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (STR("/Script/MainSequence.") + cls_name).c_str());
        if (!cls) { err = STR("class not found: ") + cls_name; return false; }
        UClass* ncls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.NewShipOrderInput"));
        UObject* cdo = ncls ? ncls->GetClassDefaultObject() : nullptr;
        UFunction* fn = alive(cdo) ? cdo->GetFunctionByNameInChain(STR("CreateInputNewShipOrder")) : nullptr;
        if (!fn) { err = STR("CreateInputNewShipOrder not found"); return false; }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        if (!put(fn, buf, STR("InClass"), &cls, sizeof(cls))) { err = STR("InClass param not found"); return false; }
        cdo->ProcessEvent(fn, buf.data());
        FProperty* rp = fn->GetReturnProperty();
        UObject* cmd = rp ? *reinterpret_cast<UObject**>(buf.data() + rp->GetOffset_Internal()) : nullptr;
        if (!alive(cmd)) { err = STR("NewShipOrderInput command null"); return false; }
        return add_input(receiver, cmd, err);
    }

    // WHAT: UpdateShipOrderInput.CreateInputUpdateShipOrder(InTargetID, InItem, InComparison, InAmount), sent to order oid.
    // WHY: one builder for the dock leg (target = dock, item null) and wait/unload children (target 0, item + cmp + amount).
    bool update_order(int oid, int target, UObject* item, int cmp, int amount, std::wstring& err)
    {
        UClass* ucls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.UpdateShipOrderInput"));
        UObject* cdo = ucls ? ucls->GetClassDefaultObject() : nullptr;
        UFunction* fn = alive(cdo) ? cdo->GetFunctionByNameInChain(STR("CreateInputUpdateShipOrder")) : nullptr;
        if (!fn) { err = STR("CreateInputUpdateShipOrder not found"); return false; }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        const uint8_t cmp8 = static_cast<uint8_t>(cmp);
        if (!put_prop(fn, buf, STR("InTargetID"), &target, sizeof(target)) || !put_prop(fn, buf, STR("InItem"), &item, sizeof(item)) ||
            !put_prop(fn, buf, STR("InComparison"), &cmp8, 1) || !put_prop(fn, buf, STR("InAmount"), &amount, sizeof(amount)))
        { err = STR("UpdateShipOrderInput params not found"); return false; }
        cdo->ProcessEvent(fn, buf.data());
        FProperty* rp = fn->GetReturnProperty();
        UObject* cmd = rp ? *reinterpret_cast<UObject**>(buf.data() + rp->GetOffset_Internal()) : nullptr;
        if (!alive(cmd)) { err = STR("UpdateShipOrderInput command null"); return false; }
        return add_input(oid, cmd, err);
    }

    // WHAT: deletes one top-level order: DMSPlayerController.DeleteOrder(ShipID, OrderID). Without that function, sends
    //       ConfigureShipOrdersInput (RemoveOrderID = oid, SetAutopilotEnabled = -1, SkipToIndex = -1) to the ship.
    // WHY: DeleteOrder is the direct route; the input is the fallback. Missing params fail with a log line.
    bool delete_order(int sid, int oid, std::wstring& err)
    {
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        UFunction* fd = alive(ctrl) ? ctrl->GetFunctionByNameInChain(STR("DeleteOrder")) : nullptr;
        if (fd)
        {
            std::vector<uint8_t> bd(fd->GetParmsSize() + 16, 0);
            if (!put(fd, bd, STR("ShipID"), &sid, sizeof(sid)) || !put(fd, bd, STR("OrderID"), &oid, sizeof(oid)))
            { msc_log(STR("ORDERBUILD DeleteOrder params missing")); err = STR("DeleteOrder params missing"); return false; }
            ctrl->ProcessEvent(fd, bd.data());
            return true;
        }
        UClass* ccls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ConfigureShipOrdersInput"));
        UObject* cdo = ccls ? ccls->GetClassDefaultObject() : nullptr;
        UFunction* fn = alive(cdo) ? cdo->GetFunctionByNameInChain(STR("CreateInputConfigureShipOrders")) : nullptr;
        if (!fn) { msc_log(STR("ORDERBUILD DeleteOrder and CreateInputConfigureShipOrders missing")); err = STR("no delete function"); return false; }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        const int8_t no_autopilot = -1;
        const int32_t no_skip = -1;
        if (!put_prop(fn, buf, STR("RemoveOrderID"), &oid, sizeof(oid)) || !put_prop(fn, buf, STR("SetAutopilotEnabled"), &no_autopilot, 1) ||
            !put_prop(fn, buf, STR("SkipToIndex"), &no_skip, sizeof(no_skip)))
        { msc_log(STR("ORDERBUILD ConfigureShipOrders params missing")); err = STR("ConfigureShipOrders params missing"); return false; }
        cdo->ProcessEvent(fn, buf.data());
        FProperty* rp = fn->GetReturnProperty();
        UObject* cmd = rp ? *reinterpret_cast<UObject**>(buf.data() + rp->GetOffset_Internal()) : nullptr;
        if (!alive(cmd)) { err = STR("ConfigureShipOrdersInput command null"); return false; }
        return add_input(sid, cmd, err);
    }

    // WHAT: DMSPlayerController.SetShipAutopilot(ShipID, bNewAutopilot). false with err when the function or a param is missing.
    // WHY: the native autopilot does the dock legs; it is switched on for a dock leg and off by the cleanup paths.
    bool set_autopilot(int sid, bool on, std::wstring& err)
    {
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        UFunction* fa = alive(ctrl) ? ctrl->GetFunctionByNameInChain(STR("SetShipAutopilot")) : nullptr;
        if (!fa) { err = STR("SetShipAutopilot not found"); return false; }
        std::vector<uint8_t> ba(fa->GetParmsSize() + 16, 0);
        uint8_t v = on ? 1 : 0;
        if (!put(fa, ba, STR("ShipID"), &sid, sizeof(sid)) || !put(fa, ba, STR("bNewAutopilot"), &v, 1))
        { err = STR("SetShipAutopilot params not found"); return false; }
        ctrl->ProcessEvent(fa, ba.data());
        return true;
    }

    // WHAT: DMSPlayerController.SkipToOrder(ShipID, NewIndex = idx) on ship sid. before/after = CurrentOrderIndex around the call.
    // WHY: the player's dock flow starts a leg with SkipToOrder on the existing order (verified in game). The log shows whether the index changed.
    bool skip_to(int sid, int idx, int32_t& before, int32_t& after, std::wstring& err)
    {
        UObject* ship = find_by_id(STR("DMSPlayerShipObject"), sid);
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        UFunction* fs = alive(ctrl) ? ctrl->GetFunctionByNameInChain(STR("SkipToOrder")) : nullptr;
        if (!fs) { err = STR("SkipToOrder missing"); return false; }
        if (!alive(ship)) { err = STR("ship not found"); return false; }
        std::vector<uint8_t> bs(fs->GetParmsSize() + 16, 0);
        if (!put(fs, bs, STR("ShipID"), &sid, sizeof(sid)) || !put(fs, bs, STR("NewIndex"), &idx, sizeof(idx)))
        { err = STR("SkipToOrder params not found"); return false; }
        before = -1;
        after = -1;
        get_int_prop(ship, STR("CurrentOrderIndex"), before);
        ctrl->ProcessEvent(fs, bs.data());
        get_int_prop(ship, STR("CurrentOrderIndex"), after);
        msc_log(STR("SkipToOrder NewIndex=") + std::to_wstring(idx) + STR(" CurrentOrderIndex before=") + std::to_wstring(before) +
                STR(" after=") + std::to_wstring(after));
        return true;
    }

    // WHAT: reads ship.LogisticFilters into out (walks the raw TArray by the struct stride). false when a property or layout is missing.
    // WHY: no header for the struct, so the layout comes from reflection (same way as inv_total in the mining mod).
    bool filters_read(UObject* ship, std::vector<MsFilterRow>& out)
    {
        out.clear();
        if (!alive(ship)) return false;
        FProperty* lp = ship->GetPropertyByNameInChain(STR("LogisticFilters"));
        auto* ap = lp ? CastField<FArrayProperty>(lp) : nullptr;
        auto* arr = ap ? ship->GetValuePtrByPropertyNameInChain<TArray<uint8_t>>(STR("LogisticFilters")) : nullptr;
        auto* isp = (ap && ap->GetInner()) ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
        if (!arr || !isp || !isp->GetStruct()) return false;
        UStruct* st = isp->GetStruct();
        FProperty* fs = st->FindProperty(FName(STR("ItemStack"), FNAME_Add));
        FProperty* fa = st->FindProperty(FName(STR("Action"), FNAME_Add));
        auto* sst = fs ? CastField<FStructProperty>(fs) : nullptr;
        if (!sst || !sst->GetStruct() || !fa) return false;
        FProperty* fi = sst->GetStruct()->FindProperty(FName(STR("Item"), FNAME_Add));
        FProperty* fn = sst->GetStruct()->FindProperty(FName(STR("Amount"), FNAME_Add));
        int32_t stride = ap->GetInner()->GetElementSize();
        if (!fi || !fn || stride <= 0) return false;
        for (int32_t i = 0; i < arr->Num(); ++i)
        {
            uint8_t* e = &(*arr)[0] + static_cast<size_t>(i) * static_cast<size_t>(stride);
            MsFilterRow r{};
            r.item = *reinterpret_cast<UObject**>(e + fs->GetOffset_Internal() + fi->GetOffset_Internal());
            r.amount = *reinterpret_cast<int32_t*>(e + fs->GetOffset_Internal() + fn->GetOffset_Internal());
            r.action = *(e + fa->GetOffset_Internal());
            out.push_back(r);
        }
        return true;
    }

    // WHAT: LogisticFilterInput.CreateInputLogisticFilter(InIndex, InItemStack{Item, Amount}, InAction, bInRemove) sent to ship sid.
    //       remove = true removes the row at idx. item may be nullptr (new empty row).
    // WHY: same CDO + ProcessEvent + return-value pattern as the order commands. InItemStack is a struct param, so Item and Amount are written at their offsets.
    bool filter_send(int sid, int idx, UObject* item, int amount, int action, bool remove, std::wstring& err)
    {
        UClass* lcls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.LogisticFilterInput"));
        UObject* cdo = lcls ? lcls->GetClassDefaultObject() : nullptr;
        UFunction* fn = alive(cdo) ? cdo->GetFunctionByNameInChain(STR("CreateInputLogisticFilter")) : nullptr;
        if (!fn) { err = STR("CreateInputLogisticFilter not found"); return false; }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        FProperty* sp = fn->FindProperty(FName(STR("InItemStack"), FNAME_Add));
        auto* ssp = sp ? CastField<FStructProperty>(sp) : nullptr;
        UStruct* ist = ssp ? ssp->GetStruct() : nullptr;
        FProperty* si = ist ? ist->FindProperty(FName(STR("Item"), FNAME_Add)) : nullptr;
        FProperty* sa = ist ? ist->FindProperty(FName(STR("Amount"), FNAME_Add)) : nullptr;
        if (!si || !sa) { err = STR("InItemStack params not found"); return false; }
        if (static_cast<size_t>(sp->GetOffset_Internal()) + static_cast<size_t>(sp->GetSize()) > buf.size()) { err = STR("InItemStack overflows params"); return false; }
        const uint8_t act = static_cast<uint8_t>(action);
        const uint8_t rem = remove ? 1 : 0;
        if (!put(fn, buf, STR("InIndex"), &idx, sizeof(idx)) || !put(fn, buf, STR("InAction"), &act, 1) || !put(fn, buf, STR("bInRemove"), &rem, 1))
        { err = STR("LogisticFilter params not found"); return false; }
        uint8_t* base = buf.data() + sp->GetOffset_Internal();
        *reinterpret_cast<UObject**>(base + si->GetOffset_Internal()) = item;
        *reinterpret_cast<int32_t*>(base + sa->GetOffset_Internal()) = amount;
        cdo->ProcessEvent(fn, buf.data());
        FProperty* rp = fn->GetReturnProperty();
        UObject* cmd = rp ? *reinterpret_cast<UObject**>(buf.data() + rp->GetOffset_Internal()) : nullptr;
        if (!alive(cmd)) { err = STR("LogisticFilterInput command null"); return false; }
        return add_input(sid, cmd, err);
    }

    // WHAT: the one Item whose object name or display name equals res (case-insensitive), or a numeric ID. Returns nullptr with why set on zero or several matches.
    // WHY: resources come as names or ids; an ambiguous match is reported, never guessed.
    UObject* find_item(const std::string& res, std::wstring& why)
    {
        std::wstring w = lower(widen(res));
        bool num = !res.empty() && res.find_first_not_of("0123456789") == std::string::npos;
        int want = num ? static_cast<int>(std::strtol(res.c_str(), nullptr, 10)) : -1;
        std::vector<UObject*> items;
        UObjectGlobals::FindAllOf(STR("Item"), items);
        UObject* hit = nullptr;
        int n = 0;
        for (UObject* it : items)
        {
            if (!it) continue;
            bool m = lower(it->GetName()) == w || lower(obj_display(it)) == w;
            if (!m && num)
            {
                FProperty* p = it->GetPropertyByNameInChain(STR("ID"));
                if (p && CastField<FIntProperty>(p))
                {
                    auto* v = it->GetValuePtrByPropertyNameInChain<int32_t>(STR("ID"));
                    m = v && *v == want;
                }
            }
            if (m) { ++n; hit = it; }
        }
        if (n == 0) why = STR("no Item matches resource '") + widen(res) + STR("'");
        else if (n > 1) why = STR("resource '") + widen(res) + STR("' matches ") + std::to_wstring(n) + STR(" Items (ambiguous)");
        return n == 1 ? hit : nullptr;
    }

    // WHAT: the in-game name of a dock: its DisplayName text (empty when there is none).
    // WHY: game docks have an empty Name property, so DisplayName is the name the player sees.
    std::wstring dock_name(UObject* o) { return read_text_prop(o, STR("DisplayName")); }

    // WHAT: id of the one dock whose in-game name matches. Pass 0 = exact (case-insensitive), pass 1 = substring.
    //       -1 none, -2 several (why says which).
    // WHY: the mining INI and the Lua window pick a dock by the name the player sees; exact first, so a full name beats a longer name containing it.
    int find_dock_by_name(const std::string& want_s, std::wstring& why)
    {
        std::wstring w = lower(widen(want_s));
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(STR("Dock"), v);
        for (int pass = 0; pass < 2; ++pass)
        {
            int hits = 0, id = -1;
            for (UObject* o : v)
            {
                if (!o) continue;
                std::wstring n = lower(dock_name(o));
                if (n.empty()) continue;
                if (pass == 0 ? n == w : n.find(w) != std::wstring::npos) { ++hits; id = obj_id(o); }
            }
            if (hits == 1) return id;
            if (hits > 1) { why = STR("dock name '") + widen(want_s) + STR("' matches ") + std::to_wstring(hits) + STR(" docks (ambiguous)"); return -2; }
        }
        why = STR("no dock with in-game name '") + widen(want_s) + STR("'");
        return -1;
    }
}
