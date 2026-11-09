// Lobby rows are indexed by the transport's stable slot, not a compact roster
// vector position. Slots may have holes after terminal departure, and own slot
// is -1 before acceptance. Validate every externally derived slot through RowAt.
#ifndef WORLD_NET_LOBBY_H_INCLUDED
#define WORLD_NET_LOBBY_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <limits>

namespace World { namespace NetLobby {

// Authenticated provider identities, not faction owners or recovery permission.
struct SessionIdentity
{
    std::uint64_t SessionId = 0;
    std::uint64_t LocalPlayerId = 0;
    std::uint64_t HostPlayerId = 0;

    bool Valid() const
    {
        return SessionId != 0 && LocalPlayerId != 0 && HostPlayerId != 0;
    }

    bool Matches(std::uint64_t session) const
    {
        return Valid() && SessionId == session;
    }
};

template <typename Rows>
auto RowAt(Rows &rows, std::int64_t index) -> decltype(&rows[0])
{
    if (index < 0 || static_cast<std::uint64_t>(index) >= rows.size())
        return nullptr;
    return &rows[static_cast<std::size_t>(index)];
}

// Both transport terminal events and explicit kick/quit paths free exactly one
// stable slot. Never compact the surviving names, factions or readiness state.
template <typename Rows>
bool ClearSlot(Rows &rows, std::int64_t slot)
{
    auto *row = RowAt(rows, slot);
    if (!row)
        return false;
    *row = typename Rows::value_type{};
    return true;
}

// A stable slot may now hold a different participant. Names are display data;
// compare the event's own session/player tuple before any caller-side mutation.
template <typename Rows>
auto MatchRow(Rows &rows, std::int64_t slot, std::uint64_t sessionId,
              std::uint64_t playerId) -> decltype(&rows[0])
{
    auto *row = RowAt(rows, slot);
    if (!row || sessionId == 0 || playerId == 0 ||
        row->SessionId != sessionId || row->PlayerId != playerId)
        return nullptr;
    return row;
}

template <typename Rows>
std::int32_t FindPlayerSlot(const Rows &rows, std::uint64_t sessionId,
                            std::uint64_t playerId)
{
    if (sessionId == 0 || playerId == 0)
        return -1;
    std::int32_t found = -1;
    for (std::size_t slot = 0; slot < rows.size(); ++slot)
    {
        if (!MatchRow(rows, slot, sessionId, playerId))
            continue;
        if (found != -1 || slot > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            return -1;
        found = static_cast<std::int32_t>(slot);
    }
    return found;
}

// This only binds local row state. The caller must separately decide whether
// the provider identity is admitted in the current lobby/match lifecycle.
template <typename Rows, typename Name>
bool BindRow(Rows &rows, std::int64_t slot, std::uint64_t sessionId,
             std::uint64_t playerId, const Name &name)
{
    auto *row = RowAt(rows, slot);
    if (!row || sessionId == 0 || playerId == 0)
        return false;
    for (std::size_t other = 0; other < rows.size(); ++other)
        if (other != static_cast<std::size_t>(slot) &&
            MatchRow(rows, other, sessionId, playerId))
            return false;

    if (MatchRow(rows, slot, sessionId, playerId))
        row->Name = name;
    else
    {
        // Construct before assigning so name may safely refer to this row.
        typename Rows::value_type fresh{};
        fresh.SessionId = sessionId;
        fresh.PlayerId = playerId;
        fresh.Name = name;
        *row = fresh;
    }
    return true;
}

} }
#endif
