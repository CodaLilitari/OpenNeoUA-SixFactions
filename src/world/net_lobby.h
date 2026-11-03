// Lobby rows (UserData::lobbyPlayers) are indexed by transport user index,
// and that index comes from the transport, not from the lobby: an ENet
// resume skips the capacity check, so a seventh user can appear in a
// six-row lobby, and GetMyIndex() is -1 while this process is not in the
// user list. Every access with an index from the transport goes through
// RowAt, which yields nullptr for an index outside the rows.
#ifndef WORLD_NET_LOBBY_H_INCLUDED
#define WORLD_NET_LOBBY_H_INCLUDED

#include <cstddef>
#include <cstdint>

namespace World { namespace NetLobby {

template <typename Rows>
auto RowAt(Rows &rows, std::int64_t index) -> decltype(&rows[0])
{
    if (index < 0 || static_cast<std::uint64_t>(index) >= rows.size())
        return nullptr;
    return &rows[static_cast<std::size_t>(index)];
}

} }
#endif
