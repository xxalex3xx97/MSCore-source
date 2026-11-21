// FILE: cpp/MSCore/msc_repair.hpp
// WHAT: repairs a ship with the game's own command: RepairFrameInputCommand (type id 56), factory CreateInputRepair(count, bCreative) on the
//       class default object, receiver = the ship object id. creative = true skips the inventory cap (free repair). Also: repair need.
// WHY: never write Health directly. The game's handler heals n*10, removes n RepairItem (unless bCreative) and clamps Health to the hull.
// READ: need = ceil((GetTotalHull - GetCurrentHull) / 10), 0 when full. Full hull = GetCurrentHull == GetTotalHull (Hull.Value >> 16), NOT MaxHealth.
// SAFETY: the send runs under seh_run; a fault turns module 'repair' off (kill switch). Fail closed: module off -> false / -1.
// CAPS: module 'repair' (repair_resolve registers it in the caps_once block of dllmain.cpp). Kill switch: module_disable("repair").
// USED BY: dllmain.cpp (repair_resolve, check_one "repair", MSCore_ship_repair, MSCore_ship_repair_need).
// DEPENDS ON: msc_cmd.hpp (alive, seh_run, add_input), msc_caps.hpp, msc_ue.hpp (make_cmd, call_int), msc_flight.hpp (flight_ship).
// UNVERIFIED (live): bCreative really consumes no kits; count semantics; when Health changes (next sim tick).
#pragma once
#include <string>
#include <vector>

namespace
{
    void repair_resolve() { static bool done = false; if (done) return; done = true; caps_add("repair", true, ""); }

    // WHAT: items needed to fully repair ship sid. 0 full, -1 ship gone or hull unreadable.
    int ship_repair_need(int sid)
    {
        UObject* ship = flight_ship(sid);
        if (!ship) return -1;
        int32_t cur = 0, tot = 0;
        if (!call_int(ship, STR("GetCurrentHull"), cur) || !call_int(ship, STR("GetTotalHull"), tot)) return -1;
        const int64_t missing = static_cast<int64_t>(tot) - cur;
        return missing <= 0 ? 0 : static_cast<int>((missing + 9) / 10);
    }

    // WHAT: sends RepairFrameInputCommand(count, creative) to ship sid. false with err on failure.
    bool ship_repair(int sid, int count, bool creative, std::wstring& err)
    {
        if (sid <= 0 || count <= 0) { err = STR("bad sid or count"); return false; }
        if (!module_ok(STR("repair"))) { err = STR("repair module off"); return false; }
        if (!flight_ship(sid)) { err = STR("ship not found"); return false; }
        bool ok = false;
        if (!seh_run([&] {
                UObject* cmd = make_cmd(STR("RepairFrameInputCommand"), [creative, count](UFunction* f, std::vector<uint8_t>& b) {
                    for (FProperty* q : f->ForEachProperty())
                    {
                        uint8_t* d = b.data() + q->GetOffset_Internal();
                        if (CastField<FBoolProperty>(q)) *d = creative ? 1 : 0;
                        else if (CastField<FIntProperty>(q)) *reinterpret_cast<int32_t*>(d) = count;
                    }
                    return true;
                }, err);
                ok = cmd && add_input(sid, cmd, err);
            }))
        {
            err = STR("repair faulted");
            module_disable(STR("repair"), STR("ship_repair fault"));
            return false;
        }
        return ok;
    }

    // WHAT: read-only selftest: the command class and a CreateInput* function resolve. Sends nothing.
    bool repair_selftest(std::string& detail)
    {
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.RepairFrameInputCommand"));
        if (!c) { detail = "RepairFrameInputCommand not found"; return false; }
        for (UFunction* f : c->ForEachFunction())
            if (f->GetName().rfind(STR("CreateInput"), 0) == 0) { detail = "class + factory found"; return true; }
        detail = "CreateInput* not found";
        return false;
    }
}
