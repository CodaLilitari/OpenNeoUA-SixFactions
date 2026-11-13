// FreezeNetLaunch must carry provider names into gameplay, not lobby decoration.
// The complete production method and lobby row are extracted at configure time.
// The provider supplies real TDPPlayerData records; roster validation is a
// controlled precondition here. This does not test the full lobby or world load.
#include "require.h"
#include "platform/windp_roster.h"
#include "world/net_factions.h"
#include "world/net_lobby.h"
#include "world/net_update_gate.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

struct LaunchProvider
{
    std::vector<TDPPlayerData> Players;
    const std::vector<TDPPlayerData> &GetPlayersData() const { return Players; }
};
struct LaunchWorld { LaunchProvider *_netDriver; };
#include "net_launch_production.inc"

struct Fixture
{
    LaunchProvider provider;
    LaunchWorld world{&provider};
    UserData user;
    explicit Fixture(const std::string &callsign = "CLIENT")
    {
        user.p_YW = &world;
        user.netSession = {100, 505, 101};
        user.SelectedFraction = 1u << 2;
        provider.Players = {{0, 101, "HOST", TDPPlayerData::NPD_HOST},
                            {5, 505, callsign, TDPPlayerData::NPD_ITSME}};
        REQUIRE(World::NetLobby::BindRow(user.lobbyPlayers, 0, 100, 101, "HOST"));
        REQUIRE(World::NetLobby::BindRow(user.lobbyPlayers, 5, 100, 505, callsign));
        user.lobbyPlayers[0].NetFraction = 1u << 5;
        user.lobbyPlayers[0].Ready = true;
        auto &local = user.lobbyPlayers[5];
        local.Name += "X"; // Existing lobby-only presentation marker.
        local.NetFraction = 0; // Local selection is held separately before launch.
        local.Msg = "preserve pending message";
        local.Ready = true;
        local.Welcomed = true;
        local.DataChecksum = 0x12345678;
    }
};

static void NamesAndState()
{
    Fixture f;
    f.user.lobbyPlayers[0].Name = "old host display";
    REQUIRE(f.user.FreezeNetLaunch(201));
    REQUIRE(f.user.netLaunchFrozen);
    REQUIRE(f.user.netLaunchPlayers[0].Name == "HOST");
    REQUIRE(f.user.netLaunchPlayers[5].Name == "CLIENT");
    REQUIRE(f.user.netLaunchPlayers[5].Name != "CLIENTX");
    REQUIRE(f.user.netLaunchPlayers[5].PlayerId == 505);
    REQUIRE(f.user.netLaunchPlayers[5].SessionId == 100);
    REQUIRE(f.user.netLaunchPlayers[5].Owner == 2);
    REQUIRE(f.user.netLaunchPlayers[0].Owner == 5);
    REQUIRE(f.user.netLaunchPlayers[5].NetFraction == (1u << 2));
    REQUIRE(f.user.netLaunchPlayers[5].Ready && f.user.netLaunchPlayers[5].Welcomed);
    REQUIRE(f.user.netLaunchPlayers[5].Msg == "preserve pending message");
    REQUIRE(f.user.netLaunchPlayers[5].DataChecksum == 0x12345678);
    REQUIRE(f.user.netLaunchPlayers[1].PlayerId == 0);
    REQUIRE(f.user.netLaunchPlayers[4].Name.empty());
    REQUIRE(f.user.lobbyPlayers[5].Name == "CLIENTX");
    REQUIRE(f.user.lobbyPlayers[0].Name == "old host display");
}

static void LegitimateTrailingX()
{
    Fixture f("PHOENIX");
    REQUIRE(f.user.FreezeNetLaunch(201));
    REQUIRE(f.user.netLaunchPlayers[5].Name == "PHOENIX");
    REQUIRE(f.user.lobbyPlayers[5].Name == "PHOENIXX");
    f.provider.Players[1].name = "RENAMED AFTER FREEZE";
    REQUIRE(!f.user.FreezeNetLaunch(201));
    REQUIRE(f.user.netLaunchPlayers[5].Name == "PHOENIX");
}

static void RejectedLaunchIsAtomic()
{
    for (int condition = 0; condition != 5; ++condition)
    {
        Fixture f;
        f.user.netLaunchPlayers[3].Name = "existing sentinel";
        if (condition == 0) f.user.rosterCheck.Valid = false;
        if (condition == 1) f.provider.Players[1].flags |= TDPPlayerData::NPD_AWAY;
        if (condition == 2) f.provider.Players[1].ID = 999;
        if (condition == 3) f.user.SelectedFraction = 0;
        REQUIRE(!f.user.FreezeNetLaunch(condition == 4 ? 202 : 201));
        REQUIRE(!f.user.netLaunchFrozen);
        REQUIRE(f.user.netLaunchPlayers[3].Name == "existing sentinel");
        REQUIRE(f.user.netLaunchPlayers[0].PlayerId == 0);
        REQUIRE(f.user.netLaunchPlayers[5].Name.empty());
        REQUIRE(f.user.lobbyPlayers[5].Name == "CLIENTX");
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::string(argv[1]) == "--verify-failing-check")
        REQUIRE(false);
    NamesAndState();
    LegitimateTrailingX();
    RejectedLaunchIsAtomic();
    std::cout << "launch names, sparse seats and atomic rejection passed\n";
}
