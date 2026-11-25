// FILE: cpp/MSCore/msc_log_logic.hpp
// WHAT: pure log filter: msc_log drops noisy PROBE/SCAN/threat lines unless Mods\MSCore\debug_probes.txt exists. Test: tests/test_capture_logic.cpp.
#pragma once
#include <string>
inline bool msc_log_pass(const std::wstring& s, bool probes)
{
    if (probes) return true;
    static const wchar_t* noisy[] = { L"PROBE ", L"SCAN walk", L"threat ship=", L"stations probe", L"combat probe weapon" };
    bool hit = false;
    for (auto k : noisy) if (s.find(k) != std::wstring::npos) { hit = true; break; }
    if (!hit) return true;
    std::wstring l = s;
    for (auto& c : l) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
    static const wchar_t* keep[] = { L"fail", L"fault", L"error", L"warn", L"exception" };
    for (auto k : keep) if (l.find(k) != std::wstring::npos) return true;
    return false;
}
