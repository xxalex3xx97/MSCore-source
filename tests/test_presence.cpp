// FILE: tests/test_presence.cpp
// WHAT: native unit test for presence_step (cpp/MSCore/msc_presence.hpp), API 15 MSCore_presence_step. Pure C++, no game headers.
// RUN: scripts\test-native.ps1 (cl on Windows, g++ fallback). Exit 1 on any failed CHECK.
#include <cstdio>
#include "msc_presence.hpp"

static int g_fail = 0;
static int g_run = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_run;                                                                 \
        if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

// one row: (bad_n, listed, read) -> (out, bad)
static void row(int bad_n, int listed, int read, int want_out, int want_bad)
{
    int bad = -99;
    const int out = presence_step(bad_n, listed, read, &bad);
    CHECK(out == want_out);
    CHECK(bad == want_bad);
}

int main()
{
    row(0, 0, 1, 1, 0);
    row(0, 0, 0, 0, 1);
    row(0, 0, -1, 0, 0);
    row(0, 1, 1, 1, 0);
    row(0, 1, 0, 1, 1);
    row(1, 1, 0, 1, 2);
    row(2, 1, 0, 0, 3);
    row(2, 1, -1, 1, 2);
    row(5, 1, 1, 1, 0);
    row(3, 0, 0, 0, 3);
    row(3, 1, 0, 0, 3);

    // read other than 0 or 1 = unknown: hold like -1
    row(0, 1, 2, 1, 0);
    row(0, 0, -2, 0, 0);
    row(1, 1, 7, 1, 1);

    // null bad_out must not crash
    CHECK(presence_step(0, 1, 0, nullptr) == 1);
    CHECK(presence_step(2, 1, 0, nullptr) == 0);

    std::printf("test_presence: %d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
