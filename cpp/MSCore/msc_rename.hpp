// FILE: cpp/MSCore/msc_rename.hpp
// WHAT: native dock rename: SetTextInputCommand.CreateInputSetText(Text, Config=0) on the class default object, then AddInputCommand(receiver, cmd).
// WHY: Lua CreateInputSetText crashes the game (UE4SS pushes the null FText param back to Lua: access violation in FText::ToFString).
//      Here the FText is built natively and never crosses into Lua. Included after msc_factory.hpp (uses send_typed, factory_guard, prm).
// GAME FACTS: SetTextInputCommand = type id 57; Text FText @ +0x40, Config @ +0x50 (vault rea-machine-config.md). Receiver = dock MachineID.
#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr size_t kRenameMaxChars = 200;
    constexpr int32_t kSetTextTypeId = 57;

    // WHAT: UTF-8 to wide string. false when the bytes are not valid UTF-8.
    bool utf8_to_wide(const std::string& in, std::wstring& out)
    {
        if (in.empty()) { out.clear(); return true; }
        const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(), static_cast<int>(in.size()), nullptr, 0);
        if (n <= 0) return false;
        out.assign(static_cast<size_t>(n), L'\0');
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(), static_cast<int>(in.size()), &out[0], n) == n;
    }

    // WHAT: renames the dock with MachineID receiver to text (UTF-8, 1..200 chars). true when the command is sent.
    //       Steps: 1 checks + controller. 2 class and CDO. 3 CreateInputSetText UFunction by name. 4 param buffer from the UFunction.
    //       5 FText built natively, borrowed into the Text param. 6 ProcessEvent. 7 returned UObject* cmd. 8 send_typed (type id 57 when 0).
    // WHY: the engine's own FText ops build the text; the buffer is never read back, so no FText is released twice.
    // NOTE: no __try here (a function with __try cannot hold C++ objects); the Lua wrapper runs it through factory_guard.
    bool rename_dock(int receiver, const std::string& text, std::wstring& err)
    {
        if (receiver <= 0) { err = STR("receiver_id must be a positive number"); return false; }
        if (text.empty()) { err = STR("dock name is empty"); return false; }
        std::wstring wtext;
        if (!utf8_to_wide(text, wtext)) { err = STR("dock name is not valid UTF-8"); return false; }
        if (wtext.size() > kRenameMaxChars) { err = STR("dock name is longer than 200 characters"); return false; }

        // 1. controller first: no command is built when nothing can send it
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(ctrl)) { err = STR("no DMSPlayerController (player controller not found)"); return false; }

        // 2. class and default object
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MainSequence.SetTextInputCommand"));
        if (!c) { err = STR("SetTextInputCommand class not found"); return false; }
        UObject* cdo = c->GetClassDefaultObject();
        if (!alive(cdo)) { err = STR("SetTextInputCommand default object missing"); return false; }

        // 3. the factory by name
        UFunction* fn = cdo->GetFunctionByNameInChain(STR("CreateInputSetText"));
        if (!fn) { err = STR("CreateInputSetText function not found"); return false; }

        // 4. parameters: Text = the FText param (by type), Config = the int param (by name, Config or InConfig)
        FProperty* tp = nullptr;
        for (FProperty* p : fn->ForEachProperty())
        {
            if (!tp && CastField<FTextProperty>(p)) tp = p;
        }
        FProperty* cp = nullptr;
        if (!tp) { err = STR("CreateInputSetText has no FText parameter"); return false; }
        if (!prm(fn, STR("Config"), cp) || !CastField<FIntProperty>(cp)) { err = STR("CreateInputSetText Config int parameter not found"); return false; }
        FProperty* rp = fn->GetReturnProperty();
        if (!rp || !CastField<FObjectProperty>(rp)) { err = STR("CreateInputSetText has no object return value"); return false; }
        const size_t psize = static_cast<size_t>(fn->GetParmsSize());
        const size_t toff = static_cast<size_t>(tp->GetOffset_Internal());
        const size_t coff = static_cast<size_t>(cp->GetOffset_Internal());
        const size_t roff = static_cast<size_t>(rp->GetOffset_Internal());
        const size_t fsize = sizeof(FText);
        wchar_t offmsg[256];
        swprintf(offmsg, 256, L"CreateInputSetText param offsets out of range: psize=%zu text=%zu config=%zu ret=%zu sizeof(FText)=%zu",
                 psize, toff, coff, roff, fsize);
        // ranges: text [toff, toff+16) (the FText object ptr + ref controller), config [coff, coff+4), ret [roff, roff+8)
        if (toff >= 0x400 || coff >= 0x400 || roff >= 0x400) { err = offmsg; return false; }
        const bool overlap = (toff < coff + 4 && coff < toff + 16) || (toff < roff + 8 && roff < toff + 16) || (coff < roff + 8 && roff < coff + 4);
        if (overlap) { err = offmsg; return false; }

        const size_t needed = std::max({ psize, toff + 24, coff + sizeof(int32_t), roff + sizeof(UObject*) });
        std::vector<uint8_t> buf(needed + 64, 0);

        // 5. FText built natively. The engine layout (16-byte ptr + ref, then flags) may differ from UE4SS sizeof(FText),
        //    so the copy goes into an aligned scratch and only the first 16 bytes move into the buffer (flags stay zero).
        //    The scratch is never destructed: the ref now belongs to the buffer (worst case one leaked ref).
        FText ftext(wtext.c_str());
        if (!ftext.GetTextData()) { err = STR("FText construction failed"); return false; }
        alignas(16) unsigned char scratch[sizeof(FText) + 16];
        FText* moved = new (scratch) FText(std::move(ftext));
        std::memcpy(buf.data() + toff, moved, 16);

        // Config written after the FText copy, so an oversized FText can never clobber it
        const int32_t config = 0;
        std::memcpy(buf.data() + coff, &config, sizeof(config));

        // 6-7. call and read the returned command
        cdo->ProcessEvent(fn, buf.data());
        UObject* cmd = *reinterpret_cast<UObject**>(buf.data() + roff);
        if (!alive(cmd)) { err = STR("CreateInputSetText returned a null command"); return false; }

        // 8. send with the existing helper (type id 57 written when the factory left it 0)
        return send_typed(receiver, cmd, kSetTextTypeId, err);
    }
}
