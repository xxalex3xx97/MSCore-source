// FILE: cpp/MSCore/msc_warp.hpp
// WHAT: warp and sector reads: chunk/axes/warp-distance/system id of objects, piloting input, the teleport warp, jump arrival point, jump cost,
//       and the sector list (ores from GenParams.ResourceRichness).
// WHY: ported from MiningBenchMod mb_warp, mb_belt, mb_steer. The mining mod keeps the warp decisions and calls these for the commands.
// GAME FACTS (probes 2026-10-29):
//   warp = AddInputCommand(receiver, ShipTeleportInput{bSetTarget, NewLocation, NewQuat{W=65536}, NewChunkLocation, NewSolarSystem}).
//   The manual warp wraps it: SetPilotingInput(receiver = player controller id, bNewPiloting=1, ConsoleID) before, bNewPiloting=0 after.
//   GetJumpArrivalLocation(TargetLocation out, TargetChunkLocation, TargetSolarSystemID, ArrivalDistance) -> PVector.
// DEPENDS ON: msc_api.hpp (MsSector), msc_cmd.hpp (alive, add_input), msc_ue.hpp (obj_id, get_int_prop, get_obj_prop, make_cmd, prm, wr_fields, read_axis, pilot_console_of).
#pragma once
#include <cmath>

namespace
{
    // WHAT: chunk location of o (GetChunkLocation, else the ChunkLocation property) into c[3]. false when missing.
    bool get_chunk(UObject* o, int64_t c[3])
    {
        if (!alive(o)) return false;
        UFunction* fn = o->GetFunctionByNameInChain(STR("GetChunkLocation"));
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        auto* sp = rp ? CastField<FStructProperty>(rp) : nullptr;
        if (!sp || !sp->GetStruct())
        {
            // no GetChunkLocation function: read the ChunkLocation property
            FProperty* cp = o->GetPropertyByNameInChain(STR("ChunkLocation"));
            auto* csp = cp ? CastField<FStructProperty>(cp) : nullptr;
            if (!csp || !csp->GetStruct()) return false;
            const uint8_t* cb = reinterpret_cast<const uint8_t*>(o) + cp->GetOffset_Internal();
            return read_axis(cb, csp->GetStruct(), STR("X"), c[0]) && read_axis(cb, csp->GetStruct(), STR("Y"), c[1]) && read_axis(cb, csp->GetStruct(), STR("Z"), c[2]);
        }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        const uint8_t* base = buf.data() + rp->GetOffset_Internal();
        return read_axis(base, sp->GetStruct(), STR("X"), c[0]) && read_axis(base, sp->GetStruct(), STR("Y"), c[1]) && read_axis(base, sp->GetStruct(), STR("Z"), c[2]);
    }

    // WHAT: X, Y, Z of struct property prop of o into out[3] (raw). false when missing.
    bool sec_axes(UObject* o, const TCHAR* prop, int64_t out[3])
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return false;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(o) + p->GetOffset_Internal();
        return read_axis(base, sp->GetStruct(), STR("X"), out[0]) && read_axis(base, sp->GetStruct(), STR("Y"), out[1]) && read_axis(base, sp->GetStruct(), STR("Z"), out[2]);
    }

    // WHAT: GenParams.WarpArrivalDistance of a sector (raw). GenParams is an object (data asset). false when missing.
    bool sec_warp_dist(UObject* sec, int64_t& out)
    {
        UObject* gp = alive(sec) ? get_obj_prop(sec, STR("GenParams")) : nullptr;
        if (!alive(gp)) return false;
        return read_axis(reinterpret_cast<const uint8_t*>(gp), gp->GetClassPrivate(), STR("WarpArrivalDistance"), out);
    }

    // WHAT: solar system id of o: GetSolarSystem's object id, else an int SolarSystemID/SolarSystemId, else the SolarSystem object's id. 0 when unreadable.
    int sys_id(UObject* o)
    {
        if (!alive(o)) return 0;
        UFunction* fn = o->GetFunctionByNameInChain(STR("GetSolarSystem"));
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        if (rp && CastField<FObjectProperty>(rp))
        {
            std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
            o->ProcessEvent(fn, buf.data());
            UObject* s = *reinterpret_cast<UObject**>(buf.data() + rp->GetOffset_Internal());
            if (alive(s)) return obj_id(s);
        }
        int32_t v = 0;
        if (get_int_prop(o, STR("SolarSystemID"), v) || get_int_prop(o, STR("SolarSystemId"), v)) return v;
        UObject* s = get_obj_prop(o, STR("SolarSystem"));
        return alive(s) ? obj_id(s) : 0;
    }

    // WHAT: SetPilotingInput(bNewPiloting = on, ConsoleID = cid) sent to recv. false with err on failure.
    bool piloting_input(int recv, bool on, int cid, std::wstring& err)
    {
        UObject* cmd = make_cmd(STR("SetPilotingInput"), [on, cid](UFunction* f, std::vector<uint8_t>& b) {
            // factory params are prefixed (InNewPiloting...), so fill by type: the bool = piloting on/off, the int = console id
            for (FProperty* q : f->ForEachProperty())
            {
                uint8_t* d = b.data() + q->GetOffset_Internal();
                if (CastField<FBoolProperty>(q)) *d = on ? 1 : 0;
                else if (CastField<FIntProperty>(q)) *reinterpret_cast<int32_t*>(d) = cid;
            }
            return true;
        }, err);
        return cmd && add_input(recv, cmd, err);
    }

    // WHAT: one warp to ship sid: optional SetPilotingInput on (wrap), ShipTeleportInput (NewLocation = arr, NewChunkLocation = ch,
    //       NewSolarSystem = sys, bSetTarget = set_target), optional SetPilotingInput off. The piloting receiver is the player controller id. false with err on failure.
    bool warp_send(int sid, int sys, const int64_t ch[3], const int64_t arr[3], bool wrap, std::wstring& err, bool set_target = false)
    {
        int recv = 0, cid = 0;
        if (wrap)
        {
            UObject* con = pilot_console_of(sid);
            UObject* ctrl = first_of(STR("DMSPlayerController"));
            if (!alive(con) || !alive(ctrl)) { err = STR("no pilot console or player controller"); return false; }
            recv = obj_id(ctrl);
            cid = obj_id(con);
            msc_log(STR("WARPSECTOR ids ctrl=") + std::to_wstring(recv) + STR(" console=") + std::to_wstring(cid));
            if (!piloting_input(recv, true, cid, err)) return false;
        }
        UObject* tp = make_cmd(STR("ShipTeleportInput"), [&](UFunction* f, std::vector<uint8_t>& b) {
            // factory CreateInputShipTeleport has no bool params (bSetTarget is set on the command itself)
            struct Fld { const TCHAR* nm; std::initializer_list<std::pair<const TCHAR*, int64_t>> kv; };
            const Fld flds[] = {
                {STR("Location"), {{STR("X"), arr[0]}, {STR("Y"), arr[1]}, {STR("Z"), arr[2]}}},
                {STR("Quat"), {{STR("W"), 65536}, {STR("X"), 0}, {STR("Y"), 0}, {STR("Z"), 0}}},
                {STR("ChunkLocation"), {{STR("X"), ch[0]}, {STR("Y"), ch[1]}, {STR("Z"), ch[2]}}}};
            for (const Fld& fl : flds)
            {
                FProperty* p = nullptr;
                if (!prm(f, fl.nm, p) || !CastField<FStructProperty>(p)) { msc_log(STR("WARPSECTOR fill: ") + std::wstring(fl.nm) + STR(" missing/not struct")); return false; }
                if (!wr_fields(CastField<FStructProperty>(p)->GetStruct(), b.data() + p->GetOffset_Internal(), fl.kv))
                {
                    msc_log(STR("WARPSECTOR fill: ") + std::wstring(fl.nm) + STR(" fields failed"));
                    return false;
                }
            }
            return set_prm(f, b, STR("SolarSystemID"), &sys, sizeof(sys));
        }, err);
        if (tp && set_target) set_bool_prop(tp, STR("bSetTarget"), true);   // probe: bSetTarget=1 only sets the ship's jump target, no teleport
        if (!tp || !add_input(sid, tp, err)) return false;
        if (wrap)
        {
            std::wstring e2;
            if (!piloting_input(recv, false, 0, e2)) msc_log(STR("WARPSECTOR piloting off failed: ") + e2);
        }
        return true;
    }

    // WHAT: the ship's jump arrival point in a sector: GetJumpArrivalLocation(TargetChunkLocation = ch, TargetSolarSystemID = sys, ArrivalDistance = dist).
    //       arr gets the raw PVector. false with err when a param or the function is missing.
    bool arrival_of(UObject* ship, int sys, const int64_t ch[3], int64_t dist, int64_t arr[3], std::wstring& err)
    {
        UFunction* fn = ship->GetFunctionByNameInChain(STR("GetJumpArrivalLocation"));
        if (!fn) { err = STR("GetJumpArrivalLocation not found"); return false; }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        FProperty* p = nullptr;
        if (!prm(fn, STR("TargetChunkLocation"), p) || !CastField<FStructProperty>(p)) { err = STR("TargetChunkLocation param not found"); return false; }
        if (!wr_fields(CastField<FStructProperty>(p)->GetStruct(), buf.data() + p->GetOffset_Internal(), {{STR("X"), ch[0]}, {STR("Y"), ch[1]}, {STR("Z"), ch[2]}}))
        { err = STR("TargetChunkLocation fields failed"); return false; }
        if (!set_prm(fn, buf, STR("TargetSolarSystemID"), &sys, sizeof(sys))) { err = STR("TargetSolarSystemID param failed"); return false; }
        if (!prm(fn, STR("ArrivalDistance"), p) || !CastField<FStructProperty>(p)) { err = STR("ArrivalDistance param not found"); return false; }
        if (!wr_fields(CastField<FStructProperty>(p)->GetStruct(), buf.data() + p->GetOffset_Internal(), {{STR("Value"), dist}}))
        { err = STR("ArrivalDistance fields failed"); return false; }
        ship->ProcessEvent(fn, buf.data());
        FProperty* rp = fn->GetReturnProperty();
        auto* rsp = rp ? CastField<FStructProperty>(rp) : nullptr;
        if (!rsp || !rsp->GetStruct()) { err = STR("no PVector return"); return false; }
        const uint8_t* rb = buf.data() + rp->GetOffset_Internal();
        if (!read_axis(rb, rsp->GetStruct(), STR("X"), arr[0]) || !read_axis(rb, rsp->GetStruct(), STR("Y"), arr[1]) || !read_axis(rb, rsp->GetStruct(), STR("Z"), arr[2]))
        { err = STR("PVector fields unreadable"); return false; }
        return true;
    }

    // WHAT: GetJumpCost(TargetChunkLocation = ch, TargetSolarSystemID = sys) on the ship. cost gets the int result. false + err on failure.
    bool jump_cost(UObject* ship, int sys, const int64_t ch[3], int32_t& cost, std::wstring& err)
    {
        UFunction* fn = alive(ship) ? ship->GetFunctionByNameInChain(STR("GetJumpCost")) : nullptr;
        if (!fn) { err = STR("GetJumpCost not found"); return false; }
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        FProperty* p = nullptr;
        if (!prm(fn, STR("TargetChunkLocation"), p) || !CastField<FStructProperty>(p)) { err = STR("TargetChunkLocation param not found"); return false; }
        if (!wr_fields(CastField<FStructProperty>(p)->GetStruct(), buf.data() + p->GetOffset_Internal(), {{STR("X"), ch[0]}, {STR("Y"), ch[1]}, {STR("Z"), ch[2]}}))
        { err = STR("TargetChunkLocation fields failed"); return false; }
        if (!set_prm(fn, buf, STR("TargetSolarSystemID"), &sys, sizeof(sys))) { err = STR("TargetSolarSystemID param failed"); return false; }
        FProperty* rp = fn->GetReturnProperty();
        if (!rp || !CastField<FIntProperty>(rp)) { err = STR("GetJumpCost has no int return"); return false; }
        ship->ProcessEvent(fn, buf.data());
        cost = *reinterpret_cast<int32_t*>(buf.data() + rp->GetOffset_Internal());
        return true;
    }

    // WHAT: ore item objects of a sector's GenParams.ResourceRichness (max 40). Item is read as a raw object pointer.
    std::vector<UObject*> sec_ores(UObject* sec)
    {
        std::vector<UObject*> out;
        UObject* gp = alive(sec) ? get_obj_prop(sec, STR("GenParams")) : nullptr;
        if (!alive(gp)) return out;
        FProperty* rr = gp->GetPropertyByNameInChain(STR("ResourceRichness"));
        auto* rap = rr ? CastField<FArrayProperty>(rr) : nullptr;
        auto* esp = rap ? CastField<FStructProperty>(rap->GetInner()) : nullptr;
        if (!esp || !esp->GetStruct()) return out;
        FProperty* ip = esp->GetStruct()->FindProperty(FName(STR("Item"), FNAME_Add));
        if (!ip || !CastField<FObjectProperty>(ip))
        {
            static bool logged = false;   // log once: the layout is the same for every sector
            if (!logged) { logged = true; msc_log(STR("SECTOR ResourceRichness.Item is not an object property")); }
            return out;
        }
        auto* arr = reinterpret_cast<TArray<uint8_t>*>(reinterpret_cast<uint8_t*>(gp) + rr->GetOffset_Internal());
        const size_t es = static_cast<size_t>(rap->GetInner()->GetElementSize());
        for (int32_t i = 0; i < arr->Num() && i < 40; ++i)
        {
            UObject* it = *reinterpret_cast<UObject**>(arr->GetData() + static_cast<size_t>(i) * es + ip->GetOffset_Internal());
            if (alive(it)) out.push_back(it);
        }
        return out;
    }

    // WHAT: writes every SolarSystemSector (with a readable chunk) into out[0..max), returns the total found. out = nullptr or max = 0 counts only.
    // WHY: one FindAllOf walk; the export and the sector list both need the same fields (id, system, chunk, half extents, warp distance, ores, name).
    int sectors_fill(MsSector* out, int max)
    {
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(STR("SolarSystemSector"), v);
        int total = 0;
        for (UObject* o : v)
        {
            if (!alive(o)) continue;
            MsSector s{};
            s.id = obj_id(o);
            int32_t par = 0;
            if (get_int_prop(o, STR("ParentID"), par)) s.sys = par;
            int64_t ch[3] = {0, 0, 0};
            int64_t he[3] = {2, 2, 2};
            if (!sec_axes(o, STR("ChunkLocation"), ch)) continue;
            sec_axes(o, STR("HalfExtents"), he);
            for (int i = 0; i < 3; ++i) { s.ch[i] = ch[i]; s.he[i] = he[i]; }
            int64_t wd = 0;
            s.warp_dist = sec_warp_dist(o, wd) ? wd : 0;
            std::vector<UObject*> ores = sec_ores(o);
            s.nores = static_cast<int32_t>(ores.size());
            for (int i = 0; i < s.nores; ++i) s.ores[i] = ores[i];
            const std::wstring nm = call_text(o, STR("GetDisplayName"));
            wcsncpy_s(s.name, 96, (nm.empty() ? STR("Sector ") + std::to_wstring(s.id) : nm).c_str(), _TRUNCATE);
            if (out && total < max) out[total] = s;
            ++total;
        }
        return total;
    }
}
