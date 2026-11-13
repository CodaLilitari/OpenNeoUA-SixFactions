#include "world/net_factions.h"
#include "world/net_protocol.h"
#include "world/net_lobby.h"
// Multiplayer provider layer (sessions, players, messages) used by the network game.
#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "common/includes.h"
#include "system/inpt.h"
#include <cstdlib>
#include "platform/windp.h"
#include "gui/button.h"
#include "game/yw_net.h"
#include "system/inivals.h"
#include "locale/locale.h"

static_assert(WindpRoster::MaxSlots == World::CVMaxNetPlayers, "provider and lobby slot limits agree");

size_t NC_STACK_windp::Init(IDVList &stak)
{
    if ( !NC_STACK_network::Init(stak))
        return 0;

    if (!init())
    {
        Delete();
        return 0;
    }

    System::IniConf::ReadFromNucleusIni();
    guaranteed_md = System::IniConf::NetGmode.Get<int>();
    version_check = System::IniConf::NetVersionCheck.Get<bool>();
    debug = System::IniConf::GameDebug.Get<bool>();
    return 1;
}

void NC_STACK_windp::_clear()
{
    for(int i = 0; i < MAX_PROVIDERS; i++)
        providers[i].clear();

    ClearConnectionState();

    _myName = "";

    version_ident = "";
    version_check = 0;

    norm_block = nullptr;

    norm_size = 0;

    guaranteed_md = 0;

    debug = 0;

    zcli = nullptr;
    zhost = nullptr;
    zcon = nullptr;
    zenet = nullptr;
}

// Everything a connection leaves behind besides the transport objects. After
// Reset() the provider has no connection: connType 0 makes IsConnected,
// IsJoined, Connect and Host refuse instead of dereferencing a transport that
// was deleted, and the next session starts without the old roster, ids,
// session or queued broadcast bytes. The requested callsign (_myName) stays.
void NC_STACK_windp::ClearConnectionState()
{
    connType = 0;
    _hasLobby = false;
    _myID = 0;
    _myIndex = -1;
    _rawUsers.clear();
    _players.clear();
    _rawSessions.clear();
    _nextUsersGet = 0;
    _nextSessionsGet = 0;
    _sesID = 0;
    _sesName.clear();
    broadcastBuff_off = 0;
    _recovery.Clear();
    _recoveryFailurePending = false;
    _sessionIdentity = WindpRoster::SessionIdentity{};
    _identityRejected = false;
    _rosterValid = false;
}

bool NC_STACK_windp::init()
{
    _clear();

    norm_block = (char *)malloc(0x10000);
    if (!norm_block)
    {
        deinit();
        return false;
    }

    norm_size = 0x10000;

    broadcastBuff.resize(CONF_SEND_SIZE);

    if (broadcastBuff.empty())
    {
        deinit();
        return false;
    }

    return true;
}

void NC_STACK_windp::deinit()
{

}

size_t NC_STACK_windp::Deinit()
{
    return NC_STACK_nucleus::Deinit();
}


size_t NC_STACK_windp::SetMode(bool hosting)
{
    // A transport left over from an earlier session would leak with its
    // threads (and, for a host, keep the port bound).
    if (zcon)
        Reset(nullptr);

    if (hosting)
    {
        connType = 1;
        const char *transportEnv = std::getenv("OPENNEOUA_TRANSPORT");
        if (transportEnv && std::string(transportEnv) == "enet")
        {
            zenet = new ZNDNet::ZNDEnet(World::NetProtocol::ServiceId);
            zcon = zenet;
            return 1;
        }
        zhost = new ZNDNet::ZNDSingle(World::NetProtocol::ServiceId);
        zcon = zhost;
    }
    else
    {
        connType = 2;
        const char *transportEnv = std::getenv("OPENNEOUA_TRANSPORT");
        if (transportEnv && std::string(transportEnv) == "enet")
        {
            zenet = new ZNDNet::ZNDEnet(World::NetProtocol::ServiceId);
            zcon = zenet;
            return 1;
        }
        zcli = new ZNDNet::ZNDClient(World::NetProtocol::ServiceId);
        zcon = zcli;
    }
    return 1;
}

size_t NC_STACK_windp::GetMode()
{
    return connType;
}

void NC_STACK_windp::SetWantedName(const std::string &name)
{
    _myName = name;
}

std::string NC_STACK_windp::GetMyName()
{
    return _myName;
}

bool NC_STACK_windp::Connect(const std::string &connString)
{
    if (connType != 2 || (!zenet && !zcli))
        return false;

    // A deliberate new connection terminates any previous reservation. Only
    // StartRecoveryAttempt may carry a credential into another transport.
    if (zenet) zenet->SendDisconnect(); else zcli->SendDisconnect();
    StopClientTransport(true);
    ClearConnectionState();
    connType = 2;

    IPaddress server;
    if ( SDLNet_ResolveHost(&server, connString.c_str(), 61234) != 0)
    {
        _connectFailure = ZNDNet::CONNFAIL_ADDRESS;
        log_netlog("Connection to %s failed: %s (the address does not resolve)\n",
                   connString.c_str(), ZNDNet::ConnectFailureName(_connectFailure));
        return false;
    }

    _connectFailure = ZNDNet::CONNFAIL_NONE;
    if (zenet) zenet->Start(_myName, server); else zcli->Start(_myName, server);

    const uint32_t startedAt = SDL_GetTicks();
    bool waitConn = true;

    while (waitConn)
    {
        SDL_Delay(5);

        if (uint32_t(SDL_GetTicks() - startedAt) >= CONF_CONN_WAIT)
        {
            StopClientTransport(true);
            _connectFailure = ZNDNet::CONNFAIL_NO_ANSWER;
            log_netlog("Connection to %s failed: %s (nothing within %d ms)\n",
                       connString.c_str(), ZNDNet::ConnectFailureName(_connectFailure), (int)CONF_CONN_WAIT);
            return false;
        }

        for(ZNDNet::Event *evt = zcon->Events_PopSystem(); evt != nullptr; evt = zcon->Events_PopSystem())
        {
            switch( evt->type )
            {
                case ZNDNet::EVENT_CONNECTED:
                    {
                        ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
                        if (!dat->id || dat->id != zcon->GetMyID() ||
                            !evt->sessionId || evt->sessionId != zcon->GetMySesID())
                        {
                            delete evt;
                            StopClientTransport(true);
                            return false;
                        }
                        _myName = dat->name;
                        _hasLobby = dat->value == 1 ? true : false;
                        _myID = dat->id;

                        if (_hasLobby)
                            waitConn = false;
                    }
                    break;

                case ZNDNet::EVENT_SESSION_JOIN:
                {
                    ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
                    if (!dat->id || dat->id != evt->sessionId || dat->id != zcon->GetMySesID())
                    {
                        delete evt;
                        StopClientTransport(true);
                        return false;
                    }
                    _sesID = dat->id;
                    _sesName = dat->name;
                    waitConn = false;
                }
                break;

                case ZNDNet::EVENT_DISCONNECT:
                case ZNDNet::EVENT_CONNERR:
                    {
                        // The refusal reason used to be dropped; the UI still
                        // says "Time OUT", the netlog now says why.
                        _connectFailure = evt->type == ZNDNet::EVENT_DISCONNECT
                            ? ZNDNet::CONNFAIL_DROPPED : ZNDNet::ConnectFailureFromRefusal(evt->value);
                        log_netlog("Connection to %s as %s failed: %s (transport code %u)\n",
                                   connString.c_str(), _myName.c_str(),
                                   ZNDNet::ConnectFailureName(_connectFailure), (unsigned)evt->value);
                        delete evt;
                        StopClientTransport(true);
                        return false;
                    }
                    break;

                default:
                    break;
            }
            delete evt;
        }
    }

    log_netlog("Connected to %s as %s\n", connString.c_str(), _myName.c_str());
    ReloadUsersList();
    ReloadSessionsList();

    if (!RememberConnection(server, connString))
    {
        _connectFailure = ZNDNet::CONNFAIL_RESUME;
        if (zenet) zenet->SendDisconnect(); else zcli->SendDisconnect();
        StopClientTransport(true);
        _recovery.Terminal(_connectFailure);
        return false;
    }

    return true;
}

bool NC_STACK_windp::RememberConnection(const IPaddress &address, const std::string &text)
{
    if (!zcon || connType != 2 || !IsConnected() || !HasValidRoster())
        return false;
    WindpResume::Record record;
    record.kind = zenet ? WindpResume::Transport::Enet : WindpResume::Transport::Znd;
    record.addressHost = address.host;
    record.addressPort = address.port;
    record.addressText = text;
    record.name = _myName;
    record.id = _myID;
    record.session = _sesID;
    record.graceMs = zcon->GetResumeGraceMs();
    return zcon->GetResumeToken(record.token) && _myIndex >= 0 &&
           zcon->GetMyID() == _myID && zcon->GetMySesID() == _sesID &&
           _recovery.Remember(record);
}

void NC_STACK_windp::StopClientTransport(bool terminal)
{
    // Terminal session events also invalidate a hosting provider's cached
    // authority. Only the transport stop/token operations below are client-only.
    _rosterValid = false;
    if (terminal && _sessionIdentity.Valid())
        _identityRejected = true;
    broadcastBuff_off = 0;
    if (connType != 2 || !zcon)
        return;
    zcon->Stop();
    if (terminal)
    {
        zcon->ClearResumeToken();
        zcon->SetResumeOnly(false);
    }
}

void NC_STACK_windp::SetRecoveryEnabled(bool enabled)
{
    const bool wasRecovering = _recovery.Recovering();
    _recovery.Enable(enabled);
    if (wasRecovering && !_recovery.Recovering())
    {
        StopClientTransport(true);
        _recoveryFailurePending = true;
    }
}

bool NC_STACK_windp::CompleteRecovery()
{
    // A network acceptance alone must never reopen game sends. The caller is
    // the game's validated host checkpoint/owner-epoch completion path.
    // A completion arriving between receive frames still obeys the same
    // deadline; it cannot reopen gameplay after the grace period expired.
    TickRecovery();
    return IsConnected() && HasValidRoster() && _recovery.Reclaimed();
}

WindpResume::Phase NC_STACK_windp::RecoveryPhase() const
{
    return _recovery.GetPhase();
}

bool NC_STACK_windp::CanSendGameplay()
{
    if (!zcon || !HasValidRoster())
        return false;
    if (connType == 1)
        return _myID != 0 && zcon->GetMyID() == _myID;
    return connType == 2 && IsConnected() && _recovery.CanSendGameplay();
}

bool NC_STACK_windp::StartRecoveryAttempt()
{
    const WindpResume::Record record = _recovery.Saved();
    if (connType != 2 || !record.Valid())
        return false;

    // Stop/join and replace the failed client, without terminal goodbye or a
    // provider Reset. The retained record belongs to the provider, not it.
    StopClientTransport(false);
    delete zcon;
    zcon = nullptr;
    zcli = nullptr;
    zenet = nullptr;
    if (record.kind == WindpResume::Transport::Enet)
    {
        zenet = new ZNDNet::ZNDEnet(World::NetProtocol::ServiceId);
        zcon = zenet;
    }
    else
    {
        zcli = new ZNDNet::ZNDClient(World::NetProtocol::ServiceId);
        zcon = zcli;
    }
    if (!zcon->SetResumeToken(record.token) || !zcon->SetResumeOnly(true) ||
        !zcon->SetResumeGraceMs(record.graceMs))
        return false;
    IPaddress address;
    address.host = record.addressHost;
    address.port = record.addressPort;
    if (zenet) zenet->Start(record.name, address); else zcli->Start(record.name, address);
    return true;
}

void NC_STACK_windp::TickRecovery()
{
    switch (_recovery.Poll(SDL_GetTicks()))
    {
    case WindpResume::Action::StartAttempt:
        if (!StartRecoveryAttempt())
        {
            StopClientTransport(true);
            _recovery.Terminal(ZNDNet::CONNFAIL_RESUME);
            _recoveryFailurePending = true;
        }
        break;
    case WindpResume::Action::StopAttempt:
        StopClientTransport(false);
        break;
    case WindpResume::Action::Fail:
        StopClientTransport(true);
        _recoveryFailurePending = true;
        break;
    case WindpResume::Action::None:
        break;
    }
}

bool NC_STACK_windp::IsConnected()
{
    if (connType != 2)
        return false;

    if (zenet) return zenet->GetStatus() == ZNDNet::NetUser::STATUS_CONNECTED;
    return zcli && zcli->GetStatus() == ZNDNet::NetUser::STATUS_CONNECTED;
}

bool NC_STACK_windp::HasLobby()
{
    if (!IsConnected())
        return false;

    return _hasLobby;
}

bool NC_STACK_windp::IsJoined()
{
    if (connType != 2 || !zcon)
        return false;

    return zcon->GetMySesID() != 0;
}


bool NC_STACK_windp::Host(const std::string &gameName, int playerNum)
{
    if (connType != 1 || (!zenet && !zhost))
        return false;

    // Choosing another map deliberately creates a new session. Both host
    // adapters stop/join their old session and clear its queued events in Start.
    // Preserve the selected adapter and callsign, but do not compare the fresh
    // tuple against the previous session's pinned authority.
    ClearConnectionState();
    connType = 1;
    if (zenet) zenet->Start(_myName, 61234, gameName, playerNum); else zhost->Start(_myName, 61234, gameName, playerNum);
    _myID = zcon->GetMyID();
    return RefreshPlayers();
}



size_t NC_STACK_windp::EnumSessions(IDVPair *stak)
{
    //ask sessions
    
    return 0;
}

size_t NC_STACK_windp::GetSessionName(windp_getNameMsg *sessName)
{
    if (sessName->id < _rawSessions.size())
    {
        sessName->name = _rawSessions.at(sessName->id).name;
        return 1;
    }
    return 0;
}

std::string NC_STACK_windp::GetCurrentSessionName()
{
    return _sesName;
}

size_t NC_STACK_windp::JoinSession(const std::string &sessName)
{
    //join session
    
    return 0;
}

size_t NC_STACK_windp::CreateSession(windp_openSessionMsg *osm)
{
    //open session
    
    return 0;
}

size_t NC_STACK_windp::CloseSession(IDVPair *stak)
{
    //close session
    if (connType == 1)
    {
        if (zenet)
            zenet->CloseSession(0);
        else if (zhost)
            zhost->CloseSession(0);
    }
    
    return 0;
}

size_t NC_STACK_windp::GetSessionStatus()
{
    if (!zcon)
        return SESSION_STATUS_NONE;

    return SESSION_STATUS_NONE;
}

size_t NC_STACK_windp::SetSessionName(const char *sessName)
{
    //set session name
    
    return 0;
}

size_t NC_STACK_windp::CreatePlayer(windp_createPlayerMsg *cp)
{
    //create player
    
    return 0;
}

size_t NC_STACK_windp::DeletePlayer(const char *playerName)
{
    //destroy player
    
    return 0;
}


bool NC_STACK_windp::GetPlayerData(uint32_t index, TDPPlayerData *out) const
{
    const TDPPlayerData *player = WindpRoster::AtSlot(_players, index);
    if (!player || !out)
        return false;
    *out = *player;
    return true;
}

std::vector<TDPPlayerData> NC_STACK_windp::GetPlayersData() const
{
    return _players;
}

bool NC_STACK_windp::GetSessionIdentity(WindpRoster::SessionIdentity *out) const
{
    if (!out)
        return false;
    *out = WindpRoster::SessionIdentity{};
    if (_identityRejected || !_sessionIdentity.Valid())
        return false;
    *out = _sessionIdentity;
    return true;
}

bool NC_STACK_windp::HasValidRoster() const
{
    // ENet's legacy status reports a running host as CONNECTING; unlike its
    // historical identity getters, DISCONNECTED still reliably means stopped.
    if (connType == 1 && zenet && zenet->GetStatus() == ZNDNet::NetUser::STATUS_DISCONNECTED)
        return false;
    if (connType != 1 && (connType != 2 ||
        (zenet ? zenet->GetStatus() != ZNDNet::NetUser::STATUS_CONNECTED
               : !zcli || zcli->GetStatus() != ZNDNet::NetUser::STATUS_CONNECTED)))
        return false;
    const TDPPlayerData *self = WindpRoster::ByID(_players, _sessionIdentity.ownPlayerId);
    return _rosterValid && !_identityRejected && _sessionIdentity.Valid() && self && self->IsOnline() &&
           zcon && zcon->GetMyID() == _sessionIdentity.ownPlayerId &&
           zcon->GetMySesID() == _sessionIdentity.sessionId;
}

bool NC_STACK_windp::SendToPlayer(void *data, size_t size, uint64_t playerId, bool guarantee)
{
    if (!data || !size || !CanSendGameplay())
        return false;
    const TDPPlayerData *target = WindpRoster::ByID(_players, playerId);
    if (!target || !target->IsOnline())
        return false;
    FlushBroadcastBuffer();
    zcon->SendData(playerId, data, size, guarantee ? ZNDNet::PKT_FLAG_GARANT : 0);
    return true;
}

bool NC_STACK_windp::KickPlayer(uint64_t playerId)
{
    if (connType != 1 || !CanSendGameplay() ||
        _sessionIdentity.ownPlayerId != _sessionIdentity.hostPlayerId ||
        playerId == _sessionIdentity.ownPlayerId || !WindpRoster::ByID(_players, playerId))
        return false;
    zcon->KickUser(playerId);
    return true; // The request was issued; lifecycle events confirm removal.
}

int32_t NC_STACK_windp::GetMyIndex() const
{
    return _myIndex;
}

std::string NC_STACK_windp::GetPlayerName(uint32_t index) const
{
    const TDPPlayerData *player = WindpRoster::AtSlot(_players, index);
    return player ? player->name : std::string();
}

int32_t NC_STACK_windp::GetPlayerIndex(const std::string &name) const
{
    for (const TDPPlayerData &player : _players)
    {
        if ( !StriCmp(player.name, name) )
            return static_cast<int32_t>(player.Index);
    }
    return -1;
}

bool NC_STACK_windp::IsPlayer(const std::string &name) const
{
    for(const TDPPlayerData &p : _players)
    {
        if ( !StriCmp(p.name, name) )
            return true;
    }
    return false;
}


bool NC_STACK_windp::Broadcast(void *data, size_t dataSize, bool garantee)
{
    return QueueBroadcast(data, dataSize, garantee);
}

bool NC_STACK_windp::Send(void *data, size_t dataSize, const std::string &recvID, bool garantee)
{
    FlushBroadcastBuffer();

    return SendLowLevel(data, dataSize, recvID, garantee);
}

size_t NC_STACK_windp::QueueBroadcast(void *data, size_t dataSize, bool garantee)
{
    if (!CanSendGameplay())
    {
        broadcastBuff_off = 0;
        return 0;
    }

    if ( broadcastBuff_off + dataSize > broadcastBuff.size() )
        FlushBroadcastBuffer();

    if ( dataSize > broadcastBuff.size() )
        return BroadcastLowLevel(data, dataSize, garantee);

    std::memcpy( broadcastBuff.data() + broadcastBuff_off, data, dataSize );
    broadcastBuff_off += dataSize;

    return dataSize;
}

size_t NC_STACK_windp::BroadcastLowLevel(void *data, size_t dataSize, bool garantee)
{
    if (!CanSendGameplay())
        return 0;

    zcon->BroadcastData(data, dataSize, (garantee ? ZNDNet::PKT_FLAG_GARANT : 0));

    return dataSize;
}

size_t NC_STACK_windp::SendLowLevel(void *data, size_t dataSize, const std::string &recvID, bool garantee)
{
    if (!CanSendGameplay())
        return 0;

    uint64_t id = GetUserID(recvID);
    if (!id)
        return 0;

    zcon->SendData(id, data, dataSize, (garantee ? ZNDNet::PKT_FLAG_GARANT : 0));

    return dataSize;
}

bool NC_STACK_windp::Recv(windp_recvMsg *recv)
{
    if (!recv)
        return false;
    *recv = windp_recvMsg{};
    TickRecovery();
    if (_recoveryFailurePending)
    {
        _recoveryFailurePending = false;
        recv->msgType = RECVMSG_RECOVERYFAILED;
        recv->reason = _recovery.Failure();
        recv->sessionId = _sessionIdentity.sessionId;
        return true;
    }
    if (!zcon)
        return false;

    ZNDNet::Event *evt = zcon->Events_Pop();

    if (!evt)
        return false;

    recv->sessionId = evt->sessionId;
    const uint64_t livePlayerId = zcon->GetMyID();
    const uint64_t liveSessionId = zcon->GetMySesID();
    if (!_identityRejected && _sessionIdentity.Valid() &&
        ((livePlayerId && livePlayerId != _sessionIdentity.ownPlayerId) ||
         (liveSessionId && liveSessionId != _sessionIdentity.sessionId)))
        RejectSessionIdentity();
    // A refusal before admission can have no session. It belongs to the current
    // provider-owned attempt, not to a peer's gameplay/lifecycle stream.
    const bool unadmittedRefusal = evt->type == ZNDNet::EVENT_CONNERR && !evt->sessionId &&
        connType == 2 && _recovery.GetPhase() == WindpResume::Phase::Connecting;
    if (_identityRejected || (_sessionIdentity.Valid() &&
        evt->sessionId != _sessionIdentity.sessionId && !unadmittedRefusal))
    {
        recv->msgType = RECVMSG_SYSUNK;
        delete evt;
        return true;
    }

    switch( evt->type )
    {
        case ZNDNet::EVENT_CONNECTED:
        {
            ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
            if (_identityRejected || !dat->id || dat->id != zcon->GetMyID() ||
                !evt->sessionId || evt->sessionId != zcon->GetMySesID())
            {
                recv->msgType = RECVMSG_SYSUNK;
                break;
            }
            _myName = dat->name;
            _myID = dat->id;
            _hasLobby = dat->value == 1;
        }
        break;
        case ZNDNet::EVENT_SESSION_LIST:
        {
            if (zenet)
                zenet->GetSessions(_rawSessions);
            else if (zcli)
                zcli->GetSessions(_rawSessions);
        }
        break;

        case ZNDNet::EVENT_SESSION_JOIN:
        {
            ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
            if (_identityRejected || !dat->id || dat->id != evt->sessionId ||
                dat->id != zcon->GetMySesID())
            {
                recv->msgType = RECVMSG_SYSUNK;
                break;
            }
            _sesID = dat->id;
            _sesName = dat->name;
            RefreshPlayers();
        }
        break;

        case ZNDNet::EVENT_USER_ADD:
        {
            ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
            RefreshPlayers();
            if (!_rosterValid)
            {
                recv->msgType = RECVMSG_SYSUNK;
                break;
            }

            recv->msgType = dat->value == ZNDNet::JoinFresh ? RECVMSG_CREATEPLAYER
                : dat->value == ZNDNet::JoinResumed ? RECVMSG_PLAYERBACK : RECVMSG_SYSUNK;
            recv->senderName = dat->name;
            recv->playerId = dat->id;
            recv->playerSlot = dat->slot < WindpRoster::MaxSlots ? int32_t(dat->slot) : -1;
            recv->reason = dat->value;
            if (recv->playerSlot < 0 || !recv->playerId)
                recv->msgType = RECVMSG_SYSUNK;
        }
        break;

        case ZNDNet::EVENT_USER_LEAVE:
        {
            ZNDNet::EventNameID *dat = (ZNDNet::EventNameID *)evt;
            RefreshPlayers();
            if (!_rosterValid)
            {
                recv->msgType = RECVMSG_SYSUNK;
                break;
            }

            recv->msgType = dat->value == ZNDNet::LeaveAway ? RECVMSG_PLAYERAWAY
                : dat->value == ZNDNet::LeaveLeft || dat->value == ZNDNet::LeaveKicked ||
                  dat->value == ZNDNet::LeaveExpired ? RECVMSG_PLAYERGONE : RECVMSG_SYSUNK;
            recv->senderName = dat->name;
            // The slot belongs to this event. A later complete roster may
            // already contain a different player who reused the same slot.
            recv->playerId = dat->id;
            recv->playerSlot = dat->slot < WindpRoster::MaxSlots ? int32_t(dat->slot) : -1;
            recv->reason = dat->value;
            if (recv->playerSlot < 0 || !recv->playerId)
                recv->msgType = RECVMSG_SYSUNK;
        }
        break;

        case ZNDNet::EVENT_DATA:
        {
            ZNDNet::EventData *dat = (ZNDNet::EventData *)evt;

            if (!dat->data || !dat->size)
                break;

            recv->playerId = dat->from;
            const TDPPlayerData *sender = WindpRoster::ByID(_players, dat->from);
            if (!HasValidRoster() || !_sessionIdentity.Valid() ||
                evt->sessionId != _sessionIdentity.sessionId || !sender || !sender->IsOnline())
            {
                recv->msgType = RECVMSG_SYSUNK;
                break;
            }

            recv->msgType = RECVMSG_NORMAL;
            recv->_data.assign(dat->data, dat->data + dat->size);
            recv->data = recv->_data.data();
            recv->size = recv->_data.size();
            recv->senderName = sender->name;
            recv->playerSlot = static_cast<int32_t>(sender->Index);
            recv->cast = dat->cast;
        }
        break;

        case ZNDNet::EVENT_USER_LIST:
        {
            RefreshPlayers();
            if (!_rosterValid)
                recv->msgType = RECVMSG_SYSUNK;
            else if (evt->value == 1)
                recv->msgType = RECVMSG_ROSTERRESET;
        }
        break;

        case ZNDNet::EVENT_SELF_RESUMED:
        {
            if (connType == 2 && RefreshPlayers() && _recovery.Accepted(_myID, _sesID))
                recv->msgType = RECVMSG_SELFRESUMED;
            else
            {
                _recovery.Terminal(ZNDNet::CONNFAIL_RESUME);
                StopClientTransport(true);
                recv->msgType = RECVMSG_RECOVERYFAILED;
                recv->reason = _recovery.Failure();
            }
        }
        break;

        case ZNDNet::EVENT_SESSION_END:
        {
            _recovery.Terminal(ZNDNet::CONNFAIL_CLOSED);
            StopClientTransport(true);
            recv->msgType = RECVMSG_SESSIONEND;
            recv->reason = evt->value;
        }
        break;

        case ZNDNet::EVENT_CONNERR:
        {
            if (!recv->sessionId && _recovery.Saved().Valid())
                recv->sessionId = _recovery.Saved().session; // Local attempt context.
            const int failure = ZNDNet::ConnectFailureFromRefusal(evt->value);
            const bool retry = _recovery.AttemptFailed(SDL_GetTicks(), failure);
            StopClientTransport(!retry);
            if (!retry)
            {
                recv->msgType = RECVMSG_RECOVERYFAILED;
                recv->reason = failure;
            }
        }
        break;

        case ZNDNet::EVENT_DISCONNECT:
        {
            // Only an unexplained timeout can resume. Kick, leave and known
            // session shutdown are terminal even if a token was retained.
            const bool retry = evt->value == ZNDNet::ERR_DISCONNECT_TIMEOUT &&
                               _recovery.Lost(SDL_GetTicks());
            if (!retry && _recovery.GetPhase() != WindpResume::Phase::Failed)
                _recovery.Terminal(ZNDNet::CONNFAIL_DROPPED);
            StopClientTransport(!retry);
            recv->msgType = retry ? RECVMSG_SELFLOST : RECVMSG_RECOVERYFAILED;
            recv->reason = evt->value;
        }
        break;

        default:
            break;
    }
    delete evt;

    return true;
}

size_t NC_STACK_windp::FlushBroadcastBuffer()
{
    if (!CanSendGameplay())
    {
        broadcastBuff_off = 0;
        return 0;
    }
    // flush buffer
    if (broadcastBuff_off == 0)
        return 0;

    size_t ret = BroadcastLowLevel(broadcastBuff.data(), broadcastBuff_off, true);

    if ( ret )
        broadcastBuff_off = 0;

    return ret;
}

bool NC_STACK_windp::DrainSends(uint32_t maxMs)
{
    FlushBroadcastBuffer();

    if (!zcon)
        return true;

    return zcon->WaitSendIdle(maxMs);
}

size_t NC_STACK_windp::LockSession(int *arg)
{
    if (!arg || connType != 1 || !zcon)
        return 0;
    zcon->ShowSession(*arg == 0);
    return 1;
}

size_t NC_STACK_windp::Reset(IDVPair *stak)
{
    if (zenet)
    {
        if (connType == 2) zenet->SendDisconnect();
        else zenet->CloseSession(0);
        zenet->Stop();
        delete zenet;
        zenet = nullptr;
    }
    else if (zcli)
    {
        zcli->SendDisconnect();
        zcli->Stop();
        delete zcli;
    }
    else if (zhost)
    {
        zhost->CloseSession(0);
        zhost->Stop();
        delete zhost;
    }

    zcon = nullptr;
    zcli = nullptr;
    zhost = nullptr;
    ClearConnectionState();

    return 0;
}

uint32_t NC_STACK_windp::GetPlayerCount() const
{
    return static_cast<uint32_t>(_players.size());
}

bool NC_STACK_windp::GetRemoteStart(windp_arg87 *arg)
{
    //check remote start
    arg->isClient = false;
    arg->isHoster = false;

    if (!zcon)
        return true;

    if (zcon == zhost)
    {
        arg->isHoster = true;
        return true;
    }

    if (zcon == zcli)
    {
        arg->isClient = true;
        return true;
    }

    return 1;
}

void NC_STACK_windp::SetVersion(const std::string &ver)
{
    //set session ident
    version_ident = ver;
}

size_t NC_STACK_windp::GetProvType()
{
    // get provider type
    
    return connType;
}

size_t NC_STACK_windp::GetStats(int *stak)
{
    // diagnos
    
    return 0;
}


void NC_STACK_windp::ReloadUsersList()
{
    if (connType > 0 && zcon && SDL_GetTicks() > _nextUsersGet)
    {
        _nextUsersGet = SDL_GetTicks() + CONF_USERS_GET_TIME;
        RefreshPlayers();
    }
}

void NC_STACK_windp::UpdateOwnPlayerIndex()
{
    _rosterValid = false;
    if (_identityRejected)
    {
        _players.clear();
        _myIndex = -1;
        return;
    }
    const uint64_t liveId = zcon ? zcon->GetMyID() : 0;
    if (!WindpRoster::Convert(_rawUsers, liveId ? liveId : _myID, _players))
    {
        // No partial or stale roster may be used to enable a match launch.
        _players.clear();
        log_netlog("Invalid transport reservation roster ignored.\n");
    }
    _myIndex = WindpRoster::OwnSlot(_players);
    if (!_players.empty())
        _rosterValid = AcceptTransportIdentity();
}

bool NC_STACK_windp::RefreshPlayers()
{
    ZNDNet::UserInfoVect next;
    if (!zcon || !zcon->GetUsers(next))
    {
        _rawUsers.clear();
        _players.clear();
        _myIndex = -1;
        _rosterValid = false;
        return false;
    }
    _rawUsers.swap(next);
    UpdateOwnPlayerIndex();
    return HasValidRoster();
}

void NC_STACK_windp::RejectSessionIdentity()
{
    // Keep the original tuple for diagnosis, but never expose it as authority
    // after contradictory validated state. Only a deliberate fresh reset clears
    // this latch; an ordinary roster refresh cannot migrate the session host.
    _identityRejected = true;
    _rosterValid = false;
    _players.clear();
    _myIndex = -1;
    broadcastBuff_off = 0;
    _recovery.Terminal(ZNDNet::CONNFAIL_RESUME);
    StopClientTransport(true);
    _recoveryFailurePending = true;
}

bool NC_STACK_windp::AcceptTransportIdentity()
{
    if (!zcon || _identityRejected)
        return false;
    WindpRoster::SessionIdentity candidate;
    candidate.sessionId = zcon->GetMySesID();
    candidate.ownPlayerId = zcon->GetMyID();
    for (const auto &player : _players)
        if (player.IsHost())
            candidate.hostPlayerId = player.ID;
    if (!candidate.Valid())
        return false; // Expected offline getters do not erase a retained tuple.
    const bool contradicts = (_myID && _myID != candidate.ownPlayerId) ||
        (_sesID && _sesID != candidate.sessionId) ||
        (_sessionIdentity.Valid() &&
         (_sessionIdentity.sessionId != candidate.sessionId ||
          _sessionIdentity.ownPlayerId != candidate.ownPlayerId ||
          _sessionIdentity.hostPlayerId != candidate.hostPlayerId));
    if (contradicts || (connType == 1 && candidate.ownPlayerId != candidate.hostPlayerId))
    {
        RejectSessionIdentity();
        return false;
    }
    if (!WindpRoster::ByID(_players, candidate.ownPlayerId))
        return false;
    _sessionIdentity = candidate;
    _myID = candidate.ownPlayerId;
    _sesID = candidate.sessionId;
    return true;
}

void NC_STACK_windp::ReloadSessionsList()
{
    if (connType > 0 && (zcli || zenet) && SDL_GetTicks() > _nextSessionsGet)
    {
        _nextSessionsGet = SDL_GetTicks() + CONF_SESS_GET_TIME;
        if (zenet) zenet->GetSessions(_rawSessions); else zcli->GetSessions(_rawSessions);
    }
}


uint64_t NC_STACK_windp::GetUserID(const std::string &name)
{
    for(const ZNDNet::UserInfo& inf : _rawUsers)
        if (name.compare(inf.name) == 0)
            return inf.ID;
    return 0;
}

std::string NC_STACK_windp::GetUserName(uint64_t id)
{
    for(const ZNDNet::UserInfo& inf : _rawUsers)
        if (inf.ID == id)
            return inf.name;
    return "";
}

















void UserData::yw_FractionInit()
{
    const TMapRegionInfo &lvl = p_YW->_globalMapRegions.MapRegions[ netLevelID ];

    FreeFraction = lvl.FractionsBits & World::NetFactions::Mask;
    const uint32_t frkt = World::NetFactions::FirstAvailable(FreeFraction);

    SelectedFraction = frkt;
    FreeFraction &= ~frkt;
}

void UserData::AfterMapChoose()
{
    if ( netSel < 0 || netLaunchFrozen || p_YW->_userRobo )
        return;

    switch ( p_YW->_netDriver->GetMode() )
    {
        case 1:
        {
            // Host() starts a fresh transport session, including the existing
            // in-lobby choose-map route. Preserve the selected map, but never
            // carry participant authority/readiness across that boundary.
            netSession = {};
            lobbyPlayers.fill(TNetPlayerLobbyData{});
            netPlayers.fill(TNetPlayerData{});
            netLaunchPlayers.fill(TNetPlayerLobbyData{});
            netSessionEnded = false;
            noSent = false;
            netPlayerOwner = 0;
            std::string bff = fmt::sprintf("%d%s%s%s%s", netLevelID, "|", p_YW->_netDriver->GetMyName(), "|", p_YW->_buildDate);
            if (!p_YW->_netDriver->Host(bff, World::CVMaxNetPlayers) || !InitializeNetIdentity())
                return;

            p_YW->_isNetGame = true;
            netSel = -1;
            netSelMode = NETSCREEN_INSESSION;
            netName = "";
            netNameCurPos = 0;
            network_listvw.firstShownEntries = 0;
            network_listvw.selectedEntry = 0;

            lobbyPlayers[ 0 ].Name = p_YW->_netDriver->GetMyName();

            yw_FractionInit();

            rdyStart = true;
            lobbyPlayers[0].Ready = true;
        }
        break;

        case 2:

            break;

        default:
            break;
    }
    return;

    if ( p_YW->_netDriver->GetSessionStatus() )
    {
        TMapRegionInfo &lvl = p_YW->_globalMapRegions.MapRegions[ netLevelID ];

        netSelMode = NETSCREEN_INSESSION;
        netName = "";
        netNameCurPos = 0;
        network_listvw.firstShownEntries = 0;
        network_listvw.selectedEntry = 0;
        netSel = -1;

        FreeFraction = lvl.FractionsBits & World::NetFactions::Mask;
        SelectedFraction = 0;
        for (auto &player : lobbyPlayers) player.NetFraction = 0;
        for (const TDPPlayerData &player : p_YW->_netDriver->GetPlayersData())
        {
            const uint32_t bit = World::NetFactions::FirstAvailable(FreeFraction);
            if (TNetPlayerLobbyData *row = World::NetLobby::RowAt(lobbyPlayers, player.Index))
            {
                FreeFraction &= ~bit;
                row->NetFraction = bit;
                if (player.IsItMe()) SelectedFraction = bit;
            }
        }

        for(TNetPlayerLobbyData &pl : lobbyPlayers)
            pl.DataChecksum = 0;

        uamessage_setLevel msg_slvl;
        NetMsgClear(msg_slvl);
        msg_slvl.lvlID = netLevelID;
        msg_slvl.msgID = UAMSG_SETLEVEL;
        msg_slvl.owner = 0;

        p_YW->NetBroadcastMessage(&msg_slvl, sizeof(msg_slvl), true);

        p_YW->SendCRC(netLevelID);

        p_YW->_netDriver->FlushBroadcastBuffer();

        std::string bff = fmt::sprintf("%d%s%s%s%s", netLevelID, "|", netPlayerName, "|", p_YW->_buildDate);

        p_YW->_netDriver->SetSessionName(bff.c_str());
    }
    else if ( remoteMode )
    {
        TMapRegionInfo &lvl = p_YW->_globalMapRegions.MapRegions[ netLevelID ];

        netSelMode = NETSCREEN_INSESSION;
        netName = "";
        netNameCurPos = 0;
        network_listvw.firstShownEntries = 0;
        network_listvw.selectedEntry = 0;
        netSel = -1;

        FreeFraction = lvl.FractionsBits & World::NetFactions::Mask;
        SelectedFraction = 0;
        for (auto &player : lobbyPlayers) player.NetFraction = 0;
        for (const TDPPlayerData &player : p_YW->_netDriver->GetPlayersData())
        {
            const uint32_t bit = World::NetFactions::FirstAvailable(FreeFraction);
            if (TNetPlayerLobbyData *row = World::NetLobby::RowAt(lobbyPlayers, player.Index))
            {
                FreeFraction &= ~bit;
                row->NetFraction = bit;
                if (player.IsItMe()) SelectedFraction = bit;
            }
        }

        uamessage_lobbyInit limsg;
        NetMsgClear(limsg);
        limsg.lvlID = netLevelID;
        limsg.msgID = UAMSG_LOBBYINIT;
        limsg.owner = 0;

        p_YW->NetBroadcastMessage(&limsg, sizeof(limsg), true);

        p_YW->_netDriver->FlushBroadcastBuffer();
    }
    else
    {
        if (isHost)
        {
            std::string bff = fmt::sprintf("%d%s%s%s%s", netLevelID, "|", netPlayerName, "|", p_YW->_buildDate);

            if (p_YW->_netDriver->GetProvType() == 4) //MODEM!!!!
                GFX::Engine.windd_func320(nullptr);

            windp_openSessionMsg os;
            os.name = bff.c_str();
            os.maxplayers = World::CVMaxNetPlayers;

            size_t res = p_YW->_netDriver->CreateSession(&os);

            if (p_YW->_netDriver->GetProvType() == 4) //MODEM!!!!
                GFX::Engine.windd_func321(nullptr);

            if ( !res )
                return;
        }

        windp_createPlayerMsg cp;
        cp.name = netPlayerName.c_str();
        cp.flags = 1;

        if (p_YW->_netDriver->CreatePlayer(&cp))
        {
            p_YW->_isNetGame = true;
            netSel = -1;
            netSelMode = NETSCREEN_INSESSION;
            netName = "";
            netNameCurPos = 0;
            network_listvw.firstShownEntries = 0;
            network_listvw.selectedEntry = 0;

            TNetPlayerLobbyData *row = World::NetLobby::RowAt(lobbyPlayers, p_YW->_netDriver->GetMyIndex());

            if ( row )
                row->Name = cp.name;

            yw_FractionInit();

            rdyStart = true;
            if ( row )
                row->Ready = true;
        }
    }
}

void UserData::yw_NetOKProvider()
{
    if ( netSel == 0 || netSel == 1 )
    {
        GFX::Engine.windd_func320(nullptr);

        if ( p_YW->_netDriver->SetMode(netSel == 0) )
        {
            netSelMode = NETSCREEN_ENTER_NAME;
            netSel = -1;
            network_listvw.firstShownEntries = 0;
            network_listvw.selectedEntry = 0;

            p_YW->GuiWinClose( &network_listvw );

            netName = netPlayerName;

            netNameCurPos = netName.size();
        }

        GFX::Engine.windd_func321(nullptr);

        update_time_norm = 200;
        flush_time_norm = 200;
    }
}

void UserData::yw_JoinNetGame()
{
    if ( p_YW->_netDriver->GetProvType() != 4 || modemAskSession )
    {
        windp_getNameMsg gName;
        gName.id = netSel;

        if ( p_YW->_netDriver->GetSessionName(&gName) )
        {
            if ( p_YW->_netDriver->GetProvType() == 4 )
                GFX::Engine.windd_func320(nullptr);

            if ( p_YW->_netDriver->JoinSession(gName.name) )
            {
                if (!InitializeNetIdentity())
                    return;
                if ( p_YW->_netDriver->GetProvType() == 4 )
                    GFX::Engine.windd_func321(nullptr);

                netName = "";
                netNameCurPos = 0;
                netLevelID = 0;

                std::string buf = gName.name;

                for (size_t i = 0; i < buf.length(); i++)
                {
                    if (buf[i] == '"')
                    {
                        buf.resize(i);

                        netLevelID = atoi(buf.c_str());
                        break;
                    }
                }
                network_listvw.firstShownEntries = 0;
                netLevelName = p_YW->GetLevelName(netLevelID);

                for(TDPPlayerData &p : p_YW->_netDriver->GetPlayersData())
                {
                    TNetPlayerLobbyData *row = World::NetLobby::RowAt(lobbyPlayers, p.Index);
                    if ( !row )
                        continue;

                    row->Name = p.name;

                    if (p.IsItMe())
                    {
                        row->Name += "X";
                    }
                }

                windp_createPlayerMsg crPlayerMsg;
                crPlayerMsg.name = netPlayerName.c_str();
                crPlayerMsg.flags = 1;

                if ( p_YW->_netDriver->CreatePlayer(&crPlayerMsg) )
                {
                    p_YW->_isNetGame = true;
                    netSel = -1;
                    netSelMode = NETSCREEN_INSESSION;
                    netName = "";
                    netNameCurPos = 0;
                    network_listvw.firstShownEntries = 0;
                    network_listvw.selectedEntry = 0;
                    isHost = false;

                    yw_FractionInit();
                }

                rdyStart = false;
                if ( TNetPlayerLobbyData *row = World::NetLobby::RowAt(lobbyPlayers, p_YW->_netDriver->GetMyIndex()) )
                    row->Ready = false;

                p_YW->SendCRC(netLevelID);
            }
            else
            {
                if ( p_YW->_netDriver->GetProvType() == 4 )
                    GFX::Engine.windd_func320(nullptr);


                fmt::printf("%s: %s\n", Locale::Text::Advanced(Locale::ADV_ERRHEAD), Locale::Text::Advanced(Locale::ADV_ERRNOSESS));

                if ( p_YW->_netDriver->GetProvType() == 4 )
                {
                    netSel = -1;
                    ExitFromNetworkToMain();
                }
                else
                {
                    p_YW->_netDriver->EnumSessions(nullptr);
                }
            }
        }
    }
    else
    {
        if ( p_YW->_netDriver->GetProvType() == 4 )
            GFX::Engine.windd_func320(nullptr);

        if ( p_YW->_netDriver->EnumSessions(nullptr) )
        {
            modemAskSession = true;
        }

        if ( p_YW->_netDriver->GetProvType() == 4 )
            GFX::Engine.windd_func321(nullptr);
    }
}

void UserData::JoinLobbyLessGame()
{
    if (p_YW->_netDriver->IsConnected())
    {
        uint32_t waitForSession = SDL_GetTicks() + 1000;
        while (SDL_GetTicks() < waitForSession && !p_YW->_netDriver->IsJoined())
            SDL_Delay(10);

        if (p_YW->_netDriver->IsJoined())
        {
            if (!InitializeNetIdentity())
                return;
            netName = "";
            netNameCurPos = 0;
            netLevelID = 0;

            std::string buf = p_YW->_netDriver->GetCurrentSessionName();
            printf("%s\n", buf.c_str());

            for (size_t i = 0; i < buf.length(); i++)
            {
                if (buf[i] == '|')
                {
                    buf.resize(i);

                    netLevelID = std::stoi(buf);
                    break;
                }
            }
            network_listvw.firstShownEntries = 0;
            netLevelName = p_YW->GetLevelName(netLevelID);

            for(TDPPlayerData &p : p_YW->_netDriver->GetPlayersData())
            {
                TNetPlayerLobbyData *row = World::NetLobby::RowAt(lobbyPlayers, p.Index);
                if ( !row )
                    continue;

                row->Name = p.name;

                if (p.IsItMe())
                {
                    row->Name += "X";
                }
            }

            p_YW->_isNetGame = true;
            netSel = -1;
            netSelMode = NETSCREEN_INSESSION;
            netName = "";
            netNameCurPos = 0;
            network_listvw.firstShownEntries = 0;
            network_listvw.selectedEntry = 0;
            p_YW->GuiWinOpen( &network_listvw );
            isHost = false;

            yw_FractionInit();

            // Announce this client's data checksum, as the session join below
            // (no longer reached) did: without it the host never compares the
            // client's scripts and map with its own (yw_CheckCRCs).
            p_YW->SendCRC(netLevelID);
        }
    }
    return;

    if ( p_YW->_netDriver->GetProvType() != 4 || modemAskSession )
    {
        windp_getNameMsg gName;
        gName.id = netSel;

        if ( p_YW->_netDriver->GetSessionName(&gName) )
        {
            if ( p_YW->_netDriver->GetProvType() == 4 )
                GFX::Engine.windd_func320(nullptr);

            if ( p_YW->_netDriver->JoinSession(gName.name) )
            {
                if ( p_YW->_netDriver->GetProvType() == 4 )
                    GFX::Engine.windd_func321(nullptr);

                netName = "";
                netNameCurPos = 0;
                netLevelID = 0;

                std::string buf = gName.name;

                for (size_t i = 0; i < buf.length(); i++)
                {
                    if (buf[i] == '"')
                    {
                        buf.resize(i);

                        netLevelID = atoi(buf.c_str());
                        break;
                    }
                }
                network_listvw.firstShownEntries = 0;
                netLevelName = p_YW->GetLevelName(netLevelID);

                for(TDPPlayerData &p : p_YW->_netDriver->GetPlayersData())
                {
                    TNetPlayerLobbyData *row = World::NetLobby::RowAt(lobbyPlayers, p.Index);
                    if ( !row )
                        continue;

                    row->Name = p.name;

                    if (p.IsItMe())
                    {
                        row->Name += "X";
                    }
                }

                windp_createPlayerMsg crPlayerMsg;
                crPlayerMsg.name = netPlayerName.c_str();
                crPlayerMsg.flags = 1;

                if ( p_YW->_netDriver->CreatePlayer(&crPlayerMsg) )
                {
                    p_YW->_isNetGame = true;
                    netSel = -1;
                    netSelMode = NETSCREEN_INSESSION;
                    netName = "";
                    netNameCurPos = 0;
                    network_listvw.firstShownEntries = 0;
                    network_listvw.selectedEntry = 0;
                    isHost = false;

                    yw_FractionInit();
                }

                rdyStart = false;
                if ( TNetPlayerLobbyData *row = World::NetLobby::RowAt(lobbyPlayers, p_YW->_netDriver->GetMyIndex()) )
                    row->Ready = false;

                p_YW->SendCRC(netLevelID);
            }
            else
            {
                if ( p_YW->_netDriver->GetProvType() == 4 )
                    GFX::Engine.windd_func320(nullptr);


                fmt::printf("%s: %s\n", Locale::Text::Advanced(Locale::ADV_ERRHEAD), Locale::Text::Advanced(Locale::ADV_ERRNOSESS));

                if ( p_YW->_netDriver->GetProvType() == 4 )
                {
                    netSel = -1;
                    ExitFromNetworkToMain();
                }
                else
                {
                    p_YW->_netDriver->EnumSessions(nullptr);
                }
            }
        }
    }
    else
    {
        if ( p_YW->_netDriver->GetProvType() == 4 )
            GFX::Engine.windd_func320(nullptr);

        if ( p_YW->_netDriver->EnumSessions(nullptr) )
        {
            modemAskSession = true;
        }

        if ( p_YW->_netDriver->GetProvType() == 4 )
            GFX::Engine.windd_func321(nullptr);
    }
}

int yw_DestroyPlayer(NC_STACK_ypaworld *yw, uint64_t playerId)
{
    UserData *usr = yw->_GameShell;
    const int32_t slot = World::NetLobby::FindPlayerSlot(usr->lobbyPlayers,
        usr->netSession.SessionId, playerId);
    if (!usr->netSession.Valid() || slot < 0)
        return 0;
    // Only the retained session host can ask the transport to revoke a peer.
    // Local row cleanup is distinct from observing the terminal transport event.
    if (usr->netSession.LocalPlayerId == usr->netSession.HostPlayerId &&
        playerId != usr->netSession.LocalPlayerId)
        yw->_netDriver->KickPlayer(playerId);
    World::NetLobby::ClearSlot(usr->lobbyPlayers, slot);
    return 1;
}

void UserData::yw_netcleanup()
{
    // Give the queued quit announcement a bounded additional drain opportunity
    // before closing. Shutdown can discard pending data; the preceding flush
    // and following cleanup retain their existing synchronous behavior.
    p_YW->_netDriver->DrainSends(NC_STACK_windp::QuitDrainMs);

    if( netSelMode ==NETSCREEN_INSESSION || p_YW->_isNetGameStarted )
        yw_DestroyPlayer( p_YW, netSession.LocalPlayerId );

    if( ( netSelMode == NETSCREEN_INSESSION && isHost) || p_YW->_isNetGameStarted )
        p_YW->_netDriver->CloseSession(nullptr);

    p_YW->_netDriver->Reset(nullptr);

    netSelMode = NETSCREEN_MODE_SELECT;

    p_YW->_isNetGame = false;

    network_listvw.firstShownEntries = 0;
    netSel = -1;
    nInputMode = 0;
    isHost = false;
    blocked = false;
    netPlayerOwner = 0;
    netLevelID = 0;
    modemAskSession = false;
    sentAQ = false;

    for (TNetPlayerLobbyData &pl : lobbyPlayers)
        pl = UserData::TNetPlayerLobbyData();

    for (TNetPlayerData &pl : netPlayers)
        pl = TNetPlayerData{};
    netSession = {};
    netLaunchPlayers.fill(TNetPlayerLobbyData{});
    netLaunchFrozen = false;
    netSessionEnded = false;

    rdyStart = false;

    p_YW->_isNetGameStarted = false;

    msgBuffers.clear();
    lastSender.clear();
    disconnected = false;
    problemTimer = 0;
    netProblem = 0;
    netProblemCountDown = 0;
    takTime = 0;
    noSent = false;
    FreeFraction = (World::OWNER_GHOR_BIT | World::OWNER_MYKO_BIT | World::OWNER_TAER_BIT);
    SelectedFraction = World::OWNER_RESIST_BIT;
    remoteMode = false;

    log_netlog("netcleanup:      ende\n");
}

void UserData::ExitFromNetworkToMain()
{
    EnvMode = ENVMODE_TITLE;

    network_button->HideScreen();

    titel_button->ShowScreen();

    p_YW->GuiWinClose(&network_listvw);

    NC_STACK_button::button_66arg arg66;
    arg66.butID = UIWidgets::MAIN_MENU_WIDGET_IDS::BTN_MULTIPLAYER;
    arg66.field_4 = 2;

    sub_bar_button->SetState(&arg66);

    arg66.butID = 1219;
    arg66.field_4 = 2;
    sub_bar_button->SetState(&arg66);

    yw_netcleanup();
}

bool yw_initNetLogFile()
{
    dprintf("MAKE ME %s (multiplayer)\n", "yw_initNetLogFile");
    return true;
}

void UserData::yw_NetPrintStartInfo()
{
    if ( yw_initNetLogFile() )
    {
        log_netlog("-------------- Start YPA network session ------------------\n\n");

        size_t pT = p_YW->_netDriver->GetProvType();

        if ( pT == 1 )
            log_netlog("Provider: Hosting\n");
        else if ( pT == 2 )
            log_netlog("Provider: Connect\n");
        else
            log_netlog("!!! Unknown provider\n");

        log_netlog("for this povider I send a dplay-msg after %d ms\n", flush_time_norm);
        log_netlog("Following players startet with the game:\n");

        for (const TDPPlayerData &player : p_YW->_netDriver->GetPlayersData())
        {
            int id;
            if ( player.IsItMe() )
            {
                id = SelectedFraction;
            }
            else if ( const TNetPlayerLobbyData *row = World::NetLobby::RowAt(lobbyPlayers, player.Index) )
            {
                id = row->NetFraction;
            }
            else
            {
                id = 0;
            }

            const std::string &name = player.name;

            switch ( id )
            {
            case World::OWNER_RESIST_BIT:
                log_netlog("    %s and plays Resistance\n", name.c_str());
                break;

            case World::OWNER_MYKO_BIT:
                log_netlog("    %s and plays Mykonier\n", name.c_str());
                break;

            case World::OWNER_TAER_BIT:
                log_netlog("    %s and plays Taerkasten\n", name.c_str());
                break;

            case World::OWNER_GHOR_BIT:
                log_netlog("    %s and plays Ghorkov\n", name.c_str());
                break;

            case World::OWNER_BLACK_BIT:
                log_netlog("    %s and plays Black Sect\n", name.c_str());
                break;
            case World::OWNER_SULG_BIT:
                log_netlog("    %s and plays Sulgogars\n", name.c_str());
                break;
            default:
                log_netlog("    %s and plays an unknown race\n", name.c_str());
                break;
            }
        }

        if ( isHost )
            log_netlog("\nThe local player is %s and is HOST\n", netPlayerName.c_str());
        else
            log_netlog("\nThe local player is %s and is CLIENT\n", netPlayerName.c_str());

        std::string tmp = p_YW->GetLevelName(netLevelID);
        log_netlog("They play level %d, this is %s\n", netLevelID, tmp.c_str());
        log_netlog("the session started at timeindex %d\n", p_YW->_timeStamp / 1000);
        log_netlog("\n\n--------------------------------------------------------\n");
    }
    else
    {
        ypa_log_out("Unable to open Network log script\n");
    }
}


void UserData::ypaworld_func151__sub7()
{
    log_netlog("---------------- YPA Network Statistics -------------------\n\n");
    log_netlog("Sending:\n");
    log_netlog("receiving:\n");
    log_netlog("statistical sr-thread info\n");
    log_netlog("\nthe session ended at timeindex %d\n", p_YW->_timeStamp / 1000);
    log_netlog("-----------------------------------------------------------\n");

    dprintf("MAKE ME %s (multiplayer)\n", "ypaworld_func151__sub7");
}




//////////////multiplayer wnd///////////
netgamelst netgame_wnd;

void sb_0x451034__sub6(NC_STACK_ypaworld *yw)
{
    netgame_wnd.flags = 0x138;
    netgame_wnd.x = 0;
    netgame_wnd.y = 2 * yw->_screenSize.y / 3;
    netgame_wnd.w = yw->_screenSize.x - 1;
    netgame_wnd.h = 2 * yw->_fontH + 2;

    netgame_wnd.buttons.resize(2);
    netgame_wnd.buttons[0] = ButtonBox(yw->_guiTiles[0]->map[97].w, 0,
                                       yw->_guiTiles[0]->map[97].w, yw->_fontH);
    netgame_wnd.buttons[1] = ButtonBox(0, 0,
                                       yw->_guiTiles[0]->map[97].w, yw->_fontH);

    netgame_wnd.field_1CC = yw->_guiTiles[0]->map[97].w;
    netgame_wnd.field_1D0 = 2 * yw->_fontH;
    netgame_wnd.field_1D4 = yw->_screenSize.x;
    netgame_wnd.field_1D8 = netgame_wnd.field_1D0;
    netgame_wnd.field_1DC[0] = 0;
    netgame_wnd.field_21C = 0;

    netgame_wnd.cmdCommands.reserve(1024);

}

//netgui update
void ypaworld_func64__sub7__sub5(NC_STACK_ypaworld *yw, TInputState *inpt)
{
    dprintf("MAKE ME %s (multiplayer)\n", "ypaworld_func64__sub7__sub5");
}
