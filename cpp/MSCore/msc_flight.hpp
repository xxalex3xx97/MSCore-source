// FILE: cpp/MSCore/msc_flight.hpp
// WHAT: ship flight state in one read (MsFlight, msc_api.hpp): actor frame, chunk / location / Quat, throttle and steps, the bool flags,
//       max speeds, collision radius, power grid and low-power limits. Also: pilot console id, zero (Throttle 0 + rotation impulse 0),
//       set throttle, actor location.
// WHY: the mining mod calls these by logical name, so the game names (Throttle, PowerGrid, FlightModel, ...) live only in this module.
//      Ported from MiningBenchMod mb_ue (get_loc, quat4, call_pnum, get_num_prop, pvec_axis_ptr, pvec_set_axis),
//      mb_steer (zero_all, power_grid, lp_flag, lp_limits) and mb_rocks (act_frame: actor vectors, object Location, Quat).
// READ: raw values are game fixed point (65536 = 1 unit). Actor vectors are K2_GetActorLocation / GetActor*Vector (uu, float).
// SAFETY: every group runs under its own seh_run, so one faulting group keeps the others. ship_flight_read returns -1 when a group faulted;
//         the export then disables module 'flight' (kill switch) and returns -1. Writes happen only after the matching read worked.
// CAPS: module 'flight' (flight_resolve registers it in the caps_once block of dllmain.cpp). Kill switch: module_disable("flight").
// USED BY: dllmain.cpp (flight_resolve, check_one "flight", MSCore_ship_flight, MSCore_ship_set_throttle, MSCore_ship_zero,
//          MSCore_ship_console, MSCore_actor_loc).
// DEPENDS ON: msc_api.hpp (MsFlight, FL_*, FF_*), msc_cmd.hpp (alive, seh_run, set_int_prop), msc_caps.hpp (caps_add, module_disable, module_ok),
//             msc_ue.hpp (find_by_id, pilot_console_of, obj_id, get_int_prop, get_bool_prop, get_obj_prop, call_int, actor_vec_call, msc_log),
//             msc_ships.hpp (pnum_raw), msc_warp.hpp (get_chunk, sys_id).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // WHAT: registers the flight cap (ok) once. The read is checked by the selftest (check "flight"): a world with no ship is a SKIP.
    void flight_resolve() { static bool done = false; if (done) return; done = true; caps_add("flight", true, ""); }

    // WHAT: the ship object of save id sid when it is alive, else nullptr. Game class name lives here.
    UObject* flight_ship(int sid)
    {
        UObject* s = sid > 0 ? find_by_id(STR("DMSPlayerShipObject"), sid) : nullptr;
        return alive(s) ? s : nullptr;
    }

    // WHAT: X, Y, Z of a PVector-like struct at base into out (raw). false when a field is missing.
    // WHY: positions are PNum fields (Value int32 or int64); read_axis accepts both.
    bool flight_vec3(const uint8_t* base, UStruct* st, double out[3])
    {
        int64_t v[3] = {0, 0, 0};
        if (!base || !read_axis(base, st, STR("X"), v[0]) || !read_axis(base, st, STR("Y"), v[1]) || !read_axis(base, st, STR("Z"), v[2])) return false;
        for (int i = 0; i < 3; ++i) out[i] = static_cast<double>(v[i]);
        return true;
    }

    // WHAT: location of o in raw units: GetLocation (function) first, the Location property as fallback. false when neither reads.
    // WHY: ported as is from mb_ue get_loc. UNVERIFIED: whether either is chunk-relative (the mining rocks notes say chunk-relative).
    bool flight_loc(UObject* o, double out[3])
    {
        if (!o) return false;
        UFunction* fn = o->GetFunctionByNameInChain(STR("GetLocation"));
        if (fn)
        {
            FProperty* rp = fn->GetReturnProperty();
            auto* rsp = rp ? CastField<FStructProperty>(rp) : nullptr;
            if (rsp && rsp->GetStruct())
            {
                std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
                o->ProcessEvent(fn, buf.data());
                return flight_vec3(buf.data() + rp->GetOffset_Internal(), rsp->GetStruct(), out);
            }
        }
        FProperty* p = o->GetPropertyByNameInChain(STR("Location"));
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        auto* base = sp ? o->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Location")) : nullptr;
        return base && flight_vec3(base, sp->GetStruct(), out);
    }

    // WHAT: Quat {W,X,Y,Z} (each a PNum Value) into unitless doubles, / 65536. false when missing.
    bool quat4(UObject* o, double q[4])
    {
        if (!alive(o)) return false;
        FProperty* p = o->GetPropertyByNameInChain(STR("Quat"));
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        uint8_t* base = (sp && sp->GetStruct()) ? o->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Quat")) : nullptr;
        if (!base) return false;
        static const TCHAR* names[4] = {STR("W"), STR("X"), STR("Y"), STR("Z")};
        for (int i = 0; i < 4; ++i)
        {
            FProperty* f = sp->GetStruct()->FindProperty(FName(names[i], FNAME_Add));
            auto* fsp = f ? CastField<FStructProperty>(f) : nullptr;
            if (!fsp || !fsp->GetStruct()) return false;
            FProperty* v = fsp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add));
            if (!v) return false;
            const uint8_t* d = base + f->GetOffset_Internal() + v->GetOffset_Internal();
            if (CastField<FInt64Property>(v)) q[i] = static_cast<double>(*reinterpret_cast<const int64_t*>(d)) / 65536.0;
            else if (CastField<FIntProperty>(v)) q[i] = static_cast<double>(*reinterpret_cast<const int32_t*>(d)) / 65536.0;
            else return false;
        }
        return true;
    }

    // WHAT: raw Value of a PNum getter (function) into out. false when missing. Out is set only on success.
    bool call_pnum(UObject* o, const TCHAR* fname, int64_t& out)
    {
        if (!alive(o)) return false;
        UFunction* fn = o->GetFunctionByNameInChain(fname);
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        auto* sp = rp ? CastField<FStructProperty>(rp) : nullptr;
        if (!sp || !sp->GetStruct()) return false;
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        o->ProcessEvent(fn, buf.data());
        FProperty* v = sp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add));
        if (!v) return false;
        const uint8_t* d = buf.data() + rp->GetOffset_Internal() + v->GetOffset_Internal();
        if (CastField<FInt64Property>(v)) out = *reinterpret_cast<const int64_t*>(d);
        else if (CastField<FIntProperty>(v)) out = *reinterpret_cast<const int32_t*>(d);
        else return false;
        return true;
    }

    // WHAT: a numeric property (Int, Int64, Float or Double) as double; false when missing or another type.
    bool get_num_prop(UObject* o, const TCHAR* prop, double& out)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(prop) : nullptr;
        if (!p) return false;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(o) + p->GetOffset_Internal();
        if (CastField<FFloatProperty>(p)) { out = *reinterpret_cast<const float*>(base); return true; }
        if (CastField<FDoubleProperty>(p)) { out = *reinterpret_cast<const double*>(base); return true; }
        if (CastField<FIntProperty>(p)) { out = *reinterpret_cast<const int32_t*>(base); return true; }
        if (CastField<FInt64Property>(p)) { out = static_cast<double>(*reinterpret_cast<const int64_t*>(base)); return true; }
        return false;
    }

    // WHAT: address of one axis (X=0, Y=1, Z=2) Value field of a PVector property; is64 = its width. nullptr when the layout is not found.
    uint8_t* pvec_axis_ptr(UObject* o, const TCHAR* prop, int axis, bool& is64)
    {
        static const TCHAR* names[3] = {STR("X"), STR("Y"), STR("Z")};
        if (!o || axis < 0 || axis > 2) return nullptr;
        FProperty* p = o->GetPropertyByNameInChain(prop);
        auto* sp = p ? CastField<FStructProperty>(p) : nullptr;
        if (!sp || !sp->GetStruct()) return nullptr;
        uint8_t* base = o->GetValuePtrByPropertyNameInChain<uint8_t>(prop);
        if (!base) return nullptr;
        FProperty* f = sp->GetStruct()->FindProperty(FName(names[axis], FNAME_Add));
        auto* fsp = f ? CastField<FStructProperty>(f) : nullptr;
        if (!fsp || !fsp->GetStruct()) return nullptr;
        FProperty* v = fsp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add));
        if (!v) return nullptr;
        if (CastField<FInt64Property>(v)) is64 = true;
        else if (CastField<FIntProperty>(v)) is64 = false;
        else return nullptr;
        return base + f->GetOffset_Internal() + v->GetOffset_Internal();
    }

    // WHAT: sets one raw axis of a PVector property in place. false when the layout is missing.
    bool pvec_set_axis(UObject* o, const TCHAR* prop, int axis, int64_t val)
    {
        bool is64 = false;
        uint8_t* d = pvec_axis_ptr(o, prop, axis, is64);
        if (!d) return false;
        if (is64) *reinterpret_cast<int64_t*>(d) = val;
        else *reinterpret_cast<int32_t*>(d) = static_cast<int32_t>(val);
        return true;
    }

    // WHAT: sets the three RotationImpulseInput axes to 0. false when any axis is missing.
    // WHY: the game may overwrite the value on the next tick (mining notes, brief Update 22). Callers use it as the stop, same as mb_steer zero_all.
    bool rot_zero(UObject* ship)
    {
        bool ok = true;
        for (int a = 0; a < 3; ++a) ok = pvec_set_axis(ship, STR("RotationImpulseInput"), a, 0) && ok;
        return ok;
    }

    // WHAT: low-power flag name on this build: bInLowPowerMode, else bLowPower (nullptr until seen). Logged once.
    const TCHAR* g_flight_lp_name = nullptr;
    bool g_flight_lp_logged = false;

    // WHAT: low-power flag of ship into out. false when the ship is gone or has no such flag (out untouched).
    bool flight_lp_flag(UObject* ship, bool& out)
    {
        if (!alive(ship)) return false;
        if (!g_flight_lp_name)
        {
            g_flight_lp_name = ship->GetPropertyByNameInChain(STR("bInLowPowerMode")) ? STR("bInLowPowerMode") : ship->GetPropertyByNameInChain(STR("bLowPower")) ? STR("bLowPower") : nullptr;
            if (!g_flight_lp_logged)
            {
                g_flight_lp_logged = true;
                msc_log(g_flight_lp_name ? STR("POWER flag uses ") + std::wstring(g_flight_lp_name) : std::wstring(STR("POWER flag: neither bInLowPowerMode nor bLowPower on the ship")));
            }
        }
        return g_flight_lp_name && get_bool_prop(ship, g_flight_lp_name, out);
    }

    // WHAT: FlightModel LowPowerThrottle (Throttle step cap) and MaxLowPowerSpeed (fraction of max speed). false when either is unreadable.
    bool flight_lp_limits(UObject* ship, int32_t& thr, double& frac)
    {
        UObject* fm = get_obj_prop(ship, STR("FlightModel"));
        if (!alive(fm)) return false;
        int32_t t = 0;
        double f = 0.0;
        if (!get_int_prop(fm, STR("LowPowerThrottle"), t) || !get_num_prop(fm, STR("MaxLowPowerSpeed"), f)) return false;
        thr = t;
        frac = f;
        return true;
    }

    // WHAT: pilot console id of ship sid (PilotConsoleObject: ShipID first, then ParentID). 0 when none.
    int flight_console_id(int sid)
    {
        UObject* c = pilot_console_of(sid);
        return alive(c) ? obj_id(c) : 0;
    }

    // WHAT: writes Throttle = step (IntProperty, type-checked). false with err when the ship is gone or the write fails.
    bool flight_set_throttle(int sid, int step, std::wstring& err)
    {
        UObject* ship = flight_ship(sid);
        if (!ship) { err = STR("ship not found"); return false; }
        bool ok = false;
        if (!seh_run([&] { ok = set_int_prop(ship, STR("Throttle"), step); }))
        {
            err = STR("Throttle write faulted");
            module_disable(STR("flight"), STR("set_throttle fault"));
            return false;
        }
        if (!ok) err = STR("Throttle int prop missing");
        return ok;
    }

    // WHAT: stop the ship: Throttle = 0 and RotationImpulseInput xyz = 0. false with err when a write is missing.
    // WHY: same stop as mb_steer zero_all. Throttle is written first, so a missing rotation axis still stops the drive.
    bool flight_zero(int sid, std::wstring& err)
    {
        UObject* ship = flight_ship(sid);
        if (!ship) { err = STR("ship not found"); return false; }
        bool thr = false, rot = false;
        if (!seh_run([&] { thr = set_int_prop(ship, STR("Throttle"), 0); rot = rot_zero(ship); }))
        {
            err = STR("zero faulted");
            module_disable(STR("flight"), STR("zero fault"));
            return false;
        }
        if (!thr) err = STR("Throttle int prop missing");
        else if (!rot) err = STR("RotationImpulseInput axis not found");
        return thr && rot;
    }

    // WHAT: K2_GetActorLocation of an actor (uu) into out. false with why when the actor is dead or the call is missing.
    bool flight_actor_loc(UObject* actor, double out[3], std::wstring& why)
    {
        if (!alive(actor)) { why = STR("actor not alive"); return false; }
        double tmp[3] = {0, 0, 0};
        if (!actor_vec_call(actor, STR("K2_GetActorLocation"), tmp, why)) return false;
        std::copy(tmp, tmp + 3, out);
        return true;
    }

    // ---- read groups: each writes into f only after its own read worked; flags say which ----

    // WHAT: POSE group: actor frame (HAS_ACTOR, ACTOR_FRAME), chunk + Location + Quat (OBJ_POSE), solar system id.
    // WHY: the actor frame is the game's own frame for distances and aim; the object chunk/Location/Quat is the fallback frame.
    void fl_pose(UObject* ship, MsFlight& f)
    {
        UObject* actor = get_obj_prop(ship, STR("Actor"));
        if (actor) f.flags |= FF_HAS_ACTOR;
        if (alive(actor))
        {
            double loc[3] = {0, 0, 0}, fw[3] = {1, 0, 0}, R[3] = {0, 1, 0}, U[3] = {0, 0, 1};
            std::wstring why;
            if (flight_actor_loc(actor, loc, why) && actor_vec_call(actor, STR("GetActorForwardVector"), fw, why) &&
                actor_vec_call(actor, STR("GetActorRightVector"), R, why) && actor_vec_call(actor, STR("GetActorUpVector"), U, why))
            {
                std::copy(loc, loc + 3, f.act_loc);
                std::copy(fw, fw + 3, f.fw);
                std::copy(R, R + 3, f.R);
                std::copy(U, U + 3, f.U);
                f.flags |= FF_ACTOR_FRAME;
            }
        }
        int64_t c[3] = {0, 0, 0};
        double loc_raw[3] = {0, 0, 0}, q[4] = {0, 0, 0, 0};
        if (get_chunk(ship, c) && flight_loc(ship, loc_raw) && quat4(ship, q))
        {
            std::copy(c, c + 3, f.ch);
            std::copy(loc_raw, loc_raw + 3, f.loc_raw);
            std::copy(q, q + 4, f.quat);
            f.flags |= FF_OBJ_POSE;
        }
        f.sys = sys_id(ship);
    }

    // WHAT: STATE group: Throttle, GetThrottleSteps, bAutopilot / bDockingAutopilot / bTravelling (flag bit only when true), CollisionRadius.
    void fl_state(UObject* ship, MsFlight& f)
    {
        int32_t v = 0;
        if (get_int_prop(ship, STR("Throttle"), v)) { f.throttle = v; f.flags |= FF_THROTTLE; }
        if (call_int(ship, STR("GetThrottleSteps"), v)) { f.steps = v; f.flags |= FF_STEPS; }
        bool ap = false, dap = false, trav = false;
        if (get_bool_prop(ship, STR("bAutopilot"), ap) && ap) f.flags |= FF_AUTOPILOT;
        if (get_bool_prop(ship, STR("bDockingAutopilot"), dap) && dap) f.flags |= FF_DOCK_AP;
        if (get_bool_prop(ship, STR("bTravelling"), trav) && trav) f.flags |= FF_TRAVELLING;
        int64_t raw = 0;
        if (pnum_raw(ship, STR("CollisionRadius"), raw))
        {
            f.coll_radius_raw = raw;
            if (raw > 0) f.flags |= FF_COLL;
        }
    }

    // WHAT: LIMITS group: GetModifiedMaxSpeed and GetMaxSpeed, raw. Each stays 0 when its call is missing.
    void fl_limits(UObject* ship, MsFlight& f)
    {
        call_pnum(ship, STR("GetModifiedMaxSpeed"), f.max_mod_raw);
        call_pnum(ship, STR("GetMaxSpeed"), f.max_base_raw);
    }

    // WHAT: POWER group: PowerGrid totals (GRID), low-power flag (LP_KNOWN / LP_ON), FlightModel low-power limits (LP_LIMITS).
    void fl_power(UObject* ship, MsFlight& f)
    {
        UObject* grid = get_obj_prop(ship, STR("PowerGrid"));
        double p = 0.0, c = 0.0, e = 0.0;
        if (alive(grid) && get_num_prop(grid, STR("TotalProduction"), p) && get_num_prop(grid, STR("TotalConsumption"), c) &&
            get_num_prop(grid, STR("ConsumerEfficiency"), e))
        {
            f.grid_prod = p;
            f.grid_cons = c;
            f.grid_eff = e;
            f.flags |= FF_GRID;
        }
        bool on = false;
        if (flight_lp_flag(ship, on))
        {
            f.flags |= FF_LP_KNOWN;
            if (on) f.flags |= FF_LP_ON;
        }
        int32_t thr = 0;
        double frac = 0.0;
        if (flight_lp_limits(ship, thr, frac))
        {
            f.lp_thr = thr;
            f.lp_frac = frac;
            f.flags |= FF_LP_LIMITS;
        }
    }

    // WHAT: reads the flight state of ship sid into f (zero-filled first; size and sid set). mask = FL_* groups to read.
    // RETURNS: 0 ship gone (f holds only size / sid), 1 read, -1 a group faulted (the groups before and after it are still in f).
    // WHY: one call per tick is enough: the caller picks only the groups it needs (FL_POSE is the expensive one).
    int ship_flight_read(int sid, int mask, MsFlight& f)
    {
        std::memset(&f, 0, sizeof(f));
        f.size = static_cast<int32_t>(sizeof(MsFlight));
        f.sid = sid;
        UObject* ship = flight_ship(sid);
        if (!ship) return 0;
        f.flags |= FF_ALIVE;
        int rc = 1;
        if ((mask & FL_POSE) && !seh_run([&] { fl_pose(ship, f); })) rc = -1;
        if ((mask & FL_STATE) && !seh_run([&] { fl_state(ship, f); })) rc = -1;
        if ((mask & FL_LIMITS) && !seh_run([&] { fl_limits(ship, f); })) rc = -1;
        if ((mask & FL_POWER) && !seh_run([&] { fl_power(ship, f); })) rc = -1;
        return rc;
    }
}
