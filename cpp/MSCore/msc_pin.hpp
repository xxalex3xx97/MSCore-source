// FILE: cpp/MSCore/msc_pin.hpp
// WHAT: the last map pin as g_pinned_sector. A pre-hook on DMSPlayerController.AddInputCommand stores the ObjectId of a MapPinInputCommand with bShow=1.
// WHY: the mining warp reads the pin. The hook runs on every input command, so it only reads memory: no logging, no game calls beyond reflection reads.
// GAME FACTS (probe 2026-10-29): map pin = AddInputCommand(receiver 2325, MapPinInputCommand{ObjectId = sector id, bShow, bHide, bDestroy}).
// DEPENDS ON: msc_cmd.hpp (alive, first_of), msc_ue.hpp (get_bool_prop, get_int_prop, msc_log).
// USED BY: dllmain.cpp (MSCore_pin_ensure, MSCore_pinned, MSCoreNative_pin_ensure, MSCoreNative_pinned, check "pin_hook").
#pragma once

namespace
{
    int g_pinned_sector = 0;       // last pinned sector id (bShow=1); 0 = none
    UClass* g_pin_cls = nullptr;   // MapPinInputCommand class: the hook compares class pointers
    size_t g_pin_off = 0;          // offset of InCommand in the AddInputCommand parameter block
    bool g_pin_hooked = false;
    int g_pin_tag = 0;             // hook CustomData (not used, the hook needs a pointer)

    // WHAT: pre-hook of AddInputCommand: a MapPinInputCommand with bShow=1 stores its ObjectId in g_pinned_sector. Nothing else.
    void pin_hook_pre(RC::Unreal::UnrealScriptFunctionCallableContext& ctx, void*)
    {
        uint8_t* base = ctx.TheStack.Locals();
        if (!base || !g_pin_cls) return;
        UObject* cmd = *reinterpret_cast<UObject* const*>(base + g_pin_off);
        if (!alive(cmd) || cmd->GetClassPrivate() != g_pin_cls) return;
        bool show = false;
        int32_t id = 0;
        if (get_bool_prop(cmd, STR("bShow"), show) && show && get_int_prop(cmd, STR("ObjectId"), id) && id > 0) g_pinned_sector = id;
    }

    // WHAT: post-hook placeholder (RegisterHook needs both callbacks). Does nothing.
    void pin_hook_post(RC::Unreal::UnrealScriptFunctionCallableContext&, void*) {}

    // WHAT: registers pin_hook_pre on DMSPlayerController.AddInputCommand once the controller and the MapPinInputCommand class exist. true when hooked.
    // WHY: the controller does not exist at mod load, so the caller retries from the tick until it returns true.
    bool pin_hook_ensure()
    {
        if (g_pin_hooked) return true;
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        UFunction* fn = alive(ctrl) ? ctrl->GetFunctionByNameInChain(STR("AddInputCommand")) : nullptr;
        if (!fn) return false;
        FProperty* p = fn->FindProperty(FName(STR("InCommand"), FNAME_Add));
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.MapPinInputCommand"));
        if (!p || !c) return false;
        std::wstring path = fn->GetFullName();   // "Function /Script/...:AddInputCommand" -> strip the type word
        { size_t sp = path.find(L' '); if (sp != std::wstring::npos) path = path.substr(sp + 1); }
        try { UObjectGlobals::RegisterHook(path, pin_hook_pre, pin_hook_post, &g_pin_tag); }
        catch (...) { msc_log(STR("PIN hook failed on ") + path); return false; }
        g_pin_cls = c;
        g_pin_off = static_cast<size_t>(p->GetOffset_Internal());
        g_pin_hooked = true;
        msc_log(STR("PIN hook armed on ") + path);
        return true;
    }
}
