// FILE: cpp/MSCore/msc_world.hpp
// WHAT: world paused state (1 paused, 0 running, -1 unknown) by one reflected call to UGameplayStatics::IsGamePaused on the CDO.
// WHY: a paused sim never processes order commands; mods stop their timeouts while this is 1. Query, not a hook: it sees unpause too.
// READ: WorldContextObject = first DMSPlayerController; return bool read from the function's return property.
// UNREADABLE: no context object / function / return prop, or a fault = -1. Mods treat only 1 as paused.
// SAFETY: alive() guards every object; the call runs under seh_run. A fault returns -1, never a crash.
// CAPS: module 'world' (world_resolve registers it at startup). Runtime kill switch: module_disable("world") in dllmain.cpp.
// USED BY: dllmain.cpp (world_resolve, selftest check "paused", MSCore_world_paused).
#pragma once
#include <cstdint>
#include <vector>

namespace
{
    void world_resolve() { static bool done = false; if (done) return; done = true; caps_add("world", true, ""); }
    // WHAT: 1 paused, 0 running, -1 unknown (no context object / function / return prop, or fault).
    // WHY: a paused sim never processes order commands; mods stop their timeouts while this is 1. Query, not a hook: it sees unpause too.
    int world_paused_read()
    {
        static UObject* ctx = nullptr;
        if (!alive(ctx)) ctx = first_of(STR("DMSPlayerController"));
        if (!alive(ctx)) return -1;
        UObject* cdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));
        if (!alive(cdo)) return -1;
        UFunction* fn = cdo->GetFunctionByNameInChain(STR("IsGamePaused"));
        FProperty* rp = fn ? fn->GetReturnProperty() : nullptr;
        if (!rp) return -1;
        std::vector<uint8_t> buf(fn->GetParmsSize() + 16, 0);
        void* c = ctx;
        if (!put(fn, buf, STR("WorldContextObject"), &c, sizeof(c))) return -1;
        int r = -1;
        if (!seh_run([&] { cdo->ProcessEvent(fn, buf.data()); r = (buf[rp->GetOffset_Internal()] & 1) ? 1 : 0; })) return -1;
        return r;
    }
}
