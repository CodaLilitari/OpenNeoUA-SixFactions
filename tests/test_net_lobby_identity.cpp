// Local identity matching only: no transport admission, launch or recovery grant.
#include "../src/world/net_lobby.h"
#include "../src/world/net_update_gate.h"
#include "require.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace Lobby = World::NetLobby;

// The generic helper is tested with the lobby row's complete existing state
// plus the two identity fields that the game will add. The real repair gate
// makes accidental same-player resets and inherited replacement state visible.
struct Row
{
    std::uint64_t SessionId = 0;
    std::uint64_t PlayerId = 0;
    std::string Name;
    std::string Msg;
    std::uint8_t NetFraction = 0;
    bool IsTrouble = false;
    std::uint8_t Owner = 0;
    bool Ready = false;
    bool Welcomed = false;
    World::NetUpdate::PeerGate Update;
    std::uint32_t DataChecksum = 0;
};
using Rows = std::array<Row, 6>;

static_assert(std::is_same<decltype(Lobby::MatchRow(std::declval<const Rows &>(),
    0, 1, 2)), const Row *>::value, "const rows return const rows");

static void TestSessionIdentity()
{
    Lobby::SessionIdentity identity;
    REQUIRE(!identity.Valid());
    REQUIRE(!identity.Matches(0));
    identity.SessionId = 10;
    identity.LocalPlayerId = 101;
    REQUIRE(!identity.Valid());
    REQUIRE(!identity.Matches(10));
    identity.HostPlayerId = 101; // self may be the network host
    REQUIRE(identity.Valid() && identity.Matches(10));
    REQUIRE(!identity.Matches(0) && !identity.Matches(11));
    identity.HostPlayerId = 202;
    REQUIRE(identity.Valid()); // host identity does not encode a faction owner
    identity.LocalPlayerId = 0;
    REQUIRE(!identity.Valid() && !identity.Matches(10));
    identity.LocalPlayerId = 101;
    identity.SessionId = 0;
    REQUIRE(!identity.Valid());
}

static void TestExactTupleAndBounds()
{
    Rows rows{};
    REQUIRE(Lobby::BindRow(rows, 2, 10, 202, "TWO"));
    REQUIRE(Lobby::MatchRow(rows, 2, 10, 202) == &rows[2]);
    const Rows &view = rows;
    REQUIRE(Lobby::MatchRow(view, 2, 10, 202) == &rows[2]);
    REQUIRE(!Lobby::MatchRow(rows, 2, 11, 202));
    REQUIRE(!Lobby::MatchRow(rows, 2, 10, 203));
    REQUIRE(!Lobby::MatchRow(rows, 5, 10, 202));
    REQUIRE(!Lobby::MatchRow(rows, 2, 0, 202));
    REQUIRE(!Lobby::MatchRow(rows, 2, 10, 0));
    REQUIRE(!Lobby::MatchRow(rows, 0, 0, 0)); // empty row is not an identity
    const std::array<std::int64_t, 5> invalidSlots{{-1, 6, 0xFFFFFFFFll,
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max()}};
    for (std::int64_t slot : invalidSlots)
    {
        REQUIRE(!Lobby::MatchRow(rows, slot, 10, 202));
        REQUIRE(!Lobby::BindRow(rows, slot, 10, 303, "INVALID"));
    }
    REQUIRE(!Lobby::BindRow(rows, 2, 0, 202, "INVALID"));
    REQUIRE(!Lobby::BindRow(rows, 2, 10, 0, "INVALID"));
    REQUIRE(rows[2].SessionId == 10 && rows[2].PlayerId == 202);
    REQUIRE(rows[2].Name == "TWO");
}

static void TestSparseSlotsAndDuplicates()
{
    Rows rows{};
    REQUIRE(Lobby::BindRow(rows, 0, 10, 101, "HOST"));
    REQUIRE(Lobby::BindRow(rows, 2, 10, 202, "TWO"));
    REQUIRE(Lobby::BindRow(rows, 5, 10, 505, "FIVE"));
    REQUIRE(Lobby::FindPlayerSlot(rows, 10, 101) == 0);
    REQUIRE(Lobby::FindPlayerSlot(rows, 10, 202) == 2);
    REQUIRE(Lobby::FindPlayerSlot(rows, 10, 505) == 5);
    REQUIRE(Lobby::FindPlayerSlot(rows, 10, 999) == -1);
    REQUIRE(Lobby::FindPlayerSlot(rows, 0, 202) == -1);
    REQUIRE(Lobby::FindPlayerSlot(rows, 10, 0) == -1);
    REQUIRE(!Lobby::BindRow(rows, 4, 10, 202, "DUPLICATE"));
    REQUIRE(rows[4].PlayerId == 0 && rows[4].Name.empty());
    REQUIRE(rows[2].Name == "TWO" && rows[5].Name == "FIVE");

    // Corrupt input must not silently choose the first duplicate in a session.
    rows[4].SessionId = 10;
    rows[4].PlayerId = 202;
    REQUIRE(Lobby::FindPlayerSlot(rows, 10, 202) == -1);
    REQUIRE(!Lobby::BindRow(rows, 2, 10, 202, "RENAMED"));
    REQUIRE(rows[2].Name == "TWO");
    REQUIRE(Lobby::ClearSlot(rows, 4));

    // IDs are session-qualified; a different session cannot resolve this row.
    rows[4].SessionId = 11;
    rows[4].PlayerId = 202;
    const Rows &view = rows;
    REQUIRE(Lobby::FindPlayerSlot(view, 10, 202) == 2);
    REQUIRE(Lobby::FindPlayerSlot(view, 11, 202) == 4);
    REQUIRE(Lobby::ClearSlot(rows, 2));
    REQUIRE(rows[0].Name == "HOST" && rows[5].Name == "FIVE");
    REQUIRE(rows[1].PlayerId == 0 && rows[3].PlayerId == 0);
    REQUIRE(Lobby::FindPlayerSlot(rows, 10, 505) == 5);
}

static void SetOccupiedState(Row &row)
{
    row.Msg = "pending diagnostic";
    row.NetFraction = 32;
    row.IsTrouble = true;
    row.Owner = 5;
    row.Ready = true;
    row.Welcomed = true;
    row.Update.Requested(100);
    row.Update.Answered(100);
    row.DataChecksum = 12345;
}

static void TestSameIdentityPreservesState()
{
    Rows rows{};
    REQUIRE(Lobby::BindRow(rows, 2, 10, 202, "OLD NAME"));
    SetOccupiedState(rows[2]);
    REQUIRE(Lobby::BindRow(rows, 2, 10, 202, "NEW NAME"));
    const Row &row = rows[2];
    REQUIRE(row.Name == "NEW NAME");
    REQUIRE(row.SessionId == 10 && row.PlayerId == 202);
    REQUIRE(row.Msg == "pending diagnostic" && row.NetFraction == 32);
    REQUIRE(row.IsTrouble && row.Owner == 5 && row.Ready && row.Welcomed);
    REQUIRE(row.DataChecksum == 12345);
    REQUIRE(row.Update.HoldsEvents(101));
    REQUIRE(!row.Update.MayRequest(101) && !row.Update.MayAnswer(101));
}

static void CheckFreshState(const Row &row, std::uint64_t session,
                            std::uint64_t player)
{
    REQUIRE(row.SessionId == session && row.PlayerId == player);
    REQUIRE(row.Name == "SAME NAME");
    REQUIRE(row.Msg.empty() && row.NetFraction == 0 && !row.IsTrouble);
    REQUIRE(row.Owner == 0 && !row.Ready && !row.Welcomed);
    REQUIRE(row.DataChecksum == 0);
    REQUIRE(!row.Update.HoldsEvents(101));
    REQUIRE(row.Update.MayRequest(101) && row.Update.MayAnswer(101));
}

static void TestReplacementAndStaleEvents()
{
    Rows rows{};
    REQUIRE(Lobby::BindRow(rows, 2, 10, 202, "SAME NAME"));
    SetOccupiedState(rows[2]);
    REQUIRE(Lobby::BindRow(rows, 2, 10, 303, "SAME NAME"));
    CheckFreshState(rows[2], 10, 303);
    rows[2].Ready = true;

    // The generic helper does not dispatch AWAY/GONE. These caller-shaped
    // checks prove an old event cannot resolve the replacement before mutation.
    if (Row *away = Lobby::MatchRow(rows, 2, 10, 202))
        away->Ready = false;
    if (Lobby::MatchRow(rows, 2, 10, 202))
        Lobby::ClearSlot(rows, 2);
    REQUIRE(rows[2].PlayerId == 303 && rows[2].Ready);
    REQUIRE(Lobby::FindPlayerSlot(rows, 10, 202) == -1);
    REQUIRE(Lobby::FindPlayerSlot(rows, 10, 303) == 2);

    SetOccupiedState(rows[2]);
    REQUIRE(Lobby::BindRow(rows, 2, 11, 303, "SAME NAME"));
    CheckFreshState(rows[2], 11, 303); // same ID in a new session is fresh
    REQUIRE(!Lobby::MatchRow(rows, 2, 10, 303));
    REQUIRE(Lobby::MatchRow(rows, 2, 11, 303));
}

static void TestLegacyStructuralHelpers()
{
    std::array<std::string, 6> rows{};
    rows[0] = "HOST";
    rows[2] = "TWO";
    rows[5] = "FIVE";
    REQUIRE(Lobby::RowAt(rows, 2) == &rows[2]);
    const auto &view = rows;
    REQUIRE(Lobby::RowAt(view, 5) == &rows[5]);
    REQUIRE(!Lobby::RowAt(rows, -1) && !Lobby::RowAt(rows, 6));
    REQUIRE(Lobby::ClearSlot(rows, 2));
    REQUIRE(rows[2].empty() && rows[0] == "HOST" && rows[5] == "FIVE");
    REQUIRE(!Lobby::ClearSlot(rows, -1) && !Lobby::ClearSlot(rows, 6));
}

int main()
{
    TestSessionIdentity();
    TestExactTupleAndBounds();
    TestSparseSlotsAndDuplicates();
    TestSameIdentityPreservesState();
    TestReplacementAndStaleEvents();
    TestLegacyStructuralHelpers();
    std::cout << "lobby tuple matching, sparse slots, replacement and state retention passed\n";
}
