// FILE: cpp/MSCore/msc_presence.hpp
// WHAT: pure debounce step for "is this ship still in the list": one read per tick in, the new listed flag and the absent count out.
// WHY: mining mods must not drop a ship on one missed read. Pure function: no game access, no state outside the arguments.
// RULES: read 1 (present) -> bad 0, listed. read -1 (unknown) -> bad unchanged, keeps the old listed value.
//        read 0 (absent) -> bad = min(bad_n + 1, 3); a never-listed ship is rejected at once, a listed one is dropped on the 3rd consecutive absent read.
// USED BY: dllmain.cpp (MSCore_presence_step, API 15). Native unit test: tests/test_presence.cpp.
#pragma once

namespace
{
    // WHAT: one debounce step. Returns the new listed flag (1 / 0). bad_out may be null.
    inline int presence_step(int bad_n, int listed, int read, int* bad_out)
    {
        int bad = bad_n;
        int out = 0;
        if (read == 1) { bad = 0; out = 1; }
        else if (read == -1) { out = listed ? 1 : 0; }
        else
        {
            bad = bad_n + 1 < 3 ? bad_n + 1 : 3;
            out = (listed && bad < 3) ? 1 : 0;
        }
        if (bad_out) *bad_out = bad;
        return out;
    }
}
