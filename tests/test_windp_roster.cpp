// The production provider view, independent of SDL/window/game initialization.
#include "../src/platform/windp_roster.h"
#include "require.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// Same input members as netlib::UserInfo. The conversion below is the production
// template called by windp, rather than a second implementation of its rules.
struct TransportUser
{
    std::uint64_t ID;
    std::string name;
    std::uint8_t slot;
    std::uint8_t status;
};

int main()
{
    using namespace WindpRoster;
    std::vector<TransportUser> raw{{30, "FIVE", 5, 1}, {10, "HOST", 0, 1}, {20, "TWO", 2, 1}};
    std::vector<TDPPlayerData> players;
    REQUIRE(Convert(raw, 30, players));
    REQUIRE(players.size() == 3);
    REQUIRE(players[0].Index == 0);
    REQUIRE(players[1].Index == 2);
    REQUIRE(players[2].Index == 5);
    REQUIRE(OwnSlot(players) == 5);
    REQUIRE(AtSlot(players, 1) == nullptr);
    REQUIRE(AtSlot(players, 3) == nullptr);
    REQUIRE(AtSlot(players, 6) == nullptr);
    REQUIRE(AtSlot(players, 0xffffffffu) == nullptr);
    REQUIRE(AtSlot(players, 5)->IsItMe());
    REQUIRE(AtSlot(players, 5)->IsOnline());
    REQUIRE(AtSlot(players, 2)->ID == 20);

    // Away remains a reservation, with stable identity and seat.
    raw[2].status = 2;
    REQUIRE(Convert(raw, 30, players));
    REQUIRE(players.size() == 3);
    REQUIRE(!AtSlot(players, 2)->IsOnline());
    REQUIRE(AtSlot(players, 2)->name == "TWO");
    REQUIRE(OwnSlot(players) == 5);

    // Terminal removal and later replacement in 2 cannot move seat 5.
    raw.erase(raw.begin() + 2);
    REQUIRE(Convert(raw, 30, players));
    REQUIRE(players.size() == 2);
    REQUIRE(AtSlot(players, 2) == nullptr);
    REQUIRE(OwnSlot(players) == 5);
    raw.push_back({40, "NEW", 2, 1});
    REQUIRE(Convert(raw, 30, players));
    REQUIRE(AtSlot(players, 2)->ID == 40);
    REQUIRE(AtSlot(players, 5)->ID == 30);
    REQUIRE(Convert(raw, 999, players));
    REQUIRE(OwnSlot(players) == -1);

    // Reject an invalid complete snapshot atomically; caller can explicitly
    // clear its old view. Never expose a prefix as a new accepted roster.
    const auto accepted = players;
    const auto invalid = [&](std::vector<TransportUser> input) {
        REQUIRE(!Convert(input, 30, players));
        REQUIRE(players.size() == accepted.size());
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            REQUIRE(players[i].ID == accepted[i].ID);
            REQUIRE(players[i].Index == accepted[i].Index);
            REQUIRE(players[i].flags == accepted[i].flags);
        }
    };
    auto bad = raw;
    bad[2].slot = 5; invalid(bad);
    bad = raw; bad[2].slot = 6; invalid(bad);
    bad = raw; bad[2].ID = 30; invalid(bad);
    bad = raw; bad[2].ID = 0; invalid(bad);
    bad = raw; bad[2].status = 0; invalid(bad);
    bad = raw; bad[2].status = 3; invalid(bad);
    bad = raw; bad[2].name.clear(); invalid(bad);
    bad = raw; bad.resize(7, raw[0]); invalid(bad);
    REQUIRE(Convert(std::vector<TransportUser>{}, 30, players));
    REQUIRE(players.empty());
    REQUIRE(OwnSlot(players) == -1);
    std::cout << "provider roster preserves explicit slots, reservations and identity\n";
}
