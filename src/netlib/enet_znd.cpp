#include "enet_znd.h"
#include "errcode.h"
#include <cstdio>
#include <cstring>

namespace ZNDNet
{
namespace
{

void
PutU16(std::vector<std::uint8_t>& buf, std::uint16_t v)
{
    buf.push_back((std::uint8_t)(v & 0xFF));
    buf.push_back((std::uint8_t)((v >> 8) & 0xFF));
}
void
PutU64(std::vector<std::uint8_t>& buf, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        buf.push_back((std::uint8_t)((v >> (8 * i)) & 0xFF));
}
std::uint16_t
GetU16(const std::uint8_t* p)
{
    return (std::uint16_t)(p[0] | ((std::uint16_t)p[1] << 8));
}
std::uint64_t
GetU64(const std::uint8_t* p)
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

std::uint8_t
ConnErrFor(std::uint8_t refusal)
{
    switch (refusal)
    {
        case EnetTransport::RefuseName:
            return ERR_CONN_NAME;
        case EnetTransport::RefuseFull:
            return ERR_CONN_FULL;
        case EnetTransport::RefuseClosed:
            return ERR_CONN_CLOSED;
        case EnetTransport::RefuseResume:
            return ERR_CONN_RESUME;
        case EnetTransport::RefuseRandom:
            return ERR_CONN_RANDOM;
        case EnetTransport::RefuseService:
        case EnetTransport::RefuseVersion:
            return ERR_CONN_VERSION;
        default:
            return ERR_CONN_TIMEOUT;
    }
}

} // namespace

ZNDEnet::ZNDEnet(const std::string& servstring)
  : ZNDNet(servstring)
  , transport_(servstring)
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

void
ZNDEnet::LockUsers() const
{
    SDL_LockMutex(usersMutex_);
}
void
ZNDEnet::UnlockUsers() const
{
    SDL_UnlockMutex(usersMutex_);
}
void
ZNDEnet::LockNet() const
{
    SDL_LockMutex(netMutex_);
}
void
ZNDEnet::UnlockNet() const
{
    SDL_UnlockMutex(netMutex_);
}

int
ZNDEnet::PumpMain(void* data)
{
    ((ZNDEnet*)data)->Pump();
    return 0;
}

void
ZNDEnet::Start(const std::string& name,
               std::uint16_t port,
               const std::string& sname,
               std::uint32_t max_players)
{
    Stop();
    // A fresh host attempt ends the previous presentation identity even when
    // validation or listener creation fails. Ordinary Stop custody is unchanged.
    LockNet();
    hosting_ = false;
    sessionOpen_ = false;
    mySesId_ = 0;
    myName_.clear();
    sessionName_.clear();
    UnlockNet();
    if (!ValidSessionDescriptor(sname))
        return;
    maxPlayers_ = std::max(1u, std::min(6u, max_players ? max_players : 6));
    // The host occupies one player slot itself.
    const std::size_t clientSlots = maxPlayers_ - 1;
    LockNet();
    const bool started = transport_.StartHost(name, port, clientSlots);
    UnlockNet();
    if (!started)
        return;
    hosting_ = true;
    sessionOpen_ = true;
    myName_ = name;
    sessionName_ = sname;
    mySesId_ = transport_.SessionId();
    LockUsers();
    users_.clear();
    users_[1] = name;
    roster_ = RosterState();
    roster_.session = transport_.SessionId();
    roster_.name = sname;
    roster_.rows.push_back({ 1, 0, RosterOnline, name });
    acknowledgments_.clear();
    UnlockUsers();
    running_ = true;
    connectDeadline_ = TransportNow() + 10000;
    retryNext_ = 0;
    pumpEnd_ = false;
    pumpThread_ = SDL_CreateThread(PumpMain, "", this);
}

void
ZNDEnet::Start(const std::string& name, const IPaddress& addr)
{
    Stop();
    const std::uint8_t* b = (const std::uint8_t*)&addr.host;
    char ip[24];
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    const std::uint16_t port =
      (std::uint16_t)(((addr.port & 0xFF) << 8) | ((addr.port >> 8) & 0xFF));
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
    connectDeadline_ = TransportNow() + 10000;
    retryNext_ = 0;
    pumpEnd_ = false;
    pumpThread_ = SDL_CreateThread(PumpMain, "", this);
}

void
ZNDEnet::Stop()
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
    LockUsers();
    users_.clear();
    roster_ = RosterState();
    acknowledgments_.clear();
    UnlockUsers();
    Events_Clear();
}

bool
ZNDEnet::GetResumeToken(World::NetProtocol::ResumeToken& out)
{
    LockNet();
    bool ok = transport_.GetResumeToken(out);
    UnlockNet();
    return ok;
}
bool
ZNDEnet::SetResumeToken(const World::NetProtocol::ResumeToken& in)
{
    LockNet();
    bool ok = transport_.SetResumeToken(in);
    UnlockNet();
    return ok;
}
void
ZNDEnet::ClearResumeToken()
{
    LockNet();
    transport_.ClearResumeToken();
    UnlockNet();
}
bool
ZNDEnet::SetResumeOnly(bool only)
{
    LockNet();
    bool ok = transport_.SetResumeOnly(only);
    UnlockNet();
    return ok;
}
bool
ZNDEnet::SetResumeGraceMs(uint32_t ms)
{
    LockNet();
    bool ok = transport_.SetResumeGraceMs(ms);
    UnlockNet();
    return ok;
}
uint32_t
ZNDEnet::GetResumeGraceMs()
{
    LockNet();
    auto ms = transport_.GetResumeGraceMs();
    UnlockNet();
    return ms;
}

std::uint8_t
ZNDEnet::GetStatus()
{
    LockNet();auto value=transportConnected_?NetUser::STATUS_CONNECTED:running_?NetUser::STATUS_CONNECTING:NetUser::STATUS_DISCONNECTED;UnlockNet();return value;
}

bool
ZNDEnet::GetSessions(SessionInfoVect& dst)
{
    LockNet();
    if (!mySesId_)
    {
        UnlockNet();
        return false;
    }
    LockUsers();
    SessionInfo info;
    info.ID = mySesId_;
    info.name = sessionName_;
    info.pass = false;
    info.players = (std::uint32_t)users_.size();
    info.max_players = maxPlayers_;
    UnlockUsers();
    UnlockNet();
    dst.clear();
    dst.push_back(info);
    return true;
}

void
ZNDEnet::CreateSession(const std::string&, const std::string&, std::uint32_t)
{
    // Direct transport has a fixed single session; creation is an error,
    // matching the no-lobby ZNDSingle behaviour surfaced to windp.
    LockNet();
    const uint64_t eventSession = mySesId_;
    UnlockNet();
    Events_Push(new Event(EVENT_SESSION_FAIL, 0, eventSession));
}

void
ZNDEnet::JoinSession(std::uint64_t SID, const std::string&)
{
    LockNet();
    const bool accepted = SID == mySesId_ && transportConnected_;
    const uint64_t eventSession = mySesId_;
    UnlockNet();
    if (!accepted)
        Events_Push(new Event(EVENT_SESSION_FAIL, 0, eventSession));
    // Already auto-joined: SESSION_JOIN was (or will be) pushed by the pump.
}

void
ZNDEnet::SendDisconnect()
{
    if (!hosting_)
    {
        LockNet();
        transport_.SendGoodbye();
        UnlockNet();
    }
}

void
ZNDEnet::ShowSession(bool show)
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

void
ZNDEnet::CloseSession(std::uint32_t)
{
    if (hosting_)
    {
        LockNet();
        const uint64_t eventSession = mySesId_;
        transport_.CloseSession();
        UnlockNet();
        Stop();
        Events_Push(new Event(EVENT_SESSION_END, 0, eventSession));
        mySesId_ = 0;
    }
    else
        SendDisconnect();
}

void
ZNDEnet::KickUser(std::uint64_t _ID)
{
    if (hosting_ && _ID > 1)
    {
        LockNet();
        transport_.DisconnectPeer((std::uint32_t)(_ID - 1), false);
        UnlockNet();
    }
}

bool
ZNDEnet::GetUsers(UserInfoVect& dst)
{
    LockUsers();
    dst.clear();
    for (const auto& r : roster_.rows)
    {
        UserInfo u;
        u.ID = r.id;
        u.name = r.name;
        u.lead = r.slot == 0;
        u.slot = r.slot;
        u.status = r.status;
        dst.push_back(u);
    }
    UnlockUsers();
    return !dst.empty();
}
bool
ZNDEnet::GetUser(UserInfo& dst, const char* name)
{
    if (!name)
        return false;
    UserInfoVect all;
    GetUsers(all);
    for (const auto& u : all)
        if (u.name == name)
        {
            dst = u;
            return true;
        }
    return false;
}
bool
ZNDEnet::GetUser(UserInfo& dst, std::uint64_t id)
{
    UserInfoVect all;
    GetUsers(all);
    for (const auto& u : all)
        if (u.ID == id)
        {
            dst = u;
            return true;
        }
    return false;
}

// Caller holds the net lock. peer is the transport peer (0 = host on a client).
bool
ZNDEnet::SendFrame(std::uint32_t peer,
                   std::uint8_t type,
                   std::uint64_t address,
                   const void* data,
                   std::uint32_t sz,
                   WireChannel channel)
{
    std::vector<std::uint8_t> frame;
    frame.reserve(DataHeader + sz);
    frame.push_back(type);
    PutU64(frame, address);
    if (sz && data)
        frame.insert(frame.end(), (const std::uint8_t*)data, (const std::uint8_t*)data + sz);
    return transport_.Send(peer, frame.data(), frame.size(), channel);
}

void
ZNDEnet::SendData(std::uint64_t to, void* data, std::uint32_t sz, std::uint8_t flags, std::uint8_t)
{
    const WireChannel channel =
      (flags & PKT_FLAG_GARANT) ? WireChannel::Reliable : WireChannel::Unreliable;
    LockNet();
    if(!hosting_&&!joined_){UnlockNet();return;}
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

void
ZNDEnet::BroadcastData(void* data, std::uint32_t sz, std::uint8_t flags, std::uint8_t)
{
    const WireChannel channel =
      (flags & PKT_FLAG_GARANT) ? WireChannel::Reliable : WireChannel::Unreliable;
    LockNet();
    if(!hosting_&&!joined_){UnlockNet();return;}
    if (hosting_)
    {
        std::vector<std::uint8_t> frame;
        frame.reserve(DataHeader + sz);
        frame.push_back(FrameBroadcast);
        PutU64(frame, 1);
        if (sz && data)
            frame.insert(frame.end(), (const std::uint8_t*)data, (const std::uint8_t*)data + sz);
        transport_.Broadcast(frame.data(), frame.size(), channel);
    }
    else
    {
        SendFrame(0, FrameBroadcast, 0, data, sz, channel); // host relays to everyone else
    }
    UnlockNet();
}

std::uint64_t
ZNDEnet::GetMyID()
{
    LockNet();
    if (hosting_)
    {
        UnlockNet();
        return 1;
    }
    const std::uint32_t id = transport_.MyId();
    UnlockNet();
    return id ? (std::uint64_t)id + 1 : 0;
}

std::string
ZNDEnet::GetMyName()
{
    LockNet();auto name=myName_;UnlockNet();return name;
}

std::uint64_t
ZNDEnet::GetMySesID()
{
    LockNet();auto id=mySesId_;UnlockNet();return id;
}

// Pump thread, net lock held.
void
ZNDEnet::SendRoster()
{
    LockUsers();
    for (const auto& r : roster_.rows)
        if (r.slot && r.status == RosterOnline)
        {
            uint64_t attempt = 0;
            uint32_t gen = 0;
            if (!transport_.PeerBinding(r.id - 1, attempt, gen))
                continue;
            auto ack = acknowledgments_.find(r.id);
            if (ack != acknowledgments_.end() && ack->second >= roster_.revision)
                continue;
            auto bytes = EncodeRoster(roster_, attempt, gen);
            transport_.Send(r.id - 1, bytes.data(), bytes.size(), WireChannel::Reliable);
        }
    UnlockUsers();
    controlNext_ = TransportNow() + 250;
}
void
ZNDEnet::ApplyRoster(const std::uint8_t* data, std::size_t len)
{
    RosterState next;
    uint64_t attempt = 0;
    uint32_t gen = 0;
    if (!DecodeRoster(data, len, next, attempt, gen) || attempt != transport_.Attempt() ||
        gen != transport_.Generation() || next.session != transport_.SessionId())
        return;
    const auto self=std::find_if(next.rows.begin(),next.rows.end(),[&](const RosterRow&row){return row.id==uint64_t(transport_.MyId())+1;});
    if(self==next.rows.end()||self->slot!=transport_.MySlot()||self->name!=myName_||self->status!=RosterOnline)return;
    std::vector<RosterChange> changes;
    bool consolidated = false;
    LockUsers();
    const bool applied = ApplyRosterState(roster_, next, changes, consolidated);
    if (applied)
    {
        users_.clear();
        for (const auto& r : roster_.rows)
            users_[r.id] = r.name;
        sessionName_ = roster_.name;
    }
    UnlockUsers();
    if (applied)
    {
        if (consolidated)
            Events_Push(new Event(EVENT_USER_LIST, 1, next.session));
        else
            for (const auto& c : changes)
            {
                if (c.row.id == uint64_t(transport_.MyId()) + 1)
                    continue;
                auto t = c.transition;
                if (t == Transition::Fresh || t == Transition::Resumed)
                    Events_Push(new EventNameID(EVENT_USER_ADD,
                                                t == Transition::Fresh ? JoinFresh : JoinResumed,
                                                c.row.name,
                                                c.row.id,c.row.slot,next.session));
                else
                    Events_Push(new EventNameID(EVENT_USER_LEAVE,
                                                t == Transition::Away     ? LeaveAway
                                                : t == Transition::Left   ? LeaveLeft
                                                : t == Transition::Kicked ? LeaveKicked
                                                                          : LeaveExpired,
                                                c.row.name,
                                                c.row.id,c.row.slot,next.session));
            }
    }
    if (!joined_)
    {
        joined_ = true;
        transportConnected_ = true;
        mySesId_ = transport_.SessionId();
        Events_Push(new EventNameID(EVENT_CONNECTED, 0, myName_, uint64_t(transport_.MyId()) + 1, transport_.MySlot(), mySesId_));
        Events_Push(new EventNameID(EVENT_SESSION_JOIN, 0, sessionName_, mySesId_, 0xff, mySesId_));
        if (transport_.Resumed())
            Events_Push(new Event(EVENT_SELF_RESUMED, 0, mySesId_));
    }
    ControlWriter w;
    w.U('K', 1);
    w.U(next.session, 8);
    w.U(next.revision, 8);
    transport_.Send(0, w.b.data(), w.b.size(), WireChannel::Reliable);
}

// Pump thread, net lock held. A client frame: deliver to the host's game
// and/or relay to the addressed client(s), keeping the original sender.
void
ZNDEnet::HostDeliver(std::uint64_t from,
                     const std::uint8_t* data,
                     std::size_t len,
                     std::uint8_t channelId)
{
    if (len < DataHeader || (data[0] != FrameDirect && data[0] != FrameBroadcast))
        return;
    const std::uint64_t to = GetU64(data + 1);
    const std::uint8_t* payload = data + DataHeader;
    const std::uint32_t size = (std::uint32_t)(len - DataHeader);
    const WireChannel channel = channelId == 0 ? WireChannel::Reliable : WireChannel::Unreliable;
    const std::uint32_t fromPeer = (std::uint32_t)(from - 1);

    if (data[0] == FrameBroadcast)
    {
        Events_Push(new EventData(EVENT_DATA,
                                  0,
                                  from,
                                  true,
                                  mySesId_,
                                  size,
                                  size ? (std::uint8_t*)payload : nullptr,
                                  channelId, mySesId_));
        std::vector<std::uint8_t> frame(data, data + len);
        for (int i = 0; i < 8; ++i)
            frame[1 + i] = (std::uint8_t)((from >> (8 * i)) & 0xFF);
        transport_.BroadcastExcept(fromPeer, frame.data(), frame.size(), channel);
    }
    else if (to == 1)
    {
        Events_Push(new EventData(
          EVENT_DATA, 0, from, false, 1, size, size ? (std::uint8_t*)payload : nullptr, channelId, mySesId_));
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

void
ZNDEnet::Pump()
{
    while (!pumpEnd_)
    {
        TransportEvent ev;
        LockNet();
        const auto now = TransportNow();
        if (hosting_ && now >= controlNext_)
            SendRoster();
        if (!hosting_ && !joined_ && transport_.MyId() && now >= retryNext_)
        {
            transport_.RetryHandshake();
            retryNext_ = now + 1000;
        }
        if (!hosting_ && !joined_ && now >= connectDeadline_)
        {
            running_ = false;
            Events_Push(new Event(EVENT_CONNERR, ERR_CONN_TIMEOUT, mySesId_));
            UnlockNet();
            break;
        }
        const bool got = transport_.Poll(ev, 0);
        if (!got)
        {
            UnlockNet();
            SDL_Delay(2);
            continue;
        }
        if (ev.type == TransportEvent::Type::Connected)
        {
            if (hosting_)
            {
                const uint64_t id = uint64_t(ev.peer) + 1;
                if(ev.resumed)Events_ClearDataFrom(id);
                RosterRow row{ id, ev.slot, RosterOnline, ev.name };
                LockUsers();
                roster_.rows.erase(std::remove_if(roster_.rows.begin(),
                                                  roster_.rows.end(),
                                                  [&](const RosterRow& r) { return r.id == id; }),
                                   roster_.rows.end());
                roster_.rows.push_back(row);
                std::sort(roster_.rows.begin(),
                          roster_.rows.end(),
                          [](const RosterRow& a, const RosterRow& b) { return a.slot < b.slot; });
                roster_.Changed(row, ev.resumed ? Transition::Resumed : Transition::Fresh);
                users_[id] = ev.name;
                acknowledgments_.erase(id);
                UnlockUsers();
                Events_Push(new EventNameID(
                  EVENT_USER_ADD, ev.resumed ? JoinResumed : JoinFresh, ev.name, id,ev.slot,mySesId_));
                SendRoster();
            }
            else
                myName_ = ev.name;
        }
        else if (ev.type == TransportEvent::Type::Disconnected ||
                 ev.type == TransportEvent::Type::Expired)
        {
            if (hosting_)
            {
                const uint64_t id = uint64_t(ev.peer) + 1;
                LockUsers();
                RosterRow row{ id, ev.slot, RosterAway, ev.name };
                auto it = std::find_if(roster_.rows.begin(),
                                       roster_.rows.end(),
                                       [&](const RosterRow& r) { return r.id == id; });
                if (it != roster_.rows.end())
                {
                    row = *it;
                    row.status = RosterAway;
                    if (ev.reason == LeaveAway)
                        it->status = RosterAway;
                    else
                        roster_.rows.erase(it);
                }
                if (ev.reason != LeaveAway)
                    users_.erase(id);
                acknowledgments_.erase(id);
                roster_.Changed(row,
                                ev.reason == LeaveAway     ? Transition::Away
                                : ev.reason == LeaveLeft   ? Transition::Left
                                : ev.reason == LeaveKicked ? Transition::Kicked
                                                           : Transition::Expired);
                UnlockUsers();
                Events_Push(new EventNameID(EVENT_USER_LEAVE, ev.reason, row.name, id,row.slot,mySesId_));
                SendRoster();
            }
            else
            {
                if (ev.reason == 3)
                    Events_Push(new Event(EVENT_SESSION_END, 0, mySesId_));
                Events_Push(new Event(joined_ ? EVENT_DISCONNECT : EVENT_CONNERR,
                                      joined_ ? ev.reason : ConnErrFor(ev.reason), mySesId_));
                transportConnected_ = false;
                joined_ = false;
                running_ = false;
                UnlockNet();
                break;
            }
        }
        else if (ev.type == TransportEvent::Type::Data)
        {
            if (hosting_)
            {
                if (ev.channel == 0 && !ev.data.empty() && ev.data[0] == 'K')
                {
                    ControlReader r(ev.data.data(), ev.data.size());
                    r.U(1);
                    auto sid = r.U(8), rev = r.U(8);
                    if (r.End() && sid == roster_.session && rev <= roster_.revision)
                        acknowledgments_[uint64_t(ev.peer) + 1] = rev;
                }
                else
                    HostDeliver(uint64_t(ev.peer) + 1, ev.data.data(), ev.data.size(), ev.channel);
            }
            else if (!ev.data.empty() && ev.data[0] == FrameRoster && ev.channel == 0)
                ApplyRoster(ev.data.data(), ev.data.size());
            else if (joined_ && ev.data.size() >= DataHeader &&
                     (ev.data[0] == FrameDirect || ev.data[0] == FrameBroadcast))
            {
                const bool cast = ev.data[0] == FrameBroadcast;
                auto from = GetU64(ev.data.data() + 1);
                auto size = ev.data.size() - DataHeader;
                Events_Push(new EventData(EVENT_DATA,
                                          0,
                                          from,
                                          cast,
                                          cast ? mySesId_ : uint64_t(transport_.MyId()) + 1,
                                          size,
                                          size ? ev.data.data() + DataHeader : nullptr,
                                          ev.channel, mySesId_));
            }
        }
        UnlockNet();
    }
}
}
