// Proves that the test checks survive NDEBUG: this file is compiled with
// NDEBUG defined, and the CTest entry passes only when the deliberately false
// check below stops the program with its FAIL line.
#ifndef NDEBUG
#define NDEBUG
#endif
#include "require.h"

int main()
{
    int evaluated = 0;
    REQUIRE(++evaluated == 1);                      // runs, exactly once
    Require(evaluated == 1, "a check evaluates its condition exactly once");
    REQUIRE(evaluated == 2);                        // deliberately false
    return 0;                                       // reached only if checks compiled out
}
