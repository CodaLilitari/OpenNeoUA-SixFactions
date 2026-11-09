// Lobby rows are indexed by the transport's stable slot, not a compact roster
// vector position. Slots may have holes after terminal departure, and own slot
// is -1 before acceptance. Validate every externally derived slot through RowAt.
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

} }
#endif
