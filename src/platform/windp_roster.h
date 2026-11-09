// Provider-facing view of transport reservations. A slot is a stable identity
// within a session, never the position of a row in a compact vector.
#ifndef WINDP_ROSTER_H_INCLUDED
#define WINDP_ROSTER_H_INCLUDED

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "../netlib/transport_types.h"

struct TDPPlayerData
{
    enum NETPLAYERDATA
    {
        NPD_ITSME = (1 << 0),
        NPD_AWAY = (1 << 1),
        NPD_HOST = (1 << 2),
    };

    std::uint32_t Index = 0;
    std::uint64_t ID = 0;
    std::string name;
    std::uint32_t flags = 0;

    bool IsItMe() const { return (flags & NPD_ITSME) != 0; }
    bool IsOnline() const { return (flags & NPD_AWAY) == 0; }
    bool IsHost() const { return (flags & NPD_HOST) != 0; }
};

namespace WindpRoster {

constexpr std::size_t MaxSlots = 6;

struct SessionIdentity
{
    std::uint64_t sessionId = 0;
    std::uint64_t ownPlayerId = 0;
    std::uint64_t hostPlayerId = 0;

    bool Valid() const { return sessionId && ownPlayerId && hostPlayerId; }
};

// The transport publishes a complete validated snapshot. Still fail atomically
// if a malformed local caller reaches this boundary; don't expose a partial list.
template <typename Users>
bool Convert(const Users &users, std::uint64_t ownId, std::vector<TDPPlayerData> &out)
{
    if (users.size() > MaxSlots)
        return false;
    std::array<bool, MaxSlots> used{};
    std::vector<TDPPlayerData> next;
    next.reserve(users.size());
    std::uint64_t hostId = 0;
    for (const auto &user : users)
    {
        if (!user.ID || user.name.empty() || user.slot >= MaxSlots || used[user.slot] ||
            (user.status != ZNDNet::RosterOnline && user.status != ZNDNet::RosterAway))
            return false;
        for (const auto &previous : next)
            if (previous.ID == user.ID)
                return false;
        used[user.slot] = true;
        TDPPlayerData player;
        player.Index = user.slot;
        player.ID = user.ID;
        player.name = user.name;
        if (user.ID == ownId)
            player.flags |= TDPPlayerData::NPD_ITSME;
        if (user.status == ZNDNet::RosterAway)
            player.flags |= TDPPlayerData::NPD_AWAY;
        if (user.lead)
        {
            if (hostId || user.slot != 0 || user.status != ZNDNet::RosterOnline)
                return false;
            hostId = user.ID;
            player.flags |= TDPPlayerData::NPD_HOST;
        }
        next.push_back(std::move(player));
    }
    if (!next.empty() && !hostId)
        return false;
    std::sort(next.begin(), next.end(), [](const TDPPlayerData &a, const TDPPlayerData &b) {
        return a.Index < b.Index;
    });
    out.swap(next);
    return true;
}

inline const TDPPlayerData *AtSlot(const std::vector<TDPPlayerData> &players, std::uint32_t slot)
{
    if (slot >= MaxSlots)
        return nullptr;
    for (const auto &player : players)
        if (player.Index == slot)
            return &player;
    return nullptr;
}

inline std::int32_t OwnSlot(const std::vector<TDPPlayerData> &players)
{
    for (const auto &player : players)
        if (player.IsItMe())
            return static_cast<std::int32_t>(player.Index);
    return -1;
}

inline const TDPPlayerData *ByID(const std::vector<TDPPlayerData> &players, std::uint64_t id)
{
    if (id)
        for (const auto &player : players)
            if (player.ID == id)
                return &player;
    return nullptr;
}


} // namespace WindpRoster
#endif
