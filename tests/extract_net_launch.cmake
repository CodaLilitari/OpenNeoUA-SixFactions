# Uses the checked extraction functions and exact lobby record already loaded by
# extract_net_ingress.cmake. This fixture executes FreezeNetLaunch unchanged;
# its preceding roster validation is a controlled input, not a tested substitute.
_ingress_read("game/yw_func2.cpp" launch_source)
_ingress_read("platform/windp_roster.h" launch_roster_source)
_ingress_slice(launch_source "bool UserData::FreezeNetLaunch(int level)\n"
    "\nWorld::NetFactions::RosterCheck UserData::CheckNetLobbyRoster() const\n"
    FALSE freeze_launch_method)
file(WRITE "${NET_INGRESS_GENERATED_DIR}/net_launch_production.inc"
    "// Exact production lobby record and launch method, with test-only surroundings.\n"
    "class UserData { public:\n${lobby_record}\n"
    "LaunchWorld *p_YW = nullptr;\n"
    "bool netLaunchFrozen = false;\nint netLevelID = 201;\nuint8_t SelectedFraction = 4;\n"
    "World::NetLobby::SessionIdentity netSession;\n"
    "std::array<TNetPlayerLobbyData, 6> lobbyPlayers{}, netLaunchPlayers{};\n"
    "World::NetFactions::RosterCheck rosterCheck{true, true, 0};\n"
    "World::NetFactions::RosterCheck CheckNetLobbyRoster() const { return rosterCheck; }\n"
    "bool FreezeNetLaunch(int level);\n};\n${freeze_launch_method}\n")
file(SHA256 "${NET_INGRESS_GENERATED_DIR}/net_launch_production.inc" launch_generated_hash)
file(APPEND "${NET_INGRESS_GENERATED_DIR}/EXTRACTION.txt"
    "launch_scope=exact FreezeNetLaunch and lobby record; preceding roster validation is controlled fixture input\n"
    "generated=net_launch_production.inc\ngenerated_sha256=${launch_generated_hash}\n")
