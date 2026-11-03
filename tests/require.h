// Test checks that never compile out.
//
// The standard assert macro expands to nothing under NDEBUG, so a test tree
// configured as Release would pass without checking anything, and any call
// written inside it would not even run. REQUIRE and Require evaluate their
// condition exactly once in every build type and stop the test with a message
// on the first failure. tests/check_no_assert.cmake keeps the bare macro out
// of the test sources, and test_require_ndebug proves these checks still fail
// when NDEBUG is defined.
#ifndef OPENNEOUA_TESTS_REQUIRE_H
#define OPENNEOUA_TESTS_REQUIRE_H

#include <cstdio>
#include <cstdlib>

namespace TestSupport
{
[[noreturn]] inline void FailExpression(const char *file, int line, const char *expression)
{
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    std::fflush(stderr);
    std::exit(1);
}

[[noreturn]] inline void FailMessage(const char *message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::fflush(stderr);
    std::exit(1);
}
}

// REQUIRE(condition): stops the test when the condition is false and reports
// the expression. Variadic so that template arguments may contain commas.
#define REQUIRE(...)                                                          \
    ((__VA_ARGS__) ? static_cast<void>(0)                                     \
                   : ::TestSupport::FailExpression(__FILE__, __LINE__, #__VA_ARGS__))

// Require(condition, message): the same check with a written explanation.
inline void Require(bool condition, const char *message)
{
    if (!condition)
        ::TestSupport::FailMessage(message);
}

#endif
