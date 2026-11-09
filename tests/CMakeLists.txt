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
add_executable(test_net_lobby_identity test_net_lobby_identity.cpp)
target_compile_definitions(test_net_lobby_identity PRIVATE NDEBUG)
add_test(NAME net_lobby_identity COMMAND test_net_lobby_identity)
add_executable(test_windp_roster test_windp_roster.cpp)
add_test(NAME windp_roster COMMAND test_windp_roster)
add_executable(test_windp_resume test_windp_resume.cpp)
add_test(NAME windp_resume COMMAND test_windp_resume)
add_executable(test_net_update_gate test_net_update_gate.cpp)
add_test(NAME net_update_gate COMMAND test_net_update_gate)
add_executable(test_net_messages test_net_messages.cpp)
target_include_directories(test_net_messages PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME net_messages COMMAND test_net_messages)
# Exact production pre-dispatch prefix, with a deliberately minimal world fixture.
# Configure-time extraction and the native C++ test require neither Python nor SDL.
include(${CMAKE_CURRENT_SOURCE_DIR}/extract_net_ingress.cmake)
add_executable(test_net_ingress_phase test_net_ingress_phase.cpp)
target_include_directories(test_net_ingress_phase PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR} ${NET_INGRESS_GENERATED_DIR})
target_compile_definitions(test_net_ingress_phase PRIVATE NDEBUG)
add_test(NAME net_ingress_phase COMMAND test_net_ingress_phase)
add_test(NAME net_ingress_phase_checks_active COMMAND test_net_ingress_phase --verify-failing-check)
set_tests_properties(net_ingress_phase PROPERTIES TIMEOUT 10)
set_tests_properties(net_ingress_phase_checks_active PROPERTIES TIMEOUT 10 WILL_FAIL TRUE)
# Real queued outer/lifecycle and corruption paths; NORMAL handlers remain a
# dispatch marker. This C++14 target requires neither Python nor SDL at runtime.
add_executable(test_net_identity_outer test_net_identity_outer.cpp)
target_include_directories(test_net_identity_outer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR} ${NET_INGRESS_GENERATED_DIR})
target_compile_definitions(test_net_identity_outer PRIVATE NDEBUG)
add_test(NAME net_identity_outer COMMAND test_net_identity_outer)
add_test(NAME net_identity_outer_terminal COMMAND test_net_identity_outer --terminal-only)
add_test(NAME net_identity_outer_checks_active COMMAND test_net_identity_outer --verify-failing-check)
set_tests_properties(net_identity_outer net_identity_outer_terminal PROPERTIES TIMEOUT 10)
set_tests_properties(net_identity_outer_checks_active PROPERTIES TIMEOUT 10 WILL_FAIL TRUE)
include(${CMAKE_CURRENT_SOURCE_DIR}/extract_net_syncgm.cmake)
add_executable(test_net_syncgm test_net_syncgm.cpp)
target_include_directories(test_net_syncgm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR} ${NET_INGRESS_GENERATED_DIR})
target_compile_definitions(test_net_syncgm PRIVATE NDEBUG)
add_test(NAME net_syncgm COMMAND test_net_syncgm)
add_test(NAME net_syncgm_checks_active COMMAND test_net_syncgm --verify-failing-check)
set_tests_properties(net_syncgm PROPERTIES TIMEOUT 10)
set_tests_properties(net_syncgm_checks_active PROPERTIES TIMEOUT 10 WILL_FAIL TRUE)
# Complete actual provider methods with fake clocks/transports. These are
# boundary-fixture scenarios, distinct from the real endpoint tests below.
include(${CMAKE_CURRENT_SOURCE_DIR}/extract_provider_identity.cmake)
add_executable(test_provider_identity test_provider_identity.cpp)
target_include_directories(test_provider_identity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR} ${PROVIDER_IDENTITY_GENERATED_DIR})
target_compile_definitions(test_provider_identity PRIVATE NDEBUG)
set(_provider_identity_scenarios
    barrier fresh events malformed metadata explicit kick superseded unarmed session_end
    timeout refusal_retry refusal_final wrong_id wrong_session disable configuration_failure
    repeat_loss enable_waiting enable_connecting late_reclaim fresh_malformed fresh_no_token reset host
    identity_client identity_host identity_data identity_lifecycle identity_stale_data
    identity_stale_terminal identity_missing_session identity_host_migration identity_session_change
    identity_self_change identity_roster_unavailable identity_send_kick identity_loss
    identity_invalid_lifecycle identity_rejected_lifecycle identity_invalid_reset identity_invalid_reclaim
    identity_stale_refusal identity_rehost identity_offline_retained identity_host_offline identity_host_end)
list(LENGTH _provider_identity_scenarios _provider_scenario_count)
if(NOT _provider_scenario_count EQUAL 47)
    message(FATAL_ERROR "provider identity test: expected exactly 47 scenarios per fake adapter mode")
endif()
foreach(mode znd enet)
    foreach(scenario IN LISTS _provider_identity_scenarios)
        add_test(NAME provider_identity_${mode}_${scenario} COMMAND test_provider_identity ${mode} ${scenario})
        set_tests_properties(provider_identity_${mode}_${scenario} PROPERTIES TIMEOUT 10)
    endforeach()
endforeach()
add_test(NAME provider_identity_checks_active COMMAND test_provider_identity --verify-failing-check)
set_tests_properties(provider_identity_checks_active PROPERTIES TIMEOUT 10 WILL_FAIL TRUE)
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
    add_executable(test_transport_repair test_transport_repair.cpp
        ../src/netlib/zndNet.cpp ../src/netlib/client.cpp ../src/netlib/client_threads.cpp
        ../src/netlib/single.cpp ../src/netlib/single_threads.cpp)
    target_include_directories(test_transport_repair PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(test_transport_repair PRIVATE PkgConfig::NET_SDL)
    add_test(NAME wp1_znd_roster_repair COMMAND test_transport_repair)
    set_tests_properties(wp1_znd_roster_repair PROPERTIES TIMEOUT 35)
endif()

if(OPENNEOUA_TRANSPORT_TEST AND OPENNEOUA_ENET_ZND_TEST)
    add_executable(test_wp1 test_wp1.cpp ../src/netlib/zndNet.cpp
        ../src/netlib/client.cpp ../src/netlib/client_threads.cpp
        ../src/netlib/single.cpp ../src/netlib/single_threads.cpp
        ../src/netlib/enet_transport.cpp ../src/netlib/enet_znd.cpp ${ENET_ZND_SOURCES})
    target_include_directories(test_wp1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../src/third_party/enet/include)
    target_link_libraries(test_wp1 PRIVATE PkgConfig::NET_SDL)
    if(WIN32)
        target_link_libraries(test_wp1 PRIVATE ws2_32 winmm)
    endif()
    add_test(NAME wp1_znd_reservations COMMAND test_wp1 znd)
    add_test(NAME wp1_enet_reservations COMMAND test_wp1 enet)
    set_tests_properties(wp1_znd_reservations wp1_enet_reservations PROPERTIES TIMEOUT 55)
    foreach(wp1_transport znd enet)
        foreach(wp1_case migration terminal expiry awaykick close unknown backlog eventslots initialroster countdown eventsession)
            add_test(NAME wp1_${wp1_transport}_${wp1_case} COMMAND test_wp1 ${wp1_transport}_${wp1_case})
            set_tests_properties(wp1_${wp1_transport}_${wp1_case} PROPERTIES TIMEOUT 35)
        endforeach()
    endforeach()

endif()

add_executable(test_transport_control test_transport_control.cpp)
add_test(NAME transport_control COMMAND test_transport_control)
if(TARGET test_wp1)
    add_test(NAME wp1_znd_hostrestart COMMAND test_wp1 hostrestart)
    set_tests_properties(wp1_znd_hostrestart PROPERTIES TIMEOUT 15)
    add_test(NAME wp1_znd_flood COMMAND test_wp1 flood)
    set_tests_properties(wp1_znd_flood PROPERTIES TIMEOUT 10)
    add_test(NAME wp1_znd_binding COMMAND test_wp1 binding)
    set_tests_properties(wp1_znd_binding PROPERTIES TIMEOUT 10)
    add_test(NAME wp1_znd_retry COMMAND test_wp1 retry)
    set_tests_properties(wp1_znd_retry PROPERTIES TIMEOUT 15)
endif()

if(TARGET test_wp1)
    add_test(NAME wp1_enet_capacityone COMMAND test_wp1 capacityone)
    foreach(t znd enet)
        add_test(NAME wp1_${t}_casenames COMMAND test_wp1 ${t}_casenames)
        set_tests_properties(wp1_${t}_casenames PROPERTIES TIMEOUT 12)
    endforeach()
    foreach(c size budget duplicates count rawqueue inconsistent)
        add_test(NAME wp1_znd_multipart_${c} COMMAND test_wp1 multipart_${c})
        set_tests_properties(wp1_znd_multipart_${c} PROPERTIES TIMEOUT 12)
    endforeach()
    set_tests_properties(wp1_enet_capacityone PROPERTIES TIMEOUT 10)
endif()

if(TARGET test_wp1)
    foreach(c kickrace pollbound pending)
        add_test(NAME wp1_enet_${c} COMMAND test_wp1 ${c})
        set_tests_properties(wp1_enet_${c} PROPERTIES TIMEOUT 20)
    endforeach()
endif()

if(TARGET test_wp1)
    add_test(NAME wp1_enet_sessionincarnation COMMAND test_wp1 sessionincarnation)
    add_test(NAME wp1_znd_partialcustody COMMAND test_wp1 partialcustody)
    set_tests_properties(wp1_enet_sessionincarnation wp1_znd_partialcustody PROPERTIES TIMEOUT 12)
    foreach(c late_acceptance retry server_disconnect server_countdown after_timeout pending_system)
        add_test(NAME wp1_znd_cancel_${c} COMMAND test_wp1 cancel_${c})
        set_tests_properties(wp1_znd_cancel_${c} PROPERTIES TIMEOUT 18)
    endforeach()
endif()
if(TARGET test_wp1)
    add_executable(test_transport_control_ingress test_transport_control_ingress.cpp
        ../src/netlib/zndNet.cpp ../src/netlib/client.cpp ../src/netlib/client_threads.cpp
        ../src/netlib/single.cpp ../src/netlib/single_threads.cpp
        ../src/netlib/enet_transport.cpp ../src/netlib/enet_znd.cpp ${ENET_ZND_SOURCES})
    target_include_directories(test_transport_control_ingress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../src/third_party/enet/include)
    target_link_libraries(test_transport_control_ingress PRIVATE PkgConfig::NET_SDL)
    if(WIN32)
        target_link_libraries(test_transport_control_ingress PRIVATE ws2_32 winmm)
    endif()
    foreach(t znd enet)
        foreach(c missing malformed history)
            add_test(NAME wp1_${t}_control_${c} COMMAND test_transport_control_ingress ${t}_${c})
            set_tests_properties(wp1_${t}_control_${c} PROPERTIES TIMEOUT 15)
        endforeach()
    endforeach()
endif()

if(TARGET test_wp1)
    add_executable(test_transport_session_names test_transport_session_names.cpp
        ../src/netlib/zndNet.cpp ../src/netlib/client.cpp ../src/netlib/client_threads.cpp
        ../src/netlib/single.cpp ../src/netlib/single_threads.cpp
        ../src/netlib/enet_transport.cpp ../src/netlib/enet_znd.cpp ${ENET_ZND_SOURCES})
    target_include_directories(test_transport_session_names PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../src/third_party/enet/include)
    target_compile_definitions(test_transport_session_names PRIVATE NDEBUG)
    target_link_libraries(test_transport_session_names PRIVATE PkgConfig::NET_SDL)
    if(WIN32)
        target_link_libraries(test_transport_session_names PRIVATE ws2_32 winmm)
    endif()
    foreach(t znd enet)
        foreach(c 32 33 frontend 255 empty nul 256 rehost_empty rehost_nul rehost_256)
            add_test(NAME session_name_${t}_${c} COMMAND test_transport_session_names ${t}_${c})
            set_tests_properties(session_name_${t}_${c} PROPERTIES TIMEOUT 25)
        endforeach()
    endforeach()
    foreach(c codec raw_accept_empty raw_accept_nul raw_accept_256 raw_roster_first raw_accept_first raw_changed_accept)
        add_test(NAME session_name_${c} COMMAND test_transport_session_names ${c})
        set_tests_properties(session_name_${c} PROPERTIES TIMEOUT 25)
    endforeach()
    add_test(NAME session_name_checks_active COMMAND test_transport_session_names intentional_failure)
    set_tests_properties(session_name_checks_active PROPERTIES TIMEOUT 25 WILL_FAIL TRUE)
endif()
