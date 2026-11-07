// FILE: cpp/MSCore/msc_factory.hpp
// WHAT: factory blueprint commands (API 4): typed send, Buildable lookup by name, BuildInput (place), DisassembleInput, SetRecipeInput,
//       MachinePortInput (port toggle), InventoryInput (move between inventories).
// WHY: ported from MiningBenchMod mb_bpprobe.hpp (bp_send_fn, bp_setpath, bp_place_fn), proven in game. Fields are set by property name.
//      Offsets and type ids: vault main-sequence rea-placement.md, rea-machine-config.md s4/s5.
// DEPENDS ON: msc_cmd.hpp (alive, seh_run, first_of, add_input, kTypeIdOffset), msc_ue.hpp (widen, set_bool_prop).
// USED BY: dllmain.cpp exports MSCore_send_typed, _find_buildable, _build_place, _disassemble, _set_recipe, _port_toggle, _inv_move
//          and the Lua natives MSCoreNative_* of the same names.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // type ids written at cmd+0x28 (flush drops a command whose id is 0)
    constexpr int32_t kTypeBuild = 0x2e;        // BuildInput
    constexpr int32_t kTypeDisassemble = 0x2f;  // DisassembleInput
    constexpr int32_t kTypeInventory = 12;      // InventoryInput
    constexpr int32_t kTypePort = 17;           // MachinePortInput
    constexpr int32_t kTypeSetRecipe = 20;      // SetRecipeInput
    constexpr int64_t kIdentityQuatW = 65536;   // PNum fixed point 1.0
    constexpr int kMaxCfgBytes = 64 * 1024;     // cap for MachineConfigData bytes (Lua and export paths)

    // TArray<uint8> layout: {ptr, num, max}
    struct RawArr { uint8_t* data; int32_t num; int32_t max; };
    static_assert(sizeof(RawArr) == sizeof(TArray<uint8_t>), "TArray layout is not {ptr, num, max}; fix RawArr");

    // WHAT: runs f under SEH. A native fault returns false with err "native fault" (the feature stays usable). f's result is returned otherwise.
    // WHY: game objects can be freed under us; a fault must not crash the game.
    template <typename F>
    bool factory_guard(F&& f, std::wstring& err)
    {
        bool ok = false;
        if (!seh_run([&] { ok = f(); })) { err = STR("native fault"); return false; }
        return ok;
    }

    // WHAT: copies cfg bytes from a C buffer. Empty when p is null or n <= 0.
    std::string bytes_of(const uint8_t* p, int n)
    {
        return (p && n > 0) ? std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(n)) : std::string();
    }

    // WHAT: writes a number into int32, int64, byte or enum property `name` of obj. false when missing or another type.
    // WHY: InventoryInput.Action is a u8 enum; the probe writes the same byte.
    bool set_num_prop(UObject* o, const TCHAR* name, int64_t v)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(name) : nullptr;
        if (!p) return false;
        uint8_t* d = o->GetValuePtrByPropertyNameInChain<uint8_t>(name);
        if (!d) return false;
        if (CastField<FInt64Property>(p)) *reinterpret_cast<int64_t*>(d) = v;
        else if (CastField<FIntProperty>(p)) *reinterpret_cast<int32_t*>(d) = static_cast<int32_t>(v);
        else if (p->GetSize() == 1 && (CastField<FByteProperty>(p) || CastField<FEnumProperty>(p))) *d = static_cast<uint8_t>(v);
        else return false;
        return true;
    }

    // WHAT: writes an object pointer into ObjectProperty `name`. false when missing or not an object property.
    bool set_obj_prop(UObject* o, const TCHAR* name, UObject* v)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(name) : nullptr;
        if (!p || !CastField<FObjectProperty>(p)) return false;
        auto* pv = o->GetValuePtrByPropertyNameInChain<UObject*>(name);
        if (!pv) return false;
        *pv = v;
        return true;
    }

    // WHAT: writes a dotted path inside a struct at base (e.g. GridTransform.GridLocation.X, Quat.W). Leaf = int/int64/byte/enum(size 1)/float/double, or a PNum struct with Value.
    // WHY: GridTransform and Quat are nested, so one level (msc_ue wr_fields) is not enough.
    bool wr_path(UStruct* st, uint8_t* base, const std::wstring& path, int64_t val)
    {
        const size_t dot = path.find(L'.');
        const std::wstring head = path.substr(0, dot);
        FProperty* f = st ? st->FindProperty(FName(head.c_str(), FNAME_Add)) : nullptr;
        if (!f) return false;
        uint8_t* d = base + f->GetOffset_Internal();
        if (dot != std::wstring::npos)
        {
            auto* sp = CastField<FStructProperty>(f);
            return sp && sp->GetStruct() && wr_path(sp->GetStruct(), d, path.substr(dot + 1), val);
        }
        FProperty* t = f;
        if (auto* sp = CastField<FStructProperty>(f))
        {
            t = sp->GetStruct() ? sp->GetStruct()->FindProperty(FName(STR("Value"), FNAME_Add)) : nullptr;
            if (!t) return false;
            d += t->GetOffset_Internal();
        }
        if (CastField<FInt64Property>(t)) *reinterpret_cast<int64_t*>(d) = val;
        else if (CastField<FIntProperty>(t)) *reinterpret_cast<int32_t*>(d) = static_cast<int32_t>(val);
        else if (t->GetSize() == 1 && (CastField<FByteProperty>(t) || CastField<FEnumProperty>(t))) *d = static_cast<uint8_t>(val);
        else if (CastField<FFloatProperty>(t)) *reinterpret_cast<float*>(d) = static_cast<float>(val);
        else if (CastField<FDoubleProperty>(t)) *reinterpret_cast<double*>(d) = static_cast<double>(val);
        else return false;
        return true;
    }

    // WHAT: copies cfg into ArrayProperty<byte> `name` of obj as a new TArray. Empty cfg = nothing to write. false when missing, not a byte array, or it already holds data.
    // WHY: BuildInput.MachineConfigData carries the SerializedMachineConfig bytes (vault rea-machine-config.md). Same as the probe bp_setpath.
    // ASSUMPTION: the engine frees TArray<uint8> data with FMemory::Free, and FMemory::Malloc is the same allocator (probe, unchanged).
    bool set_bytes_prop(UObject* o, const TCHAR* name, const std::string& cfg)
    {
        FProperty* p = o ? o->GetPropertyByNameInChain(name) : nullptr;
        auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
        if (!ap || !ap->GetInner() || !CastField<FByteProperty>(ap->GetInner()) || ap->GetInner()->GetSize() != 1) return false;
        if (cfg.empty()) return true;
        auto* ra = reinterpret_cast<RawArr*>(o->GetValuePtrByPropertyNameInChain<uint8_t>(name));
        if (!ra || ra->data) return false;
        auto* mem = static_cast<uint8_t*>(FMemory::Malloc(cfg.size()));
        std::memcpy(mem, cfg.data(), cfg.size());
        ra->data = mem;
        ra->num = ra->max = static_cast<int32_t>(cfg.size());
        return true;
    }

    // WHAT: first live Buildable data asset with this name (e.g. DAT_Refinery), or nullptr.
    // WHY: BuildInput.Buildable needs the DAT asset itself. FindAllOf walks every object: do not call it every tick.
    UObject* find_buildable(const std::string& dat)
    {
        const std::wstring want = widen(dat);
        std::vector<UObject*> bs;
        UObjectGlobals::FindAllOf(STR("Buildable"), bs);
        for (UObject* o : bs) if (alive(o) && o->GetName() == want) return o;
        return nullptr;
    }

    // WHAT: sends cmd to receiver. The type id is written at +0x28 only when that int is 0 (a set id is kept).
    // WHY: the native flush drops a command whose type id is 0 (msc_cmd.hpp).
    bool send_typed(int receiver, UObject* cmd, int32_t type_id, std::wstring& err)
    {
        if (!alive(cmd)) { err = STR("cmd is not alive"); return false; }
        if (type_id == 0) { err = STR("type id 0 would be dropped by the flush"); return false; }
        int32_t* tid = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(cmd) + kTypeIdOffset);
        if (*tid == 0) *tid = type_id;
        return add_input(receiver, cmd, err);
    }

    // WHAT: StaticConstructObject of /Script/MainSequence.<cls>, owned by the controller. nullptr with err when the class or object is missing.
    // WHY: the probe builds these input commands without a factory (vault: objects built this way need the type id written by hand).
    UObject* make_input(const TCHAR* cls, std::wstring& err)
    {
        UClass* c = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, (STR("/Script/MainSequence.") + std::wstring(cls)).c_str());
        UObject* ctrl = first_of(STR("DMSPlayerController"));
        if (!alive(c) || !alive(ctrl)) { err = STR("class or controller missing: ") + std::wstring(cls); return nullptr; }
        UObject* o = UObjectGlobals::StaticConstructObject(FStaticConstructObjectParameters(c, ctrl));
        if (!alive(o)) { err = STR("StaticConstructObject failed: ") + std::wstring(cls); return nullptr; }
        return o;
    }

    // WHAT: BuildInput for one machine: Buildable asset, AttachTargetID = station, GridTransform, identity Quat, MachineConfigData = cfg. Receiver 0 (the world).
    // WHY: the placement route of the proven probe (bp_place / bp_send with BuildInput). Location, chunk and solar system stay 0 as in the probe.
    bool build_place(int station, UObject* bdat, int x, int y, int h, int rot, const std::string& cfg, std::wstring& err)
    {
        if (!alive(bdat)) { err = STR("Buildable not alive"); return false; }
        if (cfg.size() > static_cast<size_t>(kMaxCfgBytes)) { err = STR("cfg larger than 64 KiB"); return false; }
        UObject* cmd = make_input(STR("BuildInput"), err);
        if (!cmd) return false;
        uint8_t* base = reinterpret_cast<uint8_t*>(cmd);
        UStruct* st = cmd->GetClassPrivate();
        // names the first field that failed (err = "BuildInput field missing: <path>")
        auto need = [&](bool ok, const TCHAR* field) { if (!ok) err = STR("BuildInput field missing: ") + std::wstring(field); return ok; };
        if (!need(set_obj_prop(cmd, STR("Buildable"), bdat), STR("Buildable"))
            || !need(set_num_prop(cmd, STR("AttachTargetID"), station), STR("AttachTargetID"))
            || !need(wr_path(st, base, STR("GridTransform.GridLocation.X"), x), STR("GridTransform.GridLocation.X"))
            || !need(wr_path(st, base, STR("GridTransform.GridLocation.Y"), y), STR("GridTransform.GridLocation.Y"))
            || !need(wr_path(st, base, STR("GridTransform.Height"), h), STR("GridTransform.Height"))
            || !need(wr_path(st, base, STR("GridTransform.GridRotation"), rot), STR("GridTransform.GridRotation"))
            || !need(wr_path(st, base, STR("Quat.W"), kIdentityQuatW), STR("Quat.W"))
            || !need(set_bytes_prop(cmd, STR("MachineConfigData"), cfg), STR("MachineConfigData")))
            return false;
        return send_typed(0, cmd, kTypeBuild, err);
    }

    // WHAT: DisassembleInput{TargetId = target, bMassDisassemble = mass != 0}, receiver 0.
    bool disassemble(int target, int mass, std::wstring& err)
    {
        UObject* cmd = make_input(STR("DisassembleInput"), err);
        if (!cmd) return false;
        if (!set_num_prop(cmd, STR("TargetId"), target) || !set_bool_prop(cmd, STR("bMassDisassemble"), mass != 0))
        { err = STR("DisassembleInput fields missing"); return false; }
        return send_typed(0, cmd, kTypeDisassemble, err);
    }

    // WHAT: SetRecipeInput{Recipe, Amount, Config} sent to machine id (receiver). cfg is 0 or 4 bytes: the int32 Config (little endian), 0 when empty.
    // WHY: SetRecipeInput has no config bytes; Config is an int (vault rea-machine-config.md s5). Any other length is refused.
    bool set_recipe(int machine, UObject* recipe, int amount, const std::string& cfg, std::wstring& err)
    {
        if (machine <= 0) { err = STR("machine id must be > 0"); return false; }
        if (!alive(recipe)) { err = STR("recipe not alive"); return false; }
        if (cfg.size() != 0 && cfg.size() != 4) { err = STR("cfg must be 0 or 4 bytes (SetRecipeInput.Config is an int32)"); return false; }
        int32_t slot = 0;
        if (cfg.size() == 4) std::memcpy(&slot, cfg.data(), 4);
        UObject* cmd = make_input(STR("SetRecipeInput"), err);
        if (!cmd) return false;
        if (!set_obj_prop(cmd, STR("Recipe"), recipe) || !set_num_prop(cmd, STR("Amount"), amount) || !set_num_prop(cmd, STR("Config"), slot))
        { err = STR("SetRecipeInput fields missing"); return false; }
        return send_typed(machine, cmd, kTypeSetRecipe, err);
    }

    // WHAT: MachinePortInput{PortIndex = port} sent to machine id (receiver). Toggles that port.
    bool port_toggle(int machine, int port, std::wstring& err)
    {
        if (machine <= 0) { err = STR("machine id must be > 0"); return false; }
        UObject* cmd = make_input(STR("MachinePortInput"), err);
        if (!cmd) return false;
        if (!set_num_prop(cmd, STR("PortIndex"), port)) { err = STR("MachinePortInput.PortIndex missing"); return false; }
        return send_typed(machine, cmd, kTypePort, err);
    }

    // WHAT: InventoryInput{PrimaryInventoryID, SecondaryInventoryID, SlotIndex, Action}, receiver 0. action 2 = quick move (caller's choice).
    bool inv_move(int prim, int sec, int slot, int action, std::wstring& err)
    {
        UObject* cmd = make_input(STR("InventoryInput"), err);
        if (!cmd) return false;
        if (!set_num_prop(cmd, STR("PrimaryInventoryID"), prim) || !set_num_prop(cmd, STR("SecondaryInventoryID"), sec)
            || !set_num_prop(cmd, STR("SlotIndex"), slot) || !set_num_prop(cmd, STR("Action"), action))
        { err = STR("InventoryInput fields missing"); return false; }
        return send_typed(0, cmd, kTypeInventory, err);
    }
}
