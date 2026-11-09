// Lobby row access by transport index (src/world/net_lobby.h).
#include "../src/world/net_lobby.h"
#include "require.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

int main()
{
    using World::NetLobby::RowAt;
    std::array<std::string, 6> rows;
    REQUIRE(RowAt(rows, 0) == &rows[0]);
    REQUIRE(RowAt(rows, 5) == &rows[5]);
    REQUIRE(RowAt(rows, 6) == nullptr);                  // seventh user (ENet resume over capacity)
    REQUIRE(RowAt(rows, -1) == nullptr);                 // GetMyIndex() while not listed
    REQUIRE(RowAt(rows, std::uint32_t(7)) == nullptr);   // transport indexes are unsigned
    REQUIRE(RowAt(rows, 0xFFFFFFFFll) == nullptr);
    REQUIRE(RowAt(rows, std::numeric_limits<std::int64_t>::max()) == nullptr);
    REQUIRE(RowAt(rows, std::numeric_limits<std::int64_t>::min()) == nullptr);

    const std::array<std::string, 6> &view = rows;
    const std::string *row = RowAt(view, 2);
    REQUIRE(row == &rows[2]);

    std::string *seventh = RowAt(rows, 6);
    if (seventh)
        *seventh = "never written";
    REQUIRE(seventh == nullptr);

    // Sparse reservations and later slot reuse leave every survivor in place.
    rows[0] = "HOST"; rows[2] = "TWO"; rows[5] = "FIVE";
    REQUIRE(World::NetLobby::ClearSlot(rows, 2));
    REQUIRE(rows[0] == "HOST");
    REQUIRE(rows[2].empty());
    REQUIRE(rows[4].empty());
    REQUIRE(rows[5] == "FIVE");
    rows[2] = "NEW";
    REQUIRE(rows[5] == "FIVE");
    REQUIRE(!World::NetLobby::ClearSlot(rows, -1));
    REQUIRE(!World::NetLobby::ClearSlot(rows, 6));
    REQUIRE(rows[2] == "NEW" && rows[5] == "FIVE");
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i] = std::to_string(i);
    REQUIRE(World::NetLobby::ClearSlot(rows, 2));
    for (size_t i = 0; i < rows.size(); ++i)
        REQUIRE(rows[i] == (i == 2 ? std::string() : std::to_string(i)));

    std::cout << "lobby rows are reached only inside the lobby\n";
    return 0;
}
