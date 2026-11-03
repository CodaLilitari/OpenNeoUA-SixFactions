# Fails when a C++ test source uses the standard assert macro. Under NDEBUG
# that macro expands to nothing: the check and any call written inside it
# disappear and the test passes without testing. Use REQUIRE or Require from
# require.h instead. Run as: cmake -DTEST_SOURCE_DIR=<tests> -P check_no_assert.cmake
if(NOT TEST_SOURCE_DIR)
    message(FATAL_ERROR "TEST_SOURCE_DIR is not set")
endif()
file(GLOB sources "${TEST_SOURCE_DIR}/*.cpp" "${TEST_SOURCE_DIR}/*.h")
set(offenders "")
foreach(source IN LISTS sources)
    file(STRINGS "${source}" uses REGEX "(^|[^_A-Za-z0-9])assert[ \t]*\\(")
    if(uses)
        get_filename_component(name "${source}" NAME)
        list(APPEND offenders "${name}")
    endif()
endforeach()
list(LENGTH sources checked)
if(checked EQUAL 0)
    message(FATAL_ERROR "no test sources found in ${TEST_SOURCE_DIR}")
endif()
if(offenders)
    message(FATAL_ERROR "assert() compiles out under NDEBUG; use REQUIRE/Require (require.h) in: ${offenders}")
endif()
message(STATUS "${checked} test sources use checks that survive NDEBUG")
