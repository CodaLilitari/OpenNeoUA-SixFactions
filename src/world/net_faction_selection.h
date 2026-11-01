#ifndef WORLD_NET_FACTION_SELECTION_H_INCLUDED
#define WORLD_NET_FACTION_SELECTION_H_INCLUDED

#include "net_factions.h"

namespace World { namespace NetFactions {
struct WelcomeSelection
{
    std::uint32_t Selected;
    std::uint32_t FreeMask;
    bool Announceable;

    constexpr bool CanAnnounce() const { return Announceable; }
};

// Preserve the existing peer collision policy, including its faction priority.
// Exhaustion produces an unselected lobby row, never a valid announcement.
static constexpr WelcomeSelection SelectionAfterWelcome(
    std::uint32_t selected, std::uint32_t welcomed, std::uint32_t freeMask)
{
    freeMask &= ~welcomed;
    if (selected == welcomed)
        selected = FirstAvailable(freeMask);
    return {selected, freeMask & ~selected, OwnerOf(selected) >= 0};
}
} }

#endif
