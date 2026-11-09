# Exact provider algorithms with boundary-only clock/transport fixtures.
# No interpreter is required by configure, compilation or CTest.
get_filename_component(_provider_repo "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(_provider_binary "${CMAKE_CURRENT_BINARY_DIR}" ABSOLUTE)
string(FIND "${_provider_binary}/" "${_provider_repo}/" _provider_in_source)
if(_provider_in_source EQUAL 0)
    message(FATAL_ERROR "provider identity test: build directory must be outside source checkout")
endif()
set(PROVIDER_IDENTITY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/provider_identity_generated")
file(MAKE_DIRECTORY "${PROVIDER_IDENTITY_GENERATED_DIR}")
file(WRITE "${PROVIDER_IDENTITY_GENERATED_DIR}/EXTRACTION.txt"
    "format=1\nnormalization=CRLF to LF only; raw hashes retained\n"
    "scope=50 complete production provider methods; fake boundary clock/transports; no real sockets/threads/engine\n"
    "declaration_transforms=omit includes; remove virtual and override; expose protected\n")

function(_provider_read relative output)
    set(path "${_provider_repo}/src/${relative}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "provider extraction: missing source ${relative}")
    endif()
    file(READ "${path}" raw)
    file(SHA256 "${path}" raw_hash)
    string(REPLACE "\r\n" "\n" normalized "${raw}")
    string(FIND "${normalized}" "\r" bare_cr)
    if(NOT bare_cr EQUAL -1)
        message(FATAL_ERROR "provider extraction: unexpected bare CR in ${relative}")
    endif()
    string(SHA256 normalized_hash "${normalized}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(APPEND "${PROVIDER_IDENTITY_GENERATED_DIR}/EXTRACTION.txt"
        "source=${relative}\nraw_sha256=${raw_hash}\nnormalized_sha256=${normalized_hash}\n")
    set(${output} "${normalized}" PARENT_SCOPE)
endfunction()

function(_provider_unique text marker output)
    string(FIND "${text}" "${marker}" first)
    if(first EQUAL -1)
        message(FATAL_ERROR "provider extraction: missing anchor [${marker}]")
    endif()
    string(LENGTH "${marker}" size)
    math(EXPR after "${first}+${size}")
    string(SUBSTRING "${text}" ${after} -1 tail)
    string(FIND "${tail}" "${marker}" duplicate)
    if(NOT duplicate EQUAL -1)
        message(FATAL_ERROR "provider extraction: duplicate anchor [${marker}]")
    endif()
    set(${output} ${first} PARENT_SCOPE)
endfunction()

function(_provider_method name output)
    # Find definitions in code only. Literals/comments are removed together in
    # lexical order, so comment-looking bytes inside strings are not mistaken
    # for comments. Actual extraction below uses untouched normalized source.
    string(REGEX MATCHALL "(^|\n)[A-Za-z_][^\n;{}]*[ \t]+NC_STACK_windp::${name}[ \t]*\\("
        matches "${_provider_masked}")
    list(LENGTH matches count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "provider extraction: expected one definition of ${name}, found ${count}")
    endif()
    list(GET matches 0 signature)
    string(REGEX REPLACE "^\n" "" signature "${signature}")
    _provider_unique("${_provider_cpp}" "${signature}" start)
    string(SUBSTRING "${_provider_cpp}" ${start} -1 tail)
    string(FIND "${tail}" "{" opening)
    if(opening EQUAL -1)
        message(FATAL_ERROR "provider extraction: missing body for ${name}")
    endif()
    string(SUBSTRING "${tail}" 0 ${opening} header)
    # Current definitions have one signature line followed by the opening
    # brace. Fail closed if a default-expression/lambda/multiline header needs
    # a more capable signature parser; do not silently select its inner body.
    if(NOT header MATCHES "^[^\n;{}]+\n[ \t]*$")
        message(FATAL_ERROR "provider extraction: unsupported signature layout for ${name}")
    endif()
    string(LENGTH "${tail}" length)
    set(i ${opening})
    set(depth 0)
    set(state code)
    set(end -1)
    while(i LESS length)
        string(SUBSTRING "${tail}" ${i} 1 ch)
        math(EXPR next "${i}+1")
        set(pair "")
        if(next LESS length)
            string(SUBSTRING "${tail}" ${i} 2 pair)
        endif()
        if(state STREQUAL line)
            if(ch STREQUAL "\n")
                set(state code)
            endif()
        elseif(state STREQUAL block)
            if(pair STREQUAL "*/")
                set(state code)
                math(EXPR i "${i}+1")
            endif()
        elseif(state STREQUAL string OR state STREQUAL character)
            if(ch STREQUAL "\\")
                math(EXPR i "${i}+1")
            elseif((state STREQUAL string AND ch STREQUAL "\"") OR
                   (state STREQUAL character AND ch STREQUAL "'"))
                set(state code)
            endif()
        elseif(pair STREQUAL "//")
            set(state line)
            math(EXPR i "${i}+1")
        elseif(pair STREQUAL "/*")
            set(state block)
            math(EXPR i "${i}+1")
        elseif(pair STREQUAL "R\"")
            message(FATAL_ERROR "provider extraction: unsupported raw string in ${name}")
        elseif(ch STREQUAL "\"")
            set(state string)
        elseif(ch STREQUAL "'")
            set(state character)
        elseif(ch STREQUAL "{")
            math(EXPR depth "${depth}+1")
        elseif(ch STREQUAL "}")
            math(EXPR depth "${depth}-1")
            if(depth EQUAL 0)
                math(EXPR end "${i}+1")
                break()
            endif()
        endif()
        math(EXPR i "${i}+1")
    endwhile()
    if(end EQUAL -1 OR NOT state STREQUAL code)
        message(FATAL_ERROR "provider extraction: unclosed body for ${name}")
    endif()
    string(SUBSTRING "${tail}" 0 ${end} body)
    string(SHA256 body_hash "${body}")
    file(APPEND "${PROVIDER_IDENTITY_GENERATED_DIR}/EXTRACTION.txt"
        "method=${name}\nnormalized_method_sha256=${body_hash}\n")
    set(${output} "${body}" PARENT_SCOPE)
endfunction()

_provider_read("platform/windp.cpp" _provider_cpp)
_provider_read("platform/windp.h" _provider_header)
foreach(header platform/windp_resume.h platform/windp_roster.h platform/windp_events.h
        netlib/events.h netlib/transport_types.h netlib/connfail.h netlib/errcode.h world/net_protocol.h)
    _provider_read("${header}" unused_header)
endforeach()
# Bracket quoting keeps the regex's escaped-character branch intact. Ordinary
# CMake quoting would consume a backslash and misread C++ strings containing \n.
set(_provider_literal_pattern [=["([^"\\]|\\.)*"|'([^'\\]|\\.)*'|//[^
]*|/\*([^*]|\*+[^*/])*\*+/]=])
string(REGEX REPLACE "${_provider_literal_pattern}" "" _provider_masked "${_provider_cpp}")
set(_provider_methods
    _clear ClearConnectionState init deinit SetMode GetMode SetWantedName GetMyName
    Connect IsConnected HasLobby IsJoined Host GetCurrentSessionName GetPlayerData
    GetPlayersData GetPlayerCount GetMyIndex GetPlayerIndex GetPlayerName IsPlayer
    Send Broadcast QueueBroadcast SendLowLevel BroadcastLowLevel FlushBroadcastBuffer
    Recv Reset LockSession ReloadUsersList ReloadSessionsList GetUserID GetUserName
    UpdateOwnPlayerIndex SetRecoveryEnabled CompleteRecovery RecoveryPhase CanSendGameplay
    TickRecovery StartRecoveryAttempt StopClientTransport RememberConnection
    GetSessionIdentity HasValidRoster SendToPlayer KickPlayer RefreshPlayers
    AcceptTransportIdentity RejectSessionIdentity)
list(LENGTH _provider_methods method_count)
if(NOT method_count EQUAL 50)
    message(FATAL_ERROR "provider extraction: method inventory must contain exactly 50 entries")
endif()
set(_provider_bodies "")
foreach(method IN LISTS _provider_methods)
    _provider_method("${method}" body)
    string(APPEND _provider_bodies "${body}\n\n")
endforeach()

# Preserve real fields/defaults/declarations, with the same three documented
# transformations as the independently exercised external harness.
string(REGEX REPLACE "(^|\n)[ \t]*#include[^\n]*" "" declaration "${_provider_header}")
string(REPLACE "virtual " "" declaration "${declaration}")
string(REPLACE " override" "" declaration "${declaration}")
string(REPLACE "protected:" "public:" declaration "${declaration}")
string(FIND "${declaration}" "#include" remaining_include)
if(NOT remaining_include EQUAL -1)
    message(FATAL_ERROR "provider extraction: engine include removal was incomplete")
endif()
string(SHA256 declaration_hash "${declaration}")
file(APPEND "${PROVIDER_IDENTITY_GENERATED_DIR}/EXTRACTION.txt"
    "declaration_sha256=${declaration_hash}\nmethod_count=${method_count}\n")
file(WRITE "${PROVIDER_IDENTITY_GENERATED_DIR}/provider_identity_production.inc"
    "// Generated from current production source; do not hand edit.\n${declaration}\n${_provider_bodies}")
file(SHA256 "${PROVIDER_IDENTITY_GENERATED_DIR}/provider_identity_production.inc" generated_hash)
file(APPEND "${PROVIDER_IDENTITY_GENERATED_DIR}/EXTRACTION.txt"
    "generated=provider_identity_production.inc\ngenerated_sha256=${generated_hash}\n")
foreach(fixture test_provider_identity.cpp provider_identity_platform.hpp provider_identity_transport.hpp require.h)
    file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/${fixture}" fixture_hash)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/${fixture}")
    file(APPEND "${PROVIDER_IDENTITY_GENERATED_DIR}/EXTRACTION.txt"
        "fixture=${fixture}\nfixture_sha256=${fixture_hash}\n")
endforeach()
