cmake_minimum_required(VERSION 3.10)
project(OpenNeoUAMultiplayerTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

# Checks must not depend on the build type: the tests use REQUIRE/Require
# (require.h), which keep checking under NDEBUG. The first test proves that by
# compiling a deliberately failing check with NDEBUG; the second keeps the
# standard assert macro, which NDEBUG removes, out of the test sources.
add_executable(test_require_ndebug test_require_ndebug.cpp)
target_compile_definitions(test_require_ndebug PRIVATE NDEBUG)
add_test(NAME require_ndebug COMMAND test_require_ndebug)
set_tests_properties(require_ndebug PROPERTIES
    PASS_REGULAR_EXPRESSION "FAIL .*test_require_ndebug\\.cpp:[0-9]+: evaluated == 2")
add_test(NAME tests_survive_ndebug COMMAND ${CMAKE_COMMAND}
    -DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_assert.cmake)

add_executable(test_agent_protocol test_agent_protocol.cpp)
add_test(NAME agent_protocol COMMAND test_agent_protocol)
add_executable(test_agent_observation test_agent_observation.cpp)
add_test(NAME agent_observation COMMAND test_agent_observation)

option(OPENNEOUA_AGENT_BRIDGE_TEST "Test native agent files and actual SDL events" OFF)
option(OPENNEOUA_AGENT_MOUSE_TEST "Also test positioned clicks in a hidden native OpenGL window" OFF)
if(OPENNEOUA_AGENT_BRIDGE_TEST)
    find_package(PkgConfig REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    pkg_check_modules(AGENT_SDL REQUIRED IMPORTED_TARGET sdl2)
    add_executable(test_agent_bridge test_agent_bridge.cpp ../src/agent/bridge.cpp)
    target_link_libraries(test_agent_bridge PRIVATE PkgConfig::AGENT_SDL OpenGL::GL)
    add_test(NAME agent_bridge COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_agent_bridge.py $<TARGET_FILE:test_agent_bridge> ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(agent_bridge PROPERTIES TIMEOUT 25)
    if(OPENNEOUA_AGENT_MOUSE_TEST)
        add_executable(test_agent_mouse test_agent_mouse.cpp ../src/agent/bridge.cpp)
        target_link_libraries(test_agent_mouse PRIVATE PkgConfig::AGENT_SDL OpenGL::GL)
        add_test(NAME agent_mouse COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/run_agent_bridge.py $<TARGET_FILE:test_agent_mouse> ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(agent_mouse PROPERTIES TIMEOUT 25)
    endif()
endif()
add_executable(test_ratelimit test_ratelimit.cpp)
add_test(NAME ratelimit COMMAND test_ratelimit)
add_executable(test_delta_wire test_delta_wire.cpp)
add_test(NAME delta_wire COMMAND test_delta_wire)
add_executable(test_net_tick test_net_tick.cpp)
add_test(NAME net_tick COMMAND test_net_tick)
add_executable(test_yw_wire test_yw_wire.cpp)
add_test(NAME yw_wire COMMAND test_yw_wire)
add_executable(test_net_protocol test_net_protocol.cpp)
add_test(NAME net_protocol COMMAND test_net_protocol)
add_executable(test_net_lobby test_net_lobby.cpp)
add_test(NAME net_lobby COMMAND test_net_lobby)
add_executable(test_net_update_gate test_net_update_gate.cpp)
add_test(NAME net_update_gate COMMAND test_net_update_gate)
add_executable(test_net_messages test_net_messages.cpp)
target_include_directories(test_net_messages PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME net_messages COMMAND test_net_messages)
add_executable(test_net_factions test_net_factions.cpp)
add_test(NAME net_factions COMMAND test_net_factions)
add_executable(test_net_restore test_net_restore.cpp)
add_test(NAME net_restore COMMAND test_net_restore)
add_executable(test_net_building_units test_net_building_units.cpp)
add_test(NAME net_building_units COMMAND test_net_building_units)
add_executable(test_world_fork test_world_fork.cpp)
add_test(NAME world_fork COMMAND test_world_fork)
add_executable(test_net_bot test_net_bot.cpp)
add_test(NAME net_bot COMMAND test_net_bot)
add_executable(test_fight_motivation test_fight_motivation.cpp)
add_test(NAME fight_motivation COMMAND test_fight_motivation)
add_executable(test_net_lobby_flags test_net_lobby_flags.cpp)
add_test(NAME net_lobby_flags COMMAND test_net_lobby_flags)
add_executable(test_speech_factions test_speech_factions.cpp ../src/world/speech.cpp)
add_test(NAME speech_factions COMMAND test_speech_factions)
add_executable(test_net_connfail test_net_connfail.cpp)
add_test(NAME net_connfail COMMAND test_net_connfail)

option(OPENNEOUA_ENET_TEST "Test ENet transport loopback (vendored ENet, no SDL)" OFF)
if(OPENNEOUA_ENET_TEST)
    set(ENET_SOURCES ../src/third_party/enet/callbacks.c ../src/third_party/enet/compress.c
        ../src/third_party/enet/host.c ../src/third_party/enet/list.c
        ../src/third_party/enet/packet.c ../src/third_party/enet/peer.c
        ../src/third_party/enet/protocol.c)
    if(WIN32)
        list(APPEND ENET_SOURCES ../src/third_party/enet/win32.c)
    else()
        list(APPEND ENET_SOURCES ../src/third_party/enet/unix.c)
    endif()
    set_source_files_properties(${ENET_SOURCES} PROPERTIES LANGUAGE CXX)
    add_executable(test_enet_transport test_enet_transport.cpp
        ../src/netlib/enet_transport.cpp ${ENET_SOURCES})
    target_include_directories(test_enet_transport PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../src/third_party/enet/include)
    if(WIN32)
        target_link_libraries(test_enet_transport PRIVATE ws2_32 winmm)
    endif()
    add_test(NAME enet_transport COMMAND test_enet_transport)
    set_tests_properties(enet_transport PROPERTIES TIMEOUT 60)
endif()

option(OPENNEOUA_ENET_ZND_TEST "Test ZNDEnet adapter loopback (requires SDL2/SDL2_net + vendored ENet)" OFF)
if(OPENNEOUA_ENET_ZND_TEST)
    find_package(PkgConfig REQUIRED)
    if(NOT TARGET PkgConfig::NET_SDL)
        pkg_check_modules(NET_SDL REQUIRED IMPORTED_TARGET sdl2 SDL2_net)
    endif()
    set(ENET_ZND_SOURCES ../src/third_party/enet/callbacks.c ../src/third_party/enet/compress.c
        ../src/third_party/enet/host.c ../src/third_party/enet/list.c
        ../src/third_party/enet/packet.c ../src/third_party/enet/peer.c
        ../src/third_party/enet/protocol.c)
    if(WIN32)
        list(APPEND ENET_ZND_SOURCES ../src/third_party/enet/win32.c)
    else()
        list(APPEND ENET_ZND_SOURCES ../src/third_party/enet/unix.c)
    endif()
    set_source_files_properties(${ENET_ZND_SOURCES} PROPERTIES LANGUAGE CXX)
    add_executable(test_enet_znd test_enet_znd.cpp
        ../src/netlib/zndNet.cpp ../src/netlib/enet_transport.cpp ../src/netlib/enet_znd.cpp
        ${ENET_ZND_SOURCES})
    target_include_directories(test_enet_znd PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../src/third_party/enet/include)
    target_link_libraries(test_enet_znd PRIVATE PkgConfig::NET_SDL)
    if(WIN32)
        target_link_libraries(test_enet_znd PRIVATE ws2_32 winmm)
    endif()
    add_test(NAME enet_znd COMMAND test_enet_znd)
    set_tests_properties(enet_znd PROPERTIES TIMEOUT 60)

    # Three-party star relay, directed sends and duplicate-callsign refusal.
    add_executable(test_enet_znd_relay test_enet_znd_relay.cpp
        ../src/netlib/zndNet.cpp ../src/netlib/enet_transport.cpp ../src/netlib/enet_znd.cpp
        ${ENET_ZND_SOURCES})
    target_include_directories(test_enet_znd_relay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../src/third_party/enet/include)
    target_link_libraries(test_enet_znd_relay PRIVATE PkgConfig::NET_SDL)
    if(WIN32)
        target_link_libraries(test_enet_znd_relay PRIVATE ws2_32 winmm)
    endif()
    add_test(NAME enet_znd_relay COMMAND test_enet_znd_relay)
    set_tests_properties(enet_znd_relay PROPERTIES TIMEOUT 90)
endif()

option(OPENNEOUA_TRANSPORT_TEST "Test six real loopback UDP endpoints (requires SDL2/SDL2_net)" OFF)
if(OPENNEOUA_TRANSPORT_TEST)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NET_SDL REQUIRED IMPORTED_TARGET sdl2 SDL2_net)
    add_executable(test_net_transport test_net_transport.cpp
        ../src/netlib/zndNet.cpp ../src/netlib/client.cpp ../src/netlib/client_threads.cpp
        ../src/netlib/single.cpp ../src/netlib/single_threads.cpp)
    target_include_directories(test_net_transport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(test_net_transport PRIVATE PkgConfig::NET_SDL)
    add_test(NAME net_transport COMMAND test_net_transport)
    set_tests_properties(net_transport PROPERTIES TIMEOUT 180)
    add_executable(test_net_lifecycle test_net_lifecycle.cpp
        ../src/netlib/zndNet.cpp ../src/netlib/client.cpp ../src/netlib/client_threads.cpp
        ../src/netlib/single.cpp ../src/netlib/single_threads.cpp)
    target_include_directories(test_net_lifecycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(test_net_lifecycle PRIVATE PkgConfig::NET_SDL)
    foreach(lifecycle_case orderly refusal timeout disconnect backlog)
        add_test(NAME net_lifecycle_${lifecycle_case} COMMAND test_net_lifecycle ${lifecycle_case})
        set_tests_properties(net_lifecycle_${lifecycle_case} PROPERTIES TIMEOUT 35)
    endforeach()
endif()
