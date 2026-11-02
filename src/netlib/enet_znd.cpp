#include "enet_znd.h"
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
// Roster control payload (host -> clients, reliable channel 0):
// 'U' 'A' 'R' sesNameLen sesName countU16 { idU64 nameLen name }*
bool IsRoster(const std::vector<std::uint8_t> &data)
{
    return data.size() >= 3 && data[0] == 'U' && data[1] == 'A' && data[2] == 'R';
}

} // namespace

ZNDEnet::ZNDEnet(const std::string &servstring)
    : ZNDNet(servstring), transport_(servstring)
{
    usersMutex_ = SDL_CreateMutex();
}

ZNDEnet::~ZNDEnet()
{
    Stop();
    if (usersMutex_)
        SDL_DestroyMutex(usersMutex_);
}

void ZNDEnet::LockUsers() const { SDL_LockMutex(usersMutex_); }
void ZNDEnet::UnlockUsers() const { SDL_UnlockMutex(usersMutex_); }

int ZNDEnet::PumpMain(void *data)
{
    ((ZNDEnet *)data)->Pump();
    return 0;
}

void ZNDEnet::Start(const std::string &name, std::uint16_t port,
                    const std::string &sname, std::uint32_t max_players)
{
    Stop();
    if (!transport_.StartHost(name, port, max_players ? max_players : 6))
        return;
    hosting_ = true;
    myName_ = name;
    sessionName_ = sname;
    maxPlayers_ = max_players ? max_players : 6;
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
    if (!transport_.Connect(name, ip, port))
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
    transport_.Stop();
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
        transport_.SendGoodbye();
}

void ZNDEnet::ShowSession(bool) {}

void ZNDEnet::CloseSession(std::uint32_t)
{
    if (hosting_)
    {
        // 'X' close for every client, then tear the host down.
        const std::uint8_t msg[1] = { 'X' };
        transport_.Broadcast(msg, 1, WireChannel::Reliable);
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
        transport_.DisconnectPeer((std::uint32_t)(_ID - 1), false);
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

void ZNDEnet::SendData(std::uint64_t to, void *data, std::uint32_t sz,
                       std::uint8_t flags, std::uint8_t)
{
    const WireChannel channel = (flags & PKT_FLAG_GARANT)
        ? WireChannel::Reliable : WireChannel::Unreliable;
    if (hosting_)
    {
        if (to > 1)
            transport_.Send((std::uint32_t)(to - 1), data, sz, channel);
    }
    else
    {
        transport_.Send(0, data, sz, channel);
    }
}

void ZNDEnet::BroadcastData(void *data, std::uint32_t sz,
                            std::uint8_t flags, std::uint8_t)
{
    const WireChannel channel = (flags & PKT_FLAG_GARANT)
        ? WireChannel::Reliable : WireChannel::Unreliable;
    transport_.Broadcast(data, sz, channel);
}

std::uint64_t ZNDEnet::GetMyID()
{
    if (hosting_)
        return 1;
    const std::uint32_t id = transport_.MyId();
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

void ZNDEnet::SendRoster()
{
    LockUsers();
    std::vector<std::uint8_t> msg;
    msg.push_back('U'); msg.push_back('A'); msg.push_back('R');
    msg.push_back((std::uint8_t)sessionName_.size());
    msg.insert(msg.end(), sessionName_.begin(), sessionName_.end());
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
    std::size_t pos = 3;
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

    std::vector<std::pair<std::uint64_t, std::string>> added, left;
    LockUsers();
    for (const auto &kv : next)
        if (kv.first != GetMyID() && users_.find(kv.first) == users_.end())
            added.push_back(kv);
    for (const auto &kv : users_)
        if (kv.first != GetMyID() && next.find(kv.first) == next.end())
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

void ZNDEnet::Pump()
{
    while (!pumpEnd_)
    {
        TransportEvent ev;
        if (!transport_.Poll(ev, 10))
            continue;

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
                Events_Push(new EventNameID(EVENT_CONNECTED, 0, myName_, GetMyID()));
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
            else
            {
                transportConnected_ = false;
                Events_Push(new Event(EVENT_DISCONNECT, 0));
            }
            break;

        case TransportEvent::Type::Data:
            if (!hosting_ && ev.channel == 0 && IsRoster(ev.data))
            {
                ApplyRoster(ev.data.data(), ev.data.size());
            }
            else
            {
                const std::uint64_t from = hosting_
                    ? (std::uint64_t)ev.peer + 1 : 1;
                Events_Push(new EventData(EVENT_DATA, 0, from, false, 0,
                                          (std::uint32_t)ev.data.size(),
                                          ev.data.empty() ? nullptr : ev.data.data(),
                                          ev.channel));
            }
            break;
        }
    }
}

} // namespace ZNDNet
