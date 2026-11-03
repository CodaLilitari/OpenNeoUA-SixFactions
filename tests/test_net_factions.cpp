#include "../src/world/net_factions.h"
#include "../src/world/net_faction_selection.h"
#include "require.h"
#include <array>
#include <cstdlib>
#include <iostream>
using namespace World::NetFactions;
static_assert(OwnerOf(32) == 5, "Black Sect is owner 5");
static_assert(FirstAvailable(32) == 32, "Black-only maps select Black Sect");
static_assert(HostVehicle(5, 62) == 62, "Use authored Black Sect station");

static void TestRosterTransitions()
{
    std::array<RosterEntry, 6> players = {{{0, 2, true}, {1, 64, true},
        {2, 8, true}, {3, 16, true}, {4, 32, true}, {5, 4, true}}};
    const RosterCheck uiCheck = CheckRoster(126, players.data(), players.size());
    Require(uiCheck.CanStart(), "six distinct ready factions can start");

    // A previously enabled Start button must not certify a later roster.
    players[5].Faction = 32;
    const RosterCheck launchCheck = CheckRoster(126, players.data(), players.size());
    Require(uiCheck.CanStart(), "the earlier UI snapshot remains a snapshot");
    Require(!launchCheck.CanStart(), "fresh launch check rejects a newly duplicated faction");
    Require(launchCheck.ConflictingSlots == 48, "both duplicate-faction players are marked");

    players[5].Faction = 4;
    players[5].Ready = false;
    const RosterCheck unready = CheckRoster(126, players.data(), players.size());
    Require(unready.Valid && !unready.AllReady && !unready.CanStart(), "readiness withdrawn after UI check blocks launch");
    Require(unready.ConflictingSlots == 0, "readiness alone is not a faction conflict");
    players[5].Ready = true;
    Require(CheckRoster(126, players.data(), players.size()).CanStart(), "corrected roster can start again");

    const RosterCheck missing = CheckRoster(122, players.data(), players.size());
    Require(!missing.CanStart() && missing.ConflictingSlots == 32, "Sulgogar selection needs a Sulgogar map start");
    for (unsigned invalid : {0u, 1u, 6u, 128u})
    {
        players[5].Faction = invalid;
        const RosterCheck check = CheckRoster(126, players.data(), players.size());
        Require(!check.CanStart() && check.ConflictingSlots == 32, "zero, neutral, multibit and unused factions are rejected");
    }
    players[5].Faction = 4;
    players[5].Slot = 4;
    Require(!CheckRoster(126, players.data(), players.size()).CanStart(), "two entries cannot share a player slot");
    players[5].Slot = -1;
    Require(!CheckRoster(126, players.data(), players.size()).CanStart(), "negative player slot is rejected");
    players[5].Slot = 6;
    Require(!CheckRoster(126, players.data(), players.size()).CanStart(), "seventh player slot is rejected");
    players[5].Slot = 5;
    Require(!CheckRoster(126, players.data(), 0).CanStart(), "empty roster cannot start");
    Require(!CheckRoster(126, players.data(), 7).CanStart(), "oversized roster is rejected before reading entries");
    Require(!CheckRoster(126, nullptr, 1).CanStart(), "missing roster storage is rejected");
    Require(CheckRoster(2, players.data(), 1).CanStart(), "one-player confirmation remains supported");
    Require(CheckRoster(90, players.data(), 4).CanStart(), "legacy four-faction roster remains supported");
}

static void TestWelcomeSelection()
{
    // Five existing choices plus the welcomed choice leave no faction to claim.
    const auto exhausted = SelectionAfterWelcome(32, 32, 32);
    Require(exhausted.Selected == 0 && exhausted.FreeMask == 0,
            "a full lobby leaves no replacement faction");
    Require(!exhausted.CanAnnounce(), "a full lobby must not announce a zero faction");

    const auto sulgogar = SelectionAfterWelcome(32, 32, 32 | 4);
    Require(sulgogar.Selected == 4 && sulgogar.FreeMask == 0 && sulgogar.CanAnnounce(),
            "a Black collision can claim the free sixth Sulgogar faction");
    const auto black = SelectionAfterWelcome(4, 4, 4 | 32);
    Require(black.Selected == 32 && black.FreeMask == 0 && black.CanAnnounce(),
            "a Sulgogar collision can claim the free Black faction");

    const auto unchanged = SelectionAfterWelcome(2, 32, 32 | 64);
    Require(unchanged.Selected == 2 && unchanged.FreeMask == 64 && unchanged.CanAnnounce(),
            "welcoming another faction preserves an existing valid selection");
    const auto legacy = SelectionAfterWelcome(2, 2, 2 | 8 | 64);
    Require(legacy.Selected == 64 && legacy.FreeMask == 8 && legacy.CanAnnounce(),
            "welcome collision preserves the legacy faction priority");

    for (unsigned invalid : {0u, 1u, 6u, 128u})
        Require(!SelectionAfterWelcome(invalid, 32, 4).CanAnnounce(),
                "an invalid existing selection is never announced");
}

int main()
{
    TestRosterTransitions();
    TestWelcomeSelection();
    // Exhaust all one-byte faction masks, including zero, invalid IDs and multibit inputs.
    for (unsigned fraction = 0; fraction < 256; ++fraction)
    {
        const bool expected = fraction == 2 || fraction == 64 || fraction == 8 || fraction == 16 || fraction == 32 || fraction == 4;
        REQUIRE((OwnerOf(fraction) >= 0) == expected);
        for (unsigned map = 0; map < 256; ++map)
        {
            REQUIRE(IsAvailable(fraction, map) == (expected && (fraction & map)));
            REQUIRE(!IsAvailable(fraction, map, fraction));
        }
    }
    // Preserve legacy order when owner 5 is absent; append Black Sect and Sulgogars.
    unsigned free = Mask;
    const unsigned ordered[] = {2, 64, 8, 16, 32, 4};
    for (unsigned expected : ordered)
    {
        const unsigned bit = FirstAvailable(free);
        REQUIRE(bit == expected);
        REQUIRE(IsAvailable(bit, free));
        free &= ~bit;
    }
    REQUIRE(FirstAvailable(free) == 0);
    REQUIRE(FirstAvailable(1 | 128) == 0); // Neutral/unused owner are not exposed.
    REQUIRE(HostVehicle(1, 999) == 56);
    REQUIRE(HostVehicle(6, 999) == 57);
    REQUIRE(HostVehicle(3, 999) == 58);
    REQUIRE(HostVehicle(4, 999) == 60);
    REQUIRE(HostVehicle(5, 900) == 900); // mod-defined host accepted for later type validation
    REQUIRE(HostVehicle(5, 0) == 0);     // missing host rejected by engine
    REQUIRE(HostVehicle(2, 61) == 61);   // original Sulgogar Queen
    REQUIRE(HostVehicle(2, 0) == 0);
    REQUIRE(MaxPlayers == 6);
    std::cout << "65,536 availability combinations, roster/host, fresh-launch and WELCOME regressions passed\n";
}
