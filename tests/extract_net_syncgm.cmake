# Reuse fail-closed source reads/unique ordered boundaries and real records from
# extract_net_ingress.cmake. No copied authorization or SYNCGM implementation.
_ingress_slice(handler_source "    case UAMSG_SYNCGM:\n" "    case UAMSG_HOSTDIE:\n" FALSE syncgm_case)
_ingress_slice(net_source "    NC_STACK_ypabact *fndHost = nullptr;\n"
    "    _GameShell->netsend_count = 0;\n" FALSE startup_send_tail)
file(WRITE "${NET_INGRESS_GENERATED_DIR}/net_syncgm.inc"
    "#include \"net_syncgm_fixture.hpp\"\n${outer_lobby_helper}${prefix}\n"
    "${syncgm_case}\n"
    "// Other NORMAL cases are entry markers; only SYNCGM gameplay is compiled.\n"
    "default: ++dispatch_marker; break;\n}\nreturn szmsg;\n}\n"
    "${outer_corruption}${outer_receive}\n"
    "// Exact loader tail in a fixture method; preceding full level load omitted.\n"
    "size_t NC_STACK_ypaworld::FixtureStartupSend() {\n${startup_send_tail}\nreturn 1;\n}\n")
file(SHA256 "${NET_INGRESS_GENERATED_DIR}/net_syncgm.inc" syncgm_hash)
file(APPEND "${NET_INGRESS_GENERATED_DIR}/EXTRACTION.txt"
    "syncgm_scope=actual outer receive, framing, admission, accounting, SYNCGM case and loader send tail\n"
    "syncgm_limits=engine object/RTTI, provider queue and side-effect sinks; other NORMAL cases omitted\n"
    "generated=net_syncgm.inc\ngenerated_sha256=${syncgm_hash}\n")
foreach(fixture test_net_syncgm.cpp net_syncgm_fixture.hpp net_station_fixture.hpp)
    file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/${fixture}" fixture_hash)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/${fixture}")
    file(APPEND "${NET_INGRESS_GENERATED_DIR}/EXTRACTION.txt"
        "syncgm_fixture=${fixture}\nfixture_sha256=${fixture_hash}\n")
endforeach()
