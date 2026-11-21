// FILE: cpp/MSCore/msc_dockkind.hpp
// WHAT: dock kind from a dock's class name (substring rule). Pure, no game headers.
// KIND: 1 industrial dock (name contains "IndustrialDock", case-sensitive), 0 normal dock (other name containing "Dock"), -1 not a dock / empty.
// RULE: the only place the dock class-name rule lives. Mods never match dock names.
// USED BY: dllmain.cpp (dock_kind_read, MSCore_dock_kind, selfcheck "dock_kind"). Test: tests/test_dockkind.cpp.
#pragma once
#include <string>
namespace
{
    inline int dock_kind_from_class(const std::wstring& cls)
    {
        if (cls.find(L"IndustrialDock") != std::wstring::npos) return 1;
        if (cls.find(L"Dock") != std::wstring::npos) return 0;
        return -1;
    }
}
