#ifndef WORLD_NET_FACTIONS_H_INCLUDED
#define WORLD_NET_FACTIONS_H_INCLUDED

#include <cstdint>
#include <cstddef>

namespace World { namespace NetFactions {
// Owner IDs remain the existing game/data IDs, not player-slot indices.
constexpr int MaxPlayers = 6;
constexpr int Owners[] = {1, 6, 3, 4, 5, 2};
constexpr std::uint32_t Mask = (1u << 1) | (1u << 6) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 2);
static constexpr int OwnerOf(std::uint32_t fraction)
{
    for (int owner : Owners)
        if (fraction == (1u << owner))
            return owner;
    return -1;
}
static constexpr std::uint32_t FirstAvailable(std::uint32_t mask)
{
    for (int owner : Owners)
        if (mask & (1u << owner))
            return 1u << owner;
    return 0;
}
static constexpr bool IsAvailable(std::uint32_t fraction, std::uint32_t mapMask, std::uint32_t takenMask = 0)
{
    return OwnerOf(fraction) >= 0 && (fraction & mapMask) != 0 && (fraction & takenMask) == 0;
}

struct RosterEntry
{
    int Slot;
    std::uint32_t Faction;
    bool Ready;
};

struct RosterCheck
{
    bool Valid = false;
    bool AllReady = false;
    std::uint32_t ConflictingSlots = 0;

    bool CanStart() const { return Valid && AllReady; }
};

// A snapshot must be checked again at launch: a previous UI result can be stale.
// Slot bits identify UI rows; faction bits retain the game's separate owner IDs.
static inline RosterCheck CheckRoster(std::uint32_t mapMask, const RosterEntry *entries, std::size_t count)
{
    RosterCheck result;
    if (!entries || count == 0 || count > MaxPlayers)
        return result;

    result.Valid = true;
    result.AllReady = true;
    std::uint32_t seenSlots = 0;
    int firstSlot[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    for (std::size_t i = 0; i < count; ++i)
    {
        const RosterEntry &entry = entries[i];
        if (entry.Slot < 0 || entry.Slot >= MaxPlayers)
        {
            result.Valid = false;
            continue;
        }
        const std::uint32_t slotBit = 1u << entry.Slot;
        if (seenSlots & slotBit)
        {
            result.Valid = false;
            result.ConflictingSlots |= slotBit;
        }
        seenSlots |= slotBit;
        result.AllReady = result.AllReady && entry.Ready;

        const int owner = OwnerOf(entry.Faction);
        if (owner < 0 || !(mapMask & entry.Faction))
        {
            result.Valid = false;
            result.ConflictingSlots |= slotBit;
        }
        else if (firstSlot[owner] >= 0)
        {
            result.Valid = false;
            result.ConflictingSlots |= slotBit | (1u << firstSlot[owner]);
        }
        else
            firstSlot[owner] = entry.Slot;
    }
    return result;
}

static constexpr int HostVehicle(int owner, int authoredVehicle)
{
    switch (owner)
    {
    case 1: return 56;
    case 6: return 57;
    case 3: return 58;
    case 4: return 60;
    case 5:
    case 2: return authoredVehicle; // never guess a proprietary/mod prototype ID
    default: return 0;
    }
}
} }
#endif
