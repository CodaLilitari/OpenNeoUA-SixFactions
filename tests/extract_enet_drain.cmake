# Exact production methods with real SDL mutexes and an explicitly empty
# transport fixture. No private-access tricks or production testing hooks.
get_filename_component(_enet_probe_repo "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(_enet_probe_binary "${CMAKE_CURRENT_BINARY_DIR}" ABSOLUTE)
string(FIND "${_enet_probe_binary}/" "${_enet_probe_repo}/" _enet_probe_in_source)
if(_enet_probe_in_source EQUAL 0)
    message(FATAL_ERROR "ENet drain probe requires an external build directory")
endif()
set(ENET_DRAIN_PROBE_HEADER "${_enet_probe_repo}/src/netlib/enet_znd.h" CACHE FILEPATH
    "Adapter header for exact-method drain regression; override only for a pinned RED baseline")
set(ENET_DRAIN_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/enet_drain_generated")
file(MAKE_DIRECTORY "${ENET_DRAIN_GENERATED_DIR}")
file(WRITE "${ENET_DRAIN_GENERATED_DIR}/EXTRACTION.txt"
    "format=1\nnormalization=CRLF to LF only\n"
    "scope=exact inline ENet SendIdle, adapter LockNet/UnlockNet and base WaitSendIdle\n"
    "limits=real SDL mutex and clocks; controlled empty transport; no adapter lifecycle, sockets, ENet queues or game\n")
function(_enet_probe_read path output)
    file(READ "${path}" raw)
    file(SHA256 "${path}" raw_hash)
    string(REPLACE "\r\n" "\n" normalized "${raw}")
    string(FIND "${normalized}" "\r" bare_cr)
    if(NOT bare_cr EQUAL -1)
        message(FATAL_ERROR "ENet probe: unexpected bare CR in ${path}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(APPEND "${ENET_DRAIN_GENERATED_DIR}/EXTRACTION.txt"
        "source=${path}\nraw_sha256=${raw_hash}\n")
    set(${output} "${normalized}" PARENT_SCOPE)
endfunction()
function(_enet_probe_anchor text marker output)
    string(FIND "${text}" "${marker}" first)
    if(first EQUAL -1)
        message(FATAL_ERROR "ENet probe: missing anchor [${marker}]")
    endif()
    string(LENGTH "${marker}" size)
    math(EXPR after "${first}+${size}")
    string(SUBSTRING "${text}" ${after} -1 tail)
    string(FIND "${tail}" "${marker}" duplicate)
    if(NOT duplicate EQUAL -1)
        message(FATAL_ERROR "ENet probe: duplicate anchor [${marker}]")
    endif()
    set(${output} ${first} PARENT_SCOPE)
endfunction()
function(_enet_probe_slice text start_marker end_marker output)
    _enet_probe_anchor("${text}" "${start_marker}" first)
    _enet_probe_anchor("${text}" "${end_marker}" last)
    if(NOT first LESS last)
        message(FATAL_ERROR "ENet probe: unordered method boundaries")
    endif()
    math(EXPR size "${last}-${first}")
    string(SUBSTRING "${text}" ${first} ${size} method)
    string(SHA256 method_hash "${method}")
    file(APPEND "${ENET_DRAIN_GENERATED_DIR}/EXTRACTION.txt"
        "method=${output}\nnormalized_method_sha256=${method_hash}\n")
    set(${output} "${method}" PARENT_SCOPE)
endfunction()
_enet_probe_read("${ENET_DRAIN_PROBE_HEADER}" header_source)
_enet_probe_read("${_enet_probe_repo}/src/netlib/enet_znd.cpp" adapter_source)
_enet_probe_read("${_enet_probe_repo}/src/netlib/zndNet.cpp" base_source)
_enet_probe_slice("${header_source}" "    bool SendIdle() override\n"
    "    std::uint64_t GetMyID() override;\n" idle_method)
_enet_probe_slice("${adapter_source}" "void\nZNDEnet::LockNet() const\n"
    "void\nZNDEnet::UnlockNet() const\n" lock_method)
_enet_probe_slice("${adapter_source}" "void\nZNDEnet::UnlockNet() const\n"
    "int\nZNDEnet::PumpMain(void* data)\n" unlock_method)
_enet_probe_slice("${base_source}" "bool ZNDNet::WaitSendIdle(uint32_t timeoutMs)\n"
    "void ZNDNet::Send_Clear(const IPaddress &addr)\n" wait_method)
file(WRITE "${ENET_DRAIN_GENERATED_DIR}/enet_drain_probe.inc"
    "// Generated exact method text; see EXTRACTION.txt for sources and limits.\n"
    "namespace ZNDNet {\n"
    "class ZNDNet { public: virtual bool SendIdle() = 0; bool WaitSendIdle(uint32_t timeoutMs); };\n"
    "struct ControlledEmptyTransport { bool ReliableIdle() const { return true; } };\n"
    "class ZNDEnet : public ZNDNet { public:\n"
    "  SDL_mutex *netMutex_ = SDL_CreateMutex();\n"
    "  ControlledEmptyTransport transport_;\n"
    "  ~ZNDEnet() { SDL_DestroyMutex(netMutex_); }\n"
    "  void LockNet() const; void UnlockNet() const;\n"
    "${idle_method}\n};\n${lock_method}\n${unlock_method}\n${wait_method}\n}\n")
file(SHA256 "${ENET_DRAIN_GENERATED_DIR}/enet_drain_probe.inc" generated_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/test_enet_drain_locking.cpp" fixture_hash)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/test_enet_drain_locking.cpp")
file(APPEND "${ENET_DRAIN_GENERATED_DIR}/EXTRACTION.txt"
    "generated_sha256=${generated_hash}\nfixture_sha256=${fixture_hash}\n")
