// FILE: cpp/MSCore/msc_cmd.hpp
// WHAT: ship input commands: AddInputCommand wrapper, ShipMovementInputCommand throttle, InputView aim send, alive guard.
// WHY: ported from MiningBenchMod (mb_ue, mb_orders, mb_warpreal, mb_steer). Included after the UE4SS headers in dllmain.cpp.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "msc_caps.hpp"
#include "msc_contract.hpp"
#include "msc_strafe_logic.hpp"

namespace
{
    // resolve table, verified build = MSCore.verified_build (mscore.lua); selfcheck native.offsets guards it
    constexpr int kTypeIdOffset = 0x28;
    constexpr double kViewRawPerDeg = 65536.0;
    constexpr int kViewYawAxis = 2;     // ViewRotation.Z
    constexpr int kViewPitchAxis = 1;   // ViewRotation.Y

    // freed pointers can fault: SEH guard, no C++ objects in this function
    bool alive(UObject* o)
    {
        __try { return o && o->GetClassPrivate() && !o->IsUnreachable(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // WHAT: SEH filter: a native fault runs the handler; a C++ exception (MSVC code 0xE06D7363) keeps searching, so the caller's catch sees it.
    // WHY: bad_alloc and other C++ throws are not native faults; treating them as faults would turn a feature off for the session.
    int seh_filter(unsigned long code)
    {
        return code == 0xE06D7363UL ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER;
    }

    // WHAT: runs f inside SEH. false when it faulted (access violation on a freed object, bad native call). f's C++ locals are not destroyed then (a leak, not a crash).
    // WHY: game memory and ProcessEvent calls on dying objects fault; the caller turns the feature off instead of crashing the game.
    // NOTE: this helper has no C++ objects of its own (MSVC forbids __try in functions that have them); f may have any locals.
    //       A C++ exception thrown by f propagates out of seh_run (it does not return false).
    template <typename F>
    bool seh_run(F&& f)
    {
        __try { f(); return true; }
        __except (seh_filter(GetExceptionCode())) { return false; }
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

    // forward declarations: msc_ue.hpp (included after this file) defines these with the same signatures
    void msc_log(const std::wstring& s);
    template <typename T> bool ms_rd(const void* p, size_t off, T& out);
    template <typename T> bool ms_wr(void* p, size_t off, const T& v);

    std::mutex g_cmd_mx;      // Lua thread and game thread both send commands
    std::map<std::wstring, int> g_cmd_cls;   // class name -> layout state: 0 unknown, 1 verified, 2 failed
    bool g_cmd_row = false;   // the "cmd" cap row exists (set by the first verified class or a failure)

    // WHAT: true when the cmd module is off (runtime kill switch). Sets err. Before any row exists it is false (the first write verifies).
    bool cmd_is_off(std::wstring& err)
    {
        std::lock_guard<std::mutex> lk(g_cmd_mx);
        if (!g_cmd_row || module_ok(STR("cmd"))) return false;
        err = STR("cmd module off");
        return true;
    }

    // WHAT: layout check of cmd's class, run once per class before its first id write. false with why when the slot at +0x28 is not 0 or a
    //       class own property starts below 0x2c (the write would hit a reflected field).
    // WHY: rule R4, the id is written only after the layout is verified. The reads go through SEH (ms_rd); property walk too.
    bool cmd_layout_ok(UObject* cmd, std::wstring& why)
    {
        int32_t slot = -1;
        if (!ms_rd(cmd, kTypeIdOffset, slot) || slot != 0) { why = STR("slot +0x28 is not 0 after construct"); return false; }
        int32_t first = 0x7fffffff;
        bool walked = seh_run([&] {
            for (FProperty* p : cmd->GetClassPrivate()->ForEachProperty())
                if (p->GetOffset_Internal() < first) first = p->GetOffset_Internal();
        });
        if (!walked) { why = STR("property walk faulted"); return false; }
        if (first < 0x2c) { why = STR("own property at offset ") + std::to_wstring(first) + STR(" (< 0x2c)"); return false; }
        return true;
    }

    // WHAT: writes type id `id` at +0x28 of cmd when that int is 0 (a set id is kept). Class layout verified once before the first write.
    //       false with err when refused: cmd module off, layout check failed for the class, or a read/write fault. Nothing is written then.
    // WHY: the flush drops a command whose id is 0 (msc_cmd.hpp header). Writes go through ms_wr (SEH).
    bool cmd_write_id(UObject* cmd, int32_t id, std::wstring& err)
    {
        std::lock_guard<std::mutex> lk(g_cmd_mx);
        if (g_cmd_row && !module_ok(STR("cmd"))) { err = STR("cmd module off"); return false; }
        int32_t cur = -1;
        if (!ms_rd(cmd, kTypeIdOffset, cur)) { err = STR("cmd id read fault"); return false; }
        if (cur != 0) return true;   // id already set: kept, nothing to verify or write
        const std::wstring cls = cmd->GetClassPrivate()->GetName();
        int& st = g_cmd_cls[cls];
        if (st == 0)
        {
            std::wstring why;
            if (cmd_layout_ok(cmd, why))
            {
                st = 1;
                if (!g_cmd_row) { caps_add("cmd", true, "ids verified"); g_cmd_row = true; }
            }
            else
            {
                st = 2;
                msc_log(STR("COMPAT cmd: class ") + cls + STR(" layout check failed (") + why + STR("), id writes off"));
                caps_add("cmd:" + caps_nar(cls), false, caps_nar(why));
                module_disable(STR("cmd"), STR("layout ") + cls);
                g_cmd_row = true;
            }
        }
        if (st != 1) { err = STR("cmd layout check failed: ") + cls; return false; }
        if (!ms_wr(cmd, kTypeIdOffset, id))
        {
            msc_log(STR("COMPAT cmd: id write faulted for class ") + cls + STR(", id writes off"));
            module_disable(STR("cmd"), STR("write fault"));
            g_cmd_row = true;
            err = STR("cmd id write fault");
            return false;
        }
        return true;
    }

    // WHAT: sets the type id (int @ +0x28) of a ShipMovementInputCommand to kThrottleTypeId when it is 0. Other classes are untouched (true).
    // WHY: Ghidra: the flush drops a command whose type id is 0; the controller sets 0x3a for this class. The constructor leaves 0.
    bool fix_type_id(UObject* cmd, std::wstring& err)
    {
        if (!alive(cmd) || cmd->GetClassPrivate()->GetName() != STR("ShipMovementInputCommand")) return true;
        return cmd_write_id(cmd, kThrottleTypeId, err);
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

    // strafe state per ship (only non-zero entries are kept). The game HOLDS strafe until a command with another value arrives, and every
    // ShipMovementInput carries StrafeImpulse, so ALL sends (throttle too) must carry the current state or they would reset it.
    std::atomic<bool> g_strafe_off{false};   // marker Mods\MSCore\strafe_off.txt (default ON)
    std::map<int, StrafeSt> g_strafe;
    std::mutex g_strafe_mx;

    // WHAT: sends a ShipMovementInputCommand{DeltaThrottle = dthr, StrafeImpulse} to ship sid with AddInputCommand (no pilot, no seat). sright (+right/-left) goes into StrafeImpulse Y (+0x54), sup (+up/-down) into X (+0x50): live-verified swapped vs the first guess. sign * 65536.
    // WHY: the jump charge starts when the ship handles DeltaThrottle=+1 while bJumpTargetSet and bAlignedWithJumpTarget hold.
    //      The class has no CreateInput factory, so it is built with NewObject. Strafe is written (and its layout looked up by name) only when non-zero,
    //      so a throttle send never fails because of strafe. Fail closed: strafe property not found / fault -> false, nothing sent.
    bool send_ship_move(int sid, int dthr, int sright, int sup, std::wstring& err)
    {
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.ShipMovementInputCommand"));
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!c || !alive(ctrl)) { err = STR("ShipMovementInputCommand class or controller missing"); return false; }
        UObject* cmd = UObjectGlobals::NewObject<UObject>(ctrl, c);
        if (!alive(cmd)) { err = STR("NewObject ShipMovementInputCommand failed"); return false; }
        if (!set_int_prop(cmd, STR("DeltaThrottle"), dthr)) { err = STR("DeltaThrottle property missing"); return false; }
        if (sright != 0 || sup != 0)
        {
            bool x64 = false, y64 = false;
            uint8_t* base = reinterpret_cast<uint8_t*>(cmd);
            uint8_t* px = raw_vec_axis_ptr(base, c, STR("StrafeImpulse"), 0, x64);
            uint8_t* py = raw_vec_axis_ptr(base, c, STR("StrafeImpulse"), 1, y64);
            if (!px || !py) { err = STR("StrafeImpulse X/Y not found"); return false; }
            if (!seh_run([&] { raw_put(px, x64, static_cast<int64_t>(sup) * 65536); raw_put(py, y64, static_cast<int64_t>(sright) * 65536); }))   // X = up, Y = right
            { err = STR("StrafeImpulse write fault"); return false; }
        }
        if (!fix_type_id(cmd, err)) return false;
        return add_input(sid, cmd, err);
    }

    // WHAT: throttle step to ship sid, carrying that ship's held strafe (0,0 when none or the strafe module is off).
    bool send_ship_throttle(int sid, int delta, std::wstring& err)
    {
        int sr = 0, su = 0;
        if (!g_strafe_off.load() && module_ok(STR("strafe")))
        {
            std::lock_guard<std::mutex> lk(g_strafe_mx);
            auto it = g_strafe.find(sid);
            if (it != g_strafe.end()) { sr = it->second.s[0]; su = it->second.s[1]; }
        }
        return send_ship_move(sid, delta, sr, su, err);
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
