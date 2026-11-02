#include "enet_znd.h"
#include "errcode.h"
#include <cstdio>
#include <cstring>

namespace ZNDNet
{
namespace {

void PutU16(std::vector<std::uint8_t> &buf, std::uint16_t v)
{
    buf.push_back((std::uint8_t)(v & 0xFF));
    buf.push_back((std::uint8_t)((v >> 8) & 0xFF));
}
void PutU64(std::vector<std::uint8_t> &buf, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        buf.push_back((std::uint8_t)((v >> (8 * i)) & 0xFF));
}
std::uint16_t GetU16(const std::uint8_t *p)
{
    return (std::uint16_t)(p[0] | ((std::uint16_t)p[1] << 8));
}
std::uint64_t GetU64(const std::uint8_t *p)
{
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= (std::uint64_t)p[i] << (8 * i);
    return v;
}

// Frame types on the ENet wire (see enet_znd.h).
const std::uint8_t FrameDirect = 'D';
const std::uint8_t FrameBroadcast = 'B';
const std::uint8_t FrameRoster = 'U';
const std::size_t DataHeader = 1 + 8;

std::uint8_t ConnErrFor(std::uint8_t refusal)
{
    switch (refusal)
    {
    case EnetTransport::RefuseName: return ERR_CONN_NAME;
    case EnetTransport::RefuseFull: return ERR_CONN_FULL;
    case EnetTransport::RefuseService:
    case EnetTransport::RefuseVersion: return ERR_CONN_VERSION;
    default: return ERR_CONN_TIMEOUT;
    }
}

} // namespace

ZNDEnet::ZNDEnet(const std::string &servstring)
    : ZNDNet(servstring), transport_(servstring)
{
    usersMutex_ = SDL_CreateMutex();
    netMutex_ = SDL_CreateMutex();
}

ZNDEnet::~ZNDEnet()
{
    Stop();
    if (usersMutex_)
        SDL_DestroyMutex(usersMutex_);
    if (netMutex_)
        SDL_DestroyMutex(netMutex_);
}

void ZNDEnet::LockUsers() const { SDL_LockMutex(usersMutex_); }
void ZNDEnet::UnlockUsers() const { SDL_UnlockMutex(usersMutex_); }
void ZNDEnet::LockNet() const { SDL_LockMutex(netMutex_); }
void ZNDEnet::UnlockNet() const { SDL_UnlockMutex(netMutex_); }

int ZNDEnet::PumpMain(void *data)
{
    ((ZNDEnet *)data)->Pump();
    return 0;
}

void ZNDEnet::Start(const std::string &name, std::uint16_t port,
                    const std::string &sname, std::uint32_t max_players)
{
    Stop();
    maxPlayers_ = max_players ? max_players : 6;
    // The host occupies one player slot itself.
    const std::size_t clientSlots = maxPlayers_ > 1 ? maxPlayers_ - 1 : 1;
    LockNet();
    const bool started = transport_.StartHost(name, port, clientSlots);
    UnlockNet();
    if (!started)
        return;
    hosting_ = true;
    sessionOpen_ = true;
    myName_ = name;
    sessionName_ = sname;
    mySesId_ = 1;
    LockUsers();
    users_.clear();
    users_[1] = name;
    UnlockUsers();
    running_ = true;
    pumpEnd_ = false;
    pumpThread_ = SDL_CreateThread(PumpMain, "", this);
}

void ZNDEnet::Start(const std::string &name, const IPaddress &addr)
{
    Stop();
    const std::uint8_t *b = (const std::uint8_t *)&addr.host;
    char ip[24];
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    const std::uint16_t port = (std::uint16_t)(((addr.port & 0xFF) << 8) |
                                               ((addr.port >> 8) & 0xFF));
    LockNet();
    const bool connecting = transport_.Connect(name, ip, port);
    UnlockNet();
    if (!connecting)
        return;
    hosting_ = false;
    myName_ = name;
    mySesId_ = 0;
    joined_ = false;
    transportConnected_ = false;
    LockUsers();
    users_.clear();
    UnlockUsers();
    running_ = true;
    pumpEnd_ = false;
    pumpThread_ = SDL_CreateThread(PumpMain, "", this);
}

void ZNDEnet::Stop()
{
    pumpEnd_ = true;
    if (pumpThread_)
    {
        SDL_WaitThread(pumpThread_, nullptr);
        pumpThread_ = nullptr;
    }
    LockNet();
    transport_.Stop();
    UnlockNet();
    running_ = false;
    transportConnected_ = false;
    joined_ = false;
}

std::uint8_t ZNDEnet::GetStatus()
{
    if (transportConnected_)
        return NetUser::STATUS_CONNECTED;
    if (running_)
        return NetUser::STATUS_CONNECTING;
    return NetUser::STATUS_DISCONNECTED;
}

bool ZNDEnet::GetSessions(SessionInfoVect &dst)
{
    if (!mySesId_)
        return false;
    LockUsers();
    SessionInfo info;
    info.ID = mySesId_;
    info.name = sessionName_;
    info.pass = false;
    info.players = (std::uint32_t)users_.size();
    info.max_players = maxPlayers_;
    UnlockUsers();
    dst.clear();
    dst.push_back(info);
    return true;
}

void ZNDEnet::CreateSession(const std::string &, const std::string &, std::uint32_t)
{
    // Direct transport has a fixed single session; creation is an error,
    // matching the no-lobby ZNDSingle behaviour surfaced to windp.
    Events_Push(new Event(EVENT_SESSION_FAIL, 0));
}

void ZNDEnet::JoinSession(std::uint64_t SID, const std::string &)
{
    if (SID != 1 || !transportConnected_)
        Events_Push(new Event(EVENT_SESSION_FAIL, 0));
    // Already auto-joined: SESSION_JOIN was (or will be) pushed by the pump.
}

void ZNDEnet::SendDisconnect()
{
    if (!hosting_)
    {
        LockNet();
        transport_.SendGoodbye();
        UnlockNet();
    }
}

void ZNDEnet::ShowSession(bool show)
{
    // ZNDSingle parity: a hidden session takes no new players (resumes of
    // dropped players are still honoured by the transport).
    sessionOpen_ = show;
    if (hosting_)
    {
        LockNet();
        transport_.SetAcceptingNew(show);
        UnlockNet();
    }
}

void ZNDEnet::CloseSession(std::uint32_t)
{
    if (hosting_)
    {
        // 'X' close for every client, then tear the host down.
        const std::uint8_t msg[1] = { 'X' };
        LockNet();
        transport_.Broadcast(msg, 1, WireChannel::Reliable);
        UnlockNet();
        Stop();
        LockUsers();
        users_.clear();
        users_[1] = myName_;
        UnlockUsers();
        mySesId_ = 0;
    }
    else
    {
        SendDisconnect();
    }
}

void ZNDEnet::KickUser(std::uint64_t _ID)
{
    if (hosting_ && _ID > 1)
    {
        LockNet();
        transport_.DisconnectPeer((std::uint32_t)(_ID - 1), false);
        UnlockNet();
    }
}

bool ZNDEnet::GetUsers(UserInfoVect &dst)
{
    LockUsers();
    dst.clear();
    for (const auto &kv : users_)
    {
        UserInfo info;
        info.ID = kv.first;
        info.name = kv.second;
        info.lead = kv.first == 1;
        dst.push_back(info);
    }
    UnlockUsers();
    return true;
}

bool ZNDEnet::GetUser(UserInfo &dst, const char *name)
{
    if (!name)
        return false;
    LockUsers();
    for (const auto &kv : users_)
    {
        if (kv.second == name)
        {
            dst.ID = kv.first;
            dst.name = kv.second;
            dst.lead = kv.first == 1;
            UnlockUsers();
            return true;
        }
    }
    UnlockUsers();
    return false;
}

bool ZNDEnet::GetUser(UserInfo &dst, std::uint64_t _ID)
{
    LockUsers();
    const auto it = users_.find(_ID);
    if (it == users_.end())
    {
        UnlockUsers();
        return false;
    }
    dst.ID = it->first;
    dst.name = it->second;
    dst.lead = it->first == 1;
    UnlockUsers();
    return true;
}

// Caller holds the net lock. peer is the transport peer (0 = host on a client).
bool ZNDEnet::SendFrame(std::uint32_t peer, std::uint8_t type, std::uint64_t address,
                        const void *data, std::uint32_t sz, WireChannel channel)
{
    std::vector<std::uint8_t> frame;
    frame.reserve(DataHeader + sz);
    frame.push_back(type);
    PutU64(frame, address);
    if (sz && data)
        frame.insert(frame.end(), (const std::uint8_t *)data, (const std::uint8_t *)data + sz);
    return transport_.Send(peer, frame.data(), frame.size(), channel);
}

void ZNDEnet::SendData(std::uint64_t to, void *data, std::uint32_t sz,
                       std::uint8_t flags, std::uint8_t)
{
    const WireChannel channel = (flags & PKT_FLAG_GARANT)
        ? WireChannel::Reliable : WireChannel::Unreliable;
    LockNet();
    if (hosting_)
    {
        if (to > 1)
            SendFrame((std::uint32_t)(to - 1), FrameDirect, 1, data, sz, channel);
    }
    else if (to)
    {
        // Addressed to the host or relayed by it to another client.
        SendFrame(0, FrameDirect, to, data, sz, channel);
    }
    UnlockNet();
}

void ZNDEnet::BroadcastData(void *data, std::uint32_t sz,
                            std::uint8_t flags, std::uint8_t)
{
    const WireChannel channel = (flags & PKT_FLAG_GARANT)
        ? WireChannel::Reliable : WireChannel::Unreliable;
    LockNet();
    if (hosting_)
    {
        std::vector<std::uint8_t> frame;
        frame.reserve(DataHeader + sz);
        frame.push_back(FrameBroadcast);
        PutU64(frame, 1);
        if (sz && data)
            frame.insert(frame.end(), (const std::uint8_t *)data, (const std::uint8_t *)data + sz);
        transport_.Broadcast(frame.data(), frame.size(), channel);
    }
    else
    {
        SendFrame(0, FrameBroadcast, 0, data, sz, channel); // host relays to everyone else
    }
    UnlockNet();
}

std::uint64_t ZNDEnet::GetMyID()
{
    if (hosting_)
        return 1;
    LockNet();
    const std::uint32_t id = transport_.MyId();
    UnlockNet();
    return id ? (std::uint64_t)id + 1 : 0;
}

std::string ZNDEnet::GetMyName()
{
    return myName_;
}

std::uint64_t ZNDEnet::GetMySesID()
{
    return mySesId_;
}

// Pump thread, net lock held.
void ZNDEnet::SendRoster()
{
    std::vector<std::uint8_t> msg;
    msg.push_back(FrameRoster);
    msg.push_back((std::uint8_t)sessionName_.size());
    msg.insert(msg.end(), sessionName_.begin(), sessionName_.end());
    LockUsers();
    PutU16(msg, (std::uint16_t)users_.size());
    for (const auto &kv : users_)
    {
        PutU64(msg, kv.first);
        msg.push_back((std::uint8_t)kv.second.size());
        msg.insert(msg.end(), kv.second.begin(), kv.second.end());
    }
    UnlockUsers();
    transport_.Broadcast(msg.data(), msg.size(), WireChannel::Reliable);
}

void ZNDEnet::ApplyRoster(const std::uint8_t *data, std::size_t len)
{
    std::size_t pos = 1;
    if (pos >= len)
        return;
    const std::size_t sesLen = data[pos++];
    if (pos + sesLen + 2 > len)
        return;
    const std::string sesName((const char *)data + pos, sesLen);
    pos += sesLen;
    const std::uint16_t count = GetU16(data + pos);
    pos += 2;
    std::map<std::uint64_t, std::string> next;
    for (std::uint16_t i = 0; i < count; ++i)
    {
        if (pos + 8 + 1 > len)
            return;
        const std::uint64_t id = GetU64(data + pos);
        pos += 8;
        const std::size_t nameLen = data[pos++];
        if (pos + nameLen > len)
            return;
        next[id] = std::string((const char *)data + pos, nameLen);
        pos += nameLen;
    }

    const std::uint64_t me = (std::uint64_t)transport_.MyId() + 1; // net lock held
    std::vector<std::pair<std::uint64_t, std::string>> added, left;
    LockUsers();
    for (const auto &kv : next)
        if (kv.first != me && users_.find(kv.first) == users_.end())
            added.push_back(kv);
    for (const auto &kv : users_)
        if (kv.first != me && next.find(kv.first) == next.end())
            left.push_back(kv);
    users_ = next;
    sessionName_ = sesName;
    UnlockUsers();

    for (const auto &kv : added)
        Events_Push(new EventNameID(EVENT_USER_ADD, 0, kv.second, kv.first));
    for (const auto &kv : left)
        Events_Push(new EventNameID(EVENT_USER_LEAVE, 0, kv.second, kv.first));
    if (!joined_)
    {
        joined_ = true;
        mySesId_ = 1;
        Events_Push(new EventNameID(EVENT_SESSION_JOIN, 0, sesName, 1));
    }
}

// Pump thread, net lock held. A client frame: deliver to the host's game
// and/or relay to the addressed client(s), keeping the original sender.
void ZNDEnet::HostDeliver(std::uint64_t from, const std::uint8_t *data, std::size_t len,
                          std::uint8_t channelId)
{
    if (len < DataHeader || (data[0] != FrameDirect && data[0] != FrameBroadcast))
        return;
    const std::uint64_t to = GetU64(data + 1);
    const std::uint8_t *payload = data + DataHeader;
    const std::uint32_t size = (std::uint32_t)(len - DataHeader);
    const WireChannel channel = channelId == 0 ? WireChannel::Reliable : WireChannel::Unreliable;
    const std::uint32_t fromPeer = (std::uint32_t)(from - 1);

    if (data[0] == FrameBroadcast)
    {
        Events_Push(new EventData(EVENT_DATA, 0, from, true, mySesId_, size,
                                  size ? (std::uint8_t *)payload : nullptr, channelId));
        std::vector<std::uint8_t> frame(data, data + len);
        for (int i = 0; i < 8; ++i)
            frame[1 + i] = (std::uint8_t)((from >> (8 * i)) & 0xFF);
        transport_.BroadcastExcept(fromPeer, frame.data(), frame.size(), channel);
    }
    else if (to == 1)
    {
        Events_Push(new EventData(EVENT_DATA, 0, from, false, 1, size,
                                  size ? (std::uint8_t *)payload : nullptr, channelId));
    }
    else if (to > 1 && to != from)
    {
        LockUsers();
        const bool known = users_.find(to) != users_.end();
        UnlockUsers();
        if (known)
            SendFrame((std::uint32_t)(to - 1), FrameDirect, from, payload, size, channel);
    }
}

void ZNDEnet::Pump()
{
    while (!pumpEnd_)
    {
        TransportEvent ev;
        LockNet();
        const bool got = transport_.Poll(ev, 0);
        if (!got)
        {
            UnlockNet();
            SDL_Delay(2);
            continue;
        }

        switch (ev.type)
        {
        case TransportEvent::Type::Connected:
            if (hosting_)
            {
                const std::uint64_t zid = (std::uint64_t)ev.peer + 1;
                LockUsers();
                users_[zid] = ev.name;
                UnlockUsers();
                Events_Push(new EventNameID(EVENT_USER_ADD, 0, ev.name, zid));
                SendRoster();
            }
            else
            {
                transportConnected_ = true;
                Events_Push(new EventNameID(EVENT_CONNECTED, 0, myName_,
                                            (std::uint64_t)transport_.MyId() + 1));
            }
            break;

        case TransportEvent::Type::Disconnected:
            if (hosting_)
            {
                const std::uint64_t zid = (std::uint64_t)ev.peer + 1;
                std::string name;
                LockUsers();
                const auto it = users_.find(zid);
                if (it != users_.end())
                {
                    name = it->second;
                    users_.erase(it);
                }
                UnlockUsers();
                Events_Push(new EventNameID(EVENT_USER_LEAVE, 0, name, zid));
                SendRoster();
            }
            else if (!transportConnected_ && ev.reason)
            {
                // Refused during the handshake: report why (name taken,
                // session full or closed, incompatible build).
                Events_Push(new Event(EVENT_CONNERR, ConnErrFor(ev.reason)));
            }
            else
            {
                transportConnected_ = false;
                Events_Push(new Event(EVENT_DISCONNECT, 0));
            }
            break;

        case TransportEvent::Type::Data:
            if (hosting_)
            {
                HostDeliver((std::uint64_t)ev.peer + 1, ev.data.data(), ev.data.size(),
                            ev.channel);
            }
            else if (!ev.data.empty() && ev.data[0] == FrameRoster && ev.channel == 0)
            {
                ApplyRoster(ev.data.data(), ev.data.size());
            }
            else if (ev.data.size() >= DataHeader &&
                     (ev.data[0] == FrameDirect || ev.data[0] == FrameBroadcast))
            {
                // The address of a host->client frame is the original sender;
                // cast/to mirror ZNDClient (session id for broadcasts).
                const bool cast = ev.data[0] == FrameBroadcast;
                const std::uint64_t from = GetU64(ev.data.data() + 1);
                const std::uint32_t size = (std::uint32_t)(ev.data.size() - DataHeader);
                Events_Push(new EventData(EVENT_DATA, 0, from, cast,
                                          cast ? mySesId_ : (std::uint64_t)transport_.MyId() + 1,
                                          size,
                                          size ? ev.data.data() + DataHeader : nullptr,
                                          ev.channel));
            }
            break;
        }
        UnlockNet();
    }
}

} // namespace ZNDNet
