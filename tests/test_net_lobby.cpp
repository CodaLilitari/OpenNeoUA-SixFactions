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

    std::cout << "lobby rows are reached only inside the lobby\n";
    return 0;
}
