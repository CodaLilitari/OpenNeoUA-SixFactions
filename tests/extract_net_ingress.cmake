# Configure-time extraction of real production ingress, not a replacement policy.
# Every source boundary is unique and checked. Generated files belong to binarydir.
get_filename_component(_ingress_repo "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(_ingress_binary "${CMAKE_CURRENT_BINARY_DIR}" ABSOLUTE)
string(FIND "${_ingress_binary}/" "${_ingress_repo}/" _ingress_in_source)
if(_ingress_in_source EQUAL 0)
    message(FATAL_ERROR "net ingress test: build directory must be outside source checkout")
endif()
set(NET_INGRESS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/net_ingress_generated")
file(MAKE_DIRECTORY "${NET_INGRESS_GENERATED_DIR}")

function(_ingress_read relative output)
    set(path "${_ingress_repo}/src/${relative}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "net ingress extraction: missing source ${path}")
    endif()
    file(READ "${path}" raw)
    file(SHA256 "${path}" raw_hash)
    string(REPLACE "\r\n" "\n" normalized "${raw}")
    string(FIND "${normalized}" "\r" bare_cr)
    if(NOT bare_cr EQUAL -1)
        message(FATAL_ERROR "net ingress extraction: unexpected bare CR in ${relative}")
    endif()
    string(SHA256 normalized_hash "${normalized}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(APPEND "${NET_INGRESS_GENERATED_DIR}/EXTRACTION.txt"
        "source=${relative}\nraw_sha256=${raw_hash}\nnormalized_sha256=${normalized_hash}\n")
    set(${output} "${normalized}" PARENT_SCOPE)
endfunction()

function(_ingress_unique text_name marker output)
    string(FIND "${${text_name}}" "${marker}" first)
    if(first EQUAL -1)
        message(FATAL_ERROR "net ingress extraction: missing anchor [${marker}]")
    endif()
    string(LENGTH "${marker}" marker_size)
    math(EXPR after "${first}+${marker_size}")
    string(SUBSTRING "${${text_name}}" ${after} -1 tail)
    string(FIND "${tail}" "${marker}" second)
    if(NOT second EQUAL -1)
        message(FATAL_ERROR "net ingress extraction: duplicate anchor [${marker}]")
    endif()
    set(${output} ${first} PARENT_SCOPE)
endfunction()

function(_ingress_slice text_name start_marker end_marker include_end output)
    _ingress_unique(${text_name} "${start_marker}" start)
    _ingress_unique(${text_name} "${end_marker}" end)
    if(NOT start LESS end)
        message(FATAL_ERROR "net ingress extraction: unordered anchors [${start_marker}] / [${end_marker}]")
    endif()
    if(include_end)
        string(LENGTH "${end_marker}" end_size)
        math(EXPR end "${end}+${end_size}")
    endif()
    math(EXPR size "${end}-${start}")
    string(SUBSTRING "${${text_name}}" ${start} ${size} fragment)
    string(SHA256 fragment_hash "${fragment}")
    file(APPEND "${NET_INGRESS_GENERATED_DIR}/EXTRACTION.txt"
        "fragment=${output}\nstart_anchor=${start_marker}\nend_anchor=${end_marker}\nnormalized_fragment_sha256=${fragment_hash}\n")
    set(${output} "${fragment}" PARENT_SCOPE)
endfunction()

file(WRITE "${NET_INGRESS_GENERATED_DIR}/EXTRACTION.txt"
    "format=1\nnormalization=CRLF to LF only; raw hashes retained\n"
    "scope=exact helper+validator+handler prefix through first dispatch switch; downstream gameplay omitted\n"
    "fixture=only world presence/counts/grid and Common::Point coordinates; logging sink\n")
_ingress_read("game/yw_net.cpp" net_source)
_ingress_read("game/yw.h" user_source)
_ingress_read("platform/windp.h" provider_source)
_ingress_read("common/utils.cpp" utility_source)
# Pin included production headers too; normal compiler dependency tracking also applies.
foreach(header game/yw_net_msgs.h platform/windp_events.h world/net_update_gate.h
        world/net_factions.h world/consts.h common/matrix.h common/vectors.h
        common/types.h common/listnode.h)
    _ingress_read("${header}" unused_header)
endforeach()

_ingress_unique(net_source "static bool yw_netLevelLoaded(" helper_position)
_ingress_unique(net_source "static bool yw_netValidateNormMsg(" validator_position)
_ingress_unique(net_source "size_t yw_handleNormMsg(" handler_position)
_ingress_unique(net_source "    switch (msgID)\n    {" switch_position)
if(NOT (helper_position LESS validator_position AND validator_position LESS handler_position
        AND handler_position LESS switch_position))
    message(FATAL_ERROR "net ingress extraction: helper/validator/handler/switch order changed")
endif()
_ingress_slice(net_source "static bool yw_netLevelLoaded(" "    switch (msgID)\n    {" TRUE prefix)
_ingress_slice(user_source "    struct TNetPlayerLobbyData\n" "\n    struct TNetPlayerData\n" FALSE lobby_record)
_ingress_slice(user_source "    struct TNetPlayerData\n" "\n    std::array<TNetPlayerData," FALSE player_record)
_ingress_slice(provider_source "struct windp_recvMsg\n" "\nstruct windp_arg87\n" FALSE receive_record)
_ingress_slice(utility_source "int StriCmp(const std::string &a, const std::string &b)\n"
    "\nbool StrGetBool(" FALSE compare_function)

file(WRITE "${NET_INGRESS_GENERATED_DIR}/net_ingress_types.inc"
    "// Generated exact normalized production records; do not hand edit.\nclass UserData { public:\n"
    "${lobby_record}${player_record}"
    "std::array<TNetPlayerData,World::CVFractionsCount> netPlayers;\n"
    "std::array<TNetPlayerLobbyData,World::CVMaxNetPlayers> lobbyPlayers;\n};\n"
    "${receive_record}${compare_function}")
file(WRITE "${NET_INGRESS_GENERATED_DIR}/net_ingress_prefix.inc"
    "#include \"net_ingress_fixture.hpp\"\n${prefix}\n"
    "// TEST ONLY: mark switch entry, omitting all downstream gameplay cases.\n"
    "default: ++dispatch_marker; break;\n}\nreturn szmsg;\n}\n")
foreach(generated net_ingress_types.inc net_ingress_prefix.inc)
    file(SHA256 "${NET_INGRESS_GENERATED_DIR}/${generated}" generated_hash)
    file(APPEND "${NET_INGRESS_GENERATED_DIR}/EXTRACTION.txt"
        "generated=${generated}\ngenerated_sha256=${generated_hash}\n")
endforeach()
