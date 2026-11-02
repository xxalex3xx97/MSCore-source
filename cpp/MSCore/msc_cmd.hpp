// FILE: cpp/MSCore/msc_cmd.hpp
// WHAT: ship input commands: AddInputCommand wrapper, ShipMovementInputCommand throttle, InputView aim send, alive guard.
// WHY: ported from MiningBenchMod (mb_ue, mb_orders, mb_warpreal, mb_steer). Included after the UE4SS headers in dllmain.cpp.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // resolve table, verified build 149200896; selfcheck native.offsets guards it
    constexpr int kTypeIdOffset = 0x28;
    constexpr int kThrottleTypeId = 0x3a;
    constexpr double kViewRawPerDeg = 65536.0;
    constexpr int kViewYawAxis = 2;     // ViewRotation.Z
    constexpr int kViewPitchAxis = 1;   // ViewRotation.Y

    // freed pointers can fault: SEH guard, no C++ objects in this function
    bool alive(UObject* o)
    {
        __try { return o && o->GetClassPrivate() && !o->IsUnreachable(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: copies n bytes into a UFunction parameter buffer at the named parameter's offset; false if missing or it would overflow.
    // WHY: sets inputs for ProcessEvent calls without a header; the bounds check stops a wrong name from writing past the buffer.
    bool put(UFunction* fn, std::vector<uint8_t>& buf, const TCHAR* name, const void* src, size_t n)
    {
        FProperty* p = fn->FindProperty(FName(name, FNAME_Add));
        if (!p || static_cast<size_t>(p->GetOffset_Internal()) + n > buf.size()) return false;
        std::memcpy(buf.data() + p->GetOffset_Internal(), src, n);
        return true;
    }

    // WHAT: first live UObject of class cls, or nullptr.
    // WHY: FindAllOf walks every UObject in the game, so only the first hit is returned. Slow: do not call it every tick.
    UObject* first_of(const TCHAR* cls)
    {
        std::vector<UObject*> v;
        UObjectGlobals::FindAllOf(cls, v);
        for (UObject* o : v) if (o) return o;
        return nullptr;
    }

    // WHAT: writes an IntProperty; false when missing or not an int.
    // WHY: type-checked write, so a wrong property type is never written.
    bool set_int_prop(UObject* o, const TCHAR* prop, int32_t val)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        if (!p || !CastField<FIntProperty>(p)) return false;
        auto* pv = o->GetValuePtrByPropertyNameInChain<int32_t>(prop);
        if (!pv) return false;
        *pv = val;
        return true;
    }

    // WHAT: sets the type id (int @ +0x28) of a ShipMovementInputCommand to 0x3a when it is 0. Other classes are untouched.
    // WHY: Ghidra: the flush drops a command whose type id is 0; the controller sets 0x3a for this class. The constructor leaves 0.
    void fix_type_id(UObject* cmd)
    {
        if (!alive(cmd) || cmd->GetClassPrivate()->GetName() != STR("ShipMovementInputCommand")) return;
        int32_t* tid = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(cmd) + kTypeIdOffset);
        if (*tid == 0) *tid = kThrottleTypeId;
    }

    // WHAT: calls DMSPlayerController.AddInputCommand(InReceiverID, InCommand). false with `err` set when the function or a parameter is missing.
    // WHY: the game's player input route for ship commands (the same route the player's UI uses). The mod never calls the order functions directly.
    bool add_input(int receiver, UObject* cmd, std::wstring& err)
    {
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        UFunction* f = alive(ctrl) ? ctrl->GetFunctionByNameInChain(STR("AddInputCommand")) : nullptr;
        if (!f) { err = STR("AddInputCommand not found"); return false; }
        std::vector<uint8_t> b(f->GetParmsSize() + 16, 0);
        if (!put(f, b, STR("InReceiverID"), &receiver, sizeof(receiver)) || !put(f, b, STR("InCommand"), &cmd, sizeof(cmd)))
        { err = STR("AddInputCommand params not found"); return false; }
        ctrl->ProcessEvent(f, b.data());
        return true;
    }

    // WHAT: sends a ShipMovementInputCommand{DeltaThrottle = delta} to ship sid with AddInputCommand (no pilot, no seat).
    // WHY: the jump charge starts when the ship handles DeltaThrottle=+1 while bJumpTargetSet and bAlignedWithJumpTarget hold.
    //      The class has no CreateInput factory, so it is built with NewObject.
    bool send_ship_throttle(int sid, int delta, std::wstring& err)
    {
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ShipMovementInputCommand"));
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!c || !alive(ctrl)) { err = STR("ShipMovementInputCommand class or controller missing"); return false; }
        UObject* cmd = UObjectGlobals::NewObject<UObject>(ctrl, c);
        if (!alive(cmd)) { err = STR("NewObject ShipMovementInputCommand failed"); return false; }
        if (!set_int_prop(cmd, STR("DeltaThrottle"), delta)) { err = STR("DeltaThrottle property missing"); return false; }
        fix_type_id(cmd);
        return add_input(sid, cmd, err);
    }

    // WHAT: pointer to the Value field (int32 or int64, reported in is64) of struct.vec.axis inside a raw byte buffer; nullptr when missing.
    // WHY: the InputView copy is a byte buffer, so the UObject-based property helpers cannot be used on it.
    uint8_t* raw_vec_axis_ptr(uint8_t* base, UStruct* st, const TCHAR* vec_prop, int axis, bool& is64)
    {
        static const TCHAR* names[3] = {STR("X"), STR("Y"), STR("Z")};
        if (!base || !st || axis < 0 || axis > 2) return nullptr;
        FProperty* p = st->FindProperty(FName(vec_prop, FNAME_Add));
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return nullptr;
        FProperty* f = sp->GetStruct()->FindProperty(FName(names[axis], FNAME_Add));
        auto* fsp = f ? CastField<FStructProperty>(f) : nullptr;
        if (!fsp || !fsp->GetStruct()) return nullptr;
        FProperty* v = fsp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add));
        if (!v) return nullptr;
        if (CastField<FInt64Property>(v)) is64 = true;
        else if (CastField<FIntProperty>(v)) is64 = false;
        else return nullptr;
        return base + p->GetOffset_Internal() + f->GetOffset_Internal() + v->GetOffset_Internal();
    }

    // WHAT: write v at p as int64 (is64) or int32 (truncated). Matches the width found by raw_vec_axis_ptr.
    void raw_put(uint8_t* p, bool is64, int64_t v)
    {
        if (is64) *reinterpret_cast<int64_t*>(p) = v; else *reinterpret_cast<int32_t*>(p) = static_cast<int32_t>(v);
    }

    // WHAT: send one InputView command to console cid, carrying the aim offset (yaw_raw, pitch_raw in raw units).
    //       Steps: 1 controller and its live InputView struct. 2 copy the InputView bytes. 3 force ControlMode = 1 (SHIP).
    //       4 write ViewRotation Z (yaw) and Y (pitch). 5 CreateInputView on the copy. 6 AddInputCommand(cid, command).
    // WHY: the proven steering route. The controller copy says CHARACTER while the player walks, so ControlMode must be SHIP (1).
    bool view_send(int cid, int64_t yaw_raw, int64_t pitch_raw, std::wstring& err)
    {
        // 1. controller and its InputView struct
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(ctrl)) { err = STR("DMSPlayerController not found"); return false; }
        FProperty* ip = ctrl->GetPropertyByNameInChain(STR("InputView"));
        auto* isp = ip ? CastField<FStructProperty>(ip) : nullptr;
        uint8_t* ibase = ctrl->GetValuePtrByPropertyNameInChain<uint8_t>(STR("InputView"));
        if (!isp || !isp->GetStruct() || !ibase) { err = STR("InputView unreadable"); return false; }
        UStruct* ist = isp->GetStruct();
        const int32_t isize = ist->GetStructureSize();
        UClass* vcls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ViewInput"));
        UObject* vcdo = vcls ? vcls->GetClassDefaultObject() : nullptr;
        UFunction* vfn = alive(vcdo) ? vcdo->GetFunctionByNameInChain(STR("CreateInputView")) : nullptr;
        if (!vfn) { err = STR("CreateInputView not found"); return false; }
        // 2. byte copy of the live InputView
        std::vector<uint8_t> sbuf(static_cast<size_t>(isize), 0);
        std::memcpy(sbuf.data(), ibase, static_cast<size_t>(isize));
        // 3. ControlMode = SHIP (1), from an enum dump
        if (FProperty* cm = ist->FindProperty(FName(STR("ControlMode"), FNAME_Add))) sbuf[cm->GetOffset_Internal()] = 1;
        // 4. ViewRotation yaw (Z) and pitch (Y) raw values
        bool y64 = false, p64 = false;
        uint8_t* yp = raw_vec_axis_ptr(sbuf.data(), ist, STR("ViewRotation"), kViewYawAxis, y64);
        uint8_t* pp = raw_vec_axis_ptr(sbuf.data(), ist, STR("ViewRotation"), kViewPitchAxis, p64);
        if (!yp || !pp) { err = STR("ViewRotation yaw/pitch axis not found"); return false; }
        raw_put(yp, y64, yaw_raw);
        raw_put(pp, p64, pitch_raw);
        // 5. CreateInputView(InInputView) -> ViewInput command object
        std::vector<uint8_t> buf(vfn->GetParmsSize() + 16, 0);
        if (!put(vfn, buf, STR("InInputView"), sbuf.data(), static_cast<size_t>(isize))) { err = STR("InInputView param not found"); return false; }
        vcdo->ProcessEvent(vfn, buf.data());
        FProperty* vrp = vfn->GetReturnProperty();
        UObject* cmd = vrp ? *reinterpret_cast<UObject**>(buf.data() + vrp->GetOffset_Internal()) : nullptr;
        if (!alive(cmd)) { err = STR("CreateInputView returned null"); return false; }
        // 6. send to the console
        return add_input(cid, cmd, err);
    }
}
