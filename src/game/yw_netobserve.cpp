// Agent observation of the lobby and network session (see yw_netobserve.h).
#include "game/yw_netobserve.h"

#include "game/yw.h"
#include "game/yw_bot.h"
#include "game/yw_netauto.h"
#include "platform/windp.h"
#include "world/net_factions.h"

namespace NetObserve {
namespace {

const char *LobbyScreenName(int mode)
{
    switch (mode)
    {
    case UserData::NETSCREEN_MODE_SELECT:    return "provider";
    case UserData::NETSCREEN_SESSION_SELECT: return "session_select";
    case UserData::NETSCREEN_ENTER_NAME:     return "name";
    case UserData::NETSCREEN_ENTER_IP:       return "address";
    case UserData::NETSCREEN_CHOOSE_MAP:     return "map";
    case UserData::NETSCREEN_INSESSION:      return "session";
    default:                                 return "unknown";
    }
}

const char *PhaseName(World::NetBot::BotPhase phase)
{
    using World::NetBot::BotPhase;
    switch (phase)
    {
    case BotPhase::Lobby:      return "lobby";
    case BotPhase::Joining:    return "joining";
    case BotPhase::Ready:      return "ready";
    case BotPhase::InGame:     return "in_game";
    case BotPhase::Leaving:    return "leaving";
    case BotPhase::Gone:       return "gone";
    case BotPhase::Takeover:   return "takeover";
    case BotPhase::Reclaiming: return "reclaiming";
    default:                   return "unknown";
    }
}

const char *DifficultyName(World::NetBot::Difficulty difficulty)
{
    using World::NetBot::Difficulty;
    switch (difficulty)
    {
    case Difficulty::Cadet:   return "cadet";
    case Difficulty::Normal:  return "normal";
    case Difficulty::Veteran: return "veteran";
    default:                  return "unknown";
    }
}

int OwnerOfBit(uint32_t factionBit)
{
    const int owner = World::NetFactions::OwnerOf(factionBit);
    return owner < 0 ? 0 : owner;
}

} // namespace

void Fill(NC_STACK_ypaworld *world, Agent::NetState &out)
{
    out = Agent::NetState();
    if (!world || !world->_GameShell)
        return;
    const UserData &usr = *world->_GameShell;
    out.valid = true;
    out.lobbyScreen = LobbyScreenName(usr.netSelMode);
    out.menuOpen = usr.HasInited && usr.EnvMode == ENVMODE_NETPLAY;
    out.isHost = usr.isHost;
    out.levelId = usr.netLevelID;
    out.netGame = world->_isNetGame;
    out.started = world->_isNetGameStarted;
    out.localOwner = usr.netPlayerOwner;
    out.canStart = usr.isHost && usr.netGameCanStart;
    out.problem = usr.netProblem;

    NC_STACK_windp *driver = world->_netDriver;
    // Only a live transport object makes the current session view meaningful.
    if (driver && driver->zcon)
    {
        const size_t mode = driver->GetMode();
        out.role = mode == 1 ? "host" : mode == 2 ? "client" : "none";
        out.transport = driver->zenet ? "enet" : "zndnet";
        out.name = driver->GetMyName();
        out.playerIndex = driver->GetMyIndex();
        // Same rows, factions and flags as the lobby screen's player list.
        for (const TDPPlayerData &player : driver->GetPlayersData())
        {
            Agent::LobbyRow row;
            row.slot = static_cast<int>(player.Index);
            row.name = player.name;
            row.me = player.IsItMe();
            if (World::NetLobby::MatchRow(usr.lobbyPlayers, player.Index, usr.netSession.SessionId, player.ID))
            {
                const UserData::TNetPlayerLobbyData &lobby = usr.lobbyPlayers[player.Index];
                row.faction = OwnerOfBit(row.me ? usr.SelectedFraction : lobby.NetFraction);
                row.ready = lobby.Ready;
                row.welcomed = lobby.Welcomed;
                row.conflict = lobby.IsTrouble;
            }
            out.lobby.push_back(row);
        }
    }

    for (size_t owner = 0; owner < usr.netPlayers.size(); ++owner)
    {
        const UserData::TNetPlayerData &player = usr.netPlayers[owner];
        if (!usr.netSession.Matches(player.SessionId) || !player.PlayerId)
            continue;
        Agent::Seat seat;
        seat.owner = static_cast<int>(owner);
        seat.name = player.Name;
        seat.ready = player.Ready;
        seat.destroyFlags = player.DestroyFlags;
        seat.status = player.Status;
        seat.latencyMs = player.Latency;
        // Own messages never refresh the own seat's time, so it has no
        // meaningful silence (the watchdog skips it too).
        if (world->_isNetGameStarted && owner != usr.netPlayerOwner)
        {
            const long long silent = static_cast<long long>(world->_updateMessage.gTime) -
                                     static_cast<long long>(player.LastMessageTime);
            seat.silentMs = silent < 0 ? 0 : static_cast<int>(silent);
        }
        out.seats.push_back(seat);
    }

    // One bot per process today (-bot); the list leaves room for a process
    // that controls several owners.
    if (BotRuntime::Enabled())
    {
        const World::NetBot::BotSpec spec = BotRuntime::Spec();
        Agent::BotInfo bot;
        bot.faction = spec.owner;
        bot.difficulty = DifficultyName(spec.difficulty);
        bot.phase = PhaseName(BotRuntime::Phase());
        bot.owner = world->_isNetGameStarted ? world->GetPlayerOwner() : 0;
        out.bots.push_back(bot);
    }

    if (NetAuto::Active())
    {
        out.hasAuto = true;
        out.autoStep = NetAuto::StepName();
        out.autoError = NetAuto::Error();
    }
}

} // namespace NetObserve
