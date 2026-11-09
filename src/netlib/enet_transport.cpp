#include "enet_transport.h"
#include "world/net_protocol.h"
#include <atomic>
#include "os_random.h"
#include <cstring>

namespace ZNDNet
{
namespace
{

std::atomic<int> g_enetRefs(0);

bool
EnetAcquire()
{
    if (g_enetRefs.fetch_add(1) == 0 && enet_initialize() != 0)
    {
        g_enetRefs.fetch_sub(1);
        return false;
    }
    return true;
}
void
EnetRelease()
{
    if (g_enetRefs.fetch_sub(1) == 1)
        enet_deinitialize();
}

void
PutU16(std::vector<std::uint8_t>& buf, std::uint16_t v)
{
    buf.push_back((std::uint8_t)(v & 0xFF));
    buf.push_back((std::uint8_t)((v >> 8) & 0xFF));
}
void
PutU32(std::vector<std::uint8_t>& buf, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        buf.push_back((std::uint8_t)((v >> (8 * i)) & 0xFF));
}
std::uint16_t
GetU16(const std::uint8_t* p)
{
    return (std::uint16_t)(p[0] | ((std::uint16_t)p[1] << 8));
}
std::uint32_t
GetU32(const std::uint8_t* p)
{
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) |
           ((std::uint32_t)p[3] << 24);
}

} // namespace

EnetTransport::EnetTransport(const std::string& s)
  : servString_(s)
{
}
EnetTransport::~EnetTransport()
{
    Stop();
}
bool
EnetTransport::GetResumeToken(World::NetProtocol::ResumeToken& out) const
{
    std::memcpy(out.b, myToken_, 16);
    return World::NetProtocol::TokenValid(out);
}
bool
EnetTransport::SetResumeToken(const World::NetProtocol::ResumeToken& in)
{
    if (host_)
        return false;
    std::memcpy(myToken_, in.b, 16);
    resumeOnly_ = World::NetProtocol::TokenValid(in);
    return true;
}
void
EnetTransport::ClearResumeToken()
{
    if (!host_)
    {
        std::memset(myToken_, 0, 16);
        resumeOnly_ = false;
    }
}
bool
EnetTransport::SetResumeOnly(bool only)
{
    if (host_)
        return false;
    resumeOnly_ = only;
    return true;
}
bool
EnetTransport::SetResumeGraceMs(std::uint32_t ms)
{
    if (host_ || ms < 100 || ms > 600000)
        return false;
    graceMs_ = ms;
    return true;
}
bool
EnetTransport::GenerateToken(unsigned char out[16], std::uint32_t)
{
    return OsRandom(out, 16);
}
bool
EnetTransport::StartHost(const std::string& name, std::uint16_t port, std::size_t maxPeers)
{
    Stop();
    if (!OsRandom(&session_, sizeof(session_)) || !EnetAcquire())
        return false;
    ENetAddress a{ ENET_HOST_ANY, port };
    maxClients_ = std::min<std::size_t>(5, maxPeers);
    host_ = enet_host_create(&a, maxClients_ + 6, 2, 0, 0);
    if (!host_)
    {
        EnetRelease();
        return false;
    }
    host_->maximumPacketSize = 1048576;
    name_ = name;
    isHost_ = true;
    acceptingNew_ = true;
    nextGeneration_ = 0;
    nextId_ = 1;
    terminal_ = 0;
    return true;
}
bool
EnetTransport::Connect(const std::string& name, const std::string& host, std::uint16_t port)
{
    Stop();
    if (name.empty() || name.size() > 20 || !OsRandom(&attempt_, sizeof(attempt_)) ||
        !EnetAcquire())
        return false;
    host_ = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!host_)
    {
        EnetRelease();
        return false;
    }
    ENetAddress a{};
    if (enet_address_set_host(&a, host.c_str()))
    {
        Stop();
        return false;
    }
    a.port = port;
    serverPeer_ = enet_host_connect(host_, &a, 2, 0);
    if (!serverPeer_)
    {
        Stop();
        return false;
    }
    name_ = name;
    isHost_ = false;
    clientAccepted_ = false;
    refused_ = false;
    terminal_ = 0;
    deadline_ = TransportNow() + 10000;
    retryNext_ = 0;
    session_ = 0;
    return true;
}
void
EnetTransport::Stop()
{
    if (host_)
    {
        for (auto& kv : peers_)
            enet_peer_disconnect_now(kv.first, kv.second.terminal ? kv.second.terminal : terminal_);
        if (serverPeer_)
            enet_peer_disconnect_now(serverPeer_, terminal_);
        enet_host_destroy(host_);
        host_ = nullptr;
        EnetRelease();
    }
    peers_.clear();
    orphans_.clear();
    pendingEvents_.clear();
    handshakeBudget_ = HandshakeBudget();
    serverPeer_ = nullptr;
    clientAccepted_ = false;
    myId_ = 0;
    isHost_ = false;
    terminal_ = 0;
    generation_ = 0;
}
std::uint16_t
EnetTransport::BoundPort() const
{
    if (!host_ || !isHost_)
        return 0;
    ENetAddress a{};
    return enet_socket_get_address(host_->socket, &a) ? 0 : a.port;
}
EnetTransport::PeerInfo*
EnetTransport::FindPeer(std::uint32_t id)
{
    for (auto& kv : peers_)
        if (kv.second.accepted && kv.second.id == id)
            return &kv.second;
    return nullptr;
}
const EnetTransport::PeerInfo*
EnetTransport::FindPeer(std::uint32_t id) const
{
    for (const auto& kv : peers_)
        if (kv.second.accepted && kv.second.id == id)
            return &kv.second;
    return nullptr;
}
void
EnetTransport::Accept(PeerInfo& i)
{
    ControlWriter w;
    w.U('A', 1);
    w.U(i.id, 4);
    w.b.insert(w.b.end(), i.token, i.token + 16);
    w.U(i.resumed, 1);
    w.U(i.slot, 1);
    w.U(graceMs_, 4);
    w.U(i.attempt, 8);
    w.U(i.generation, 4);
    w.U(session_, 8);
    w.Str(i.name);
    auto* p = enet_packet_create(w.b.data(), w.b.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(i.peer, 0, p);
    enet_host_flush(host_);
}
void
EnetTransport::Handshake()
{
    if (!serverPeer_ || serverPeer_->state != ENET_PEER_STATE_CONNECTED)
        return;
    ControlWriter w;
    w.U('H', 1);
    w.Str(servString_);
    w.Str(name_);
    w.U(World::NetProtocol::ProtoVer, 2);
    w.b.insert(w.b.end(), myToken_, myToken_ + 16);
    w.U(FramingVersion, 1);
    World::NetProtocol::ResumeToken token{};
    std::memcpy(token.b, myToken_, 16);
    w.U(resumeOnly_ || World::NetProtocol::TokenValid(token), 1);
    w.U(attempt_, 8);
    auto* p = enet_packet_create(w.b.data(), w.b.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(serverPeer_, 0, p);
    enet_host_flush(host_);
    retryNext_ = TransportNow() + 1000;
}
void
EnetTransport::RetryHandshake()
{
    Handshake();
}
bool
EnetTransport::PeerBinding(std::uint32_t id,
                           std::uint64_t& attempt,
                           std::uint32_t& generation) const
{
    auto* i = FindPeer(id);
    if (!i)
        return false;
    attempt = i->attempt;
    generation = i->generation;
    return true;
}
bool
EnetTransport::HandleHandshake(PeerInfo& info,
                               const std::uint8_t* data,
                               std::size_t len,
                               TransportEvent& out)
{
    if (len > 512 || !handshakeBudget_.Allow(info.peer->address.host))
        return false;
    ControlReader r(data, len);
    if (r.U(1) != 'H')
        return false;
    auto svc = r.Str(255);
    auto name = r.Str(20);
    auto version = r.U(2);
    auto reject = [&](std::uint8_t reason)
    {
        uint8_t msg[2] = { 'R', reason };
        auto* p = enet_packet_create(msg, 2, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(info.peer, 0, p);
        enet_host_flush(host_);
        enet_peer_disconnect_later(info.peer, 0);
        return false;
    };
    if (!r.ok)
        return false;
    if (svc != servString_)
        return reject(RefuseService);
    if (!World::NetProtocol::ProtoCompatible(version))
        return reject(RefuseVersion);
    World::NetProtocol::ResumeToken want{};
    for (auto& v : want.b)
        v = r.U(1);
    auto frame = r.U(1), flags = r.U(1), attempt = r.U(8);
    if (frame != FramingVersion)
        return reject(RefuseVersion);
    if(terminal_==3)return reject(RefuseClosed);
    if (!r.End() || flags > 1 || !attempt || name.empty())
        return reject(RefuseName);
    if (info.accepted)
    {
        if (info.attempt == attempt)
        {
            Accept(info);
            return false;
        }
        return reject(RefuseResume);
    }
    PeerInfo* old = nullptr;
    auto orphan = orphans_.end();
    if (World::NetProtocol::TokenValid(want))
    {
        for (auto& kv : peers_)
            if (kv.second.accepted && kv.second.terminal==0)
            {
                World::NetProtocol::ResumeToken have{};
                std::memcpy(have.b, kv.second.token, 16);
                if (World::NetProtocol::TokenEqual(want, have))
                {
                    old = &kv.second;
                    break;
                }
            }
        for (auto it = orphans_.begin(); it != orphans_.end(); ++it)
        {
            World::NetProtocol::ResumeToken have{};
            std::memcpy(have.b, it->token, 16);
            if (it->expiryMs > TransportNow() && World::NetProtocol::TokenEqual(want, have))
            {
                orphan = it;
                break;
            }
        }
    }
    for (const auto& kv : peers_)
        if (kv.second.accepted && &kv.second != old &&
            kv.first->address.host == info.peer->address.host &&
            kv.first->address.port == info.peer->address.port)
            return reject(RefuseResume);
    if (old || orphan != orphans_.end())
    {
        if (old)
        {
            info.id = old->id;
            info.name = old->name;
            info.slot = old->slot;
            std::memcpy(info.token, old->token, 16);
            old->accepted = false;
            old->graceful = true;
            old->terminal = 4;
            enet_peer_disconnect_now(old->peer, 4);
        }
        else
        {
            info.id = orphan->id;
            info.name = orphan->name;
            info.slot = orphan->slot;
            std::memcpy(info.token, orphan->token, 16);
            orphans_.erase(orphan);
        }
        info.resumed = true;
    }
    else
    {
        if (World::NetProtocol::TokenValid(want) || flags)
            return reject(RefuseResume);
        std::size_t count = orphans_.size();
        bool taken = CallsignEqual(name,name_);
        std::array<bool, 6> slots{};
        slots[0] = true;
        for (const auto& kv : peers_)
            if (kv.second.accepted)
            {
                ++count;
                slots[kv.second.slot] = true;
                if (CallsignEqual(kv.second.name,name))
                    taken = true;
            }
        for (const auto& o : orphans_)
        {
            slots[o.slot] = true;
            if (CallsignEqual(o.name,name))
                taken = true;
        }
        if (!acceptingNew_)
            return reject(RefuseClosed);
        if (count >= maxClients_)
            return reject(RefuseFull);
        if (taken)
            return reject(RefuseName);
        info.id = nextId_++;
        info.name = name;
        info.slot = 1;
        while (info.slot < 6 && slots[info.slot])
            ++info.slot;
        if (!GenerateToken(info.token, info.id))
            return reject(RefuseRandom);
    }
    info.attempt = attempt;
    info.generation = ++nextGeneration_;
    info.accepted = true;
    Accept(info);
    out = TransportEvent();
    out.type = TransportEvent::Type::Connected;
    out.peer = info.id;
    out.name = info.name;
    out.resumed = info.resumed;
    out.slot = info.slot;
    return true;
}
bool
EnetTransport::Maintenance(TransportEvent& out)
{
    if(!pendingEvents_.empty()){out=pendingEvents_.front();pendingEvents_.pop_front();return true;}
    const auto now = TransportNow();
    if (isHost_)
    {
        for (auto it = orphans_.begin(); it != orphans_.end(); ++it)
            if (now >= it->expiryMs)
            {
                out = TransportEvent();
                out.type = TransportEvent::Type::Expired;
                out.peer = it->id;
                out.name = it->name;
                out.slot = it->slot;
                out.reason = it->terminalReason;
                orphans_.erase(it);
                return true;
            }
        for (auto it = peers_.begin(); it != peers_.end();)
            if (!it->second.accepted && now >= it->second.deadline)
            {
                enet_peer_disconnect_now(it->first, it->second.terminal);
                it = peers_.erase(it);
            }
            else
                ++it;
    }
    else if (!clientAccepted_ && !refused_)
    {
        if (now >= deadline_)
        {
            refused_ = true;
            out = TransportEvent();
            out.type = TransportEvent::Type::Disconnected;
            out.reason = 0;
            return true;
        }
        if (now >= retryNext_)
            Handshake();
    }
    return false;
}
bool
EnetTransport::Poll(TransportEvent& out, std::uint32_t timeoutMs)
{
    if (!host_)
        return false;
    const auto end = TransportNow() + timeoutMs;
    unsigned serviced=0;
    for (;;)
    {
        if(serviced++>=64 || (timeoutMs && serviced>1 && TransportNow()>=end))return false;
        if (Maintenance(out))
            return true;
        ENetEvent ev{};
        auto now = TransportNow();
        auto wait = now < end ? std::min<std::uint64_t>(end - now, 50) : 0;
        int rc = enet_host_service(host_, &ev, wait);
        if (rc <= 0)
        {
            if (Maintenance(out))
                return true;
            return false;
        }
        if (ev.type == ENET_EVENT_TYPE_CONNECT)
        {
            if (isHost_)
            {
                PeerInfo i;
                i.peer = ev.peer;
                i.deadline = TransportNow() + 10000;
                peers_[ev.peer] = i;
            }
            else
                Handshake();
            continue;
        }
        if (ev.type == ENET_EVENT_TYPE_RECEIVE)
        {
            bool emit = false;
            const auto* data = ev.packet->data;
            const auto len = ev.packet->dataLength;
            if (isHost_)
            {
                auto it = peers_.find(ev.peer);
                if (it != peers_.end())
                {
                    if (ev.channelID == 0 && len && data[0] == 'H')
                        emit = HandleHandshake(it->second, data, len, out);
                    else if (it->second.accepted && len >= 5 && data[0] == 'P' &&
                             GetU32(data + 1) == it->second.generation)
                    {
                        out = TransportEvent();
                        out.type = TransportEvent::Type::Data;
                        out.peer = it->second.id;
                        out.channel = ev.channelID;
                        out.data.assign(data + 5, data + len);
                        emit = true;
                    }
                }
            }
            else if (ev.channelID == 0 && len && data[0] == 'A')
            {
                ControlReader r(data, len);
                r.U(1);
                auto id = r.U(4);
                World::NetProtocol::ResumeToken token{};
                for (auto& v : token.b)
                    v = r.U(1);
                auto resumed = r.U(1), slot = r.U(1), grace = r.U(4), attempt = r.U(8),
                     generation = r.U(4), session = r.U(8);
                auto name = r.Str(20);
                if (r.End() && id && slot > 0 && slot < 6 && resumed <= 1 && grace >= 100 &&
                    grace <= 600000 && attempt == attempt_ && generation && session &&
                    World::NetProtocol::TokenValid(token) &&
                    (!clientAccepted_ ||
                     (myId_ == id && generation_ == generation && session_ == session)))
                {
                    bool first = !clientAccepted_;
                    myId_ = id;
                    slot_ = slot;
                    graceMs_ = grace;
                    generation_ = generation;
                    session_ = session;
                    name_ = name;
                    resumed_ = resumed;
                    std::memcpy(myToken_, token.b, 16);
                    clientAccepted_ = true;
                    if (first)
                    {
                        out = TransportEvent();
                        out.type = TransportEvent::Type::Connected;
                        out.name = name_;
                        out.resumed = resumed_;
                        out.slot = slot_;
                        emit = true;
                    }
                }
            }
            else if (!clientAccepted_ && len == 2 && data[0] == 'R')
            {
                refused_ = true;
                out = TransportEvent();
                out.type = TransportEvent::Type::Disconnected;
                out.reason = data[1];
                emit = true;
            }
            else if (clientAccepted_ && len >= 5 && data[0] == 'P' &&
                     GetU32(data + 1) == generation_)
            {
                out = TransportEvent();
                out.type = TransportEvent::Type::Data;
                out.channel = ev.channelID;
                out.data.assign(data + 5, data + len);
                emit = true;
            }
            enet_packet_destroy(ev.packet);
            if (emit)
                return true;
            continue;
        }
        if (ev.type == ENET_EVENT_TYPE_DISCONNECT)
        {
            if (isHost_)
            {
                auto it = peers_.find(ev.peer);
                if (it != peers_.end())
                {
                    auto i = it->second;
                    peers_.erase(it);
                    if (i.accepted)
                    {
                        auto reason = i.terminal ? i.terminal : ev.data;
                        out = TransportEvent();
                        out.type = TransportEvent::Type::Disconnected;
                        out.peer = i.id;
                        out.name = i.name;
                        out.slot = i.slot;
                        out.reason = reason == 1   ? LeaveLeft
                                     : reason == 2 ? LeaveKicked
                                                   : LeaveAway;
                        if (out.reason == LeaveAway)
                        {
                            Orphan o;
                            std::memcpy(o.token, i.token, 16);
                            o.id = i.id;
                            o.name = i.name;
                            o.slot = i.slot;
                            o.expiryMs = TransportNow() + graceMs_;
                            orphans_.push_back(o);
                        }
                        return true;
                    }
                }
            }
            else
            {
                bool notify = clientAccepted_ || !refused_;
                clientAccepted_ = false;
                serverPeer_ = nullptr;
                refused_ = true;
                if (ev.data == 1 || ev.data == 2 || ev.data == 3)
                {
                    std::memset(myToken_, 0, 16);
                    resumeOnly_ = false;
                }
                if (notify)
                {
                    out = TransportEvent();
                    out.type = TransportEvent::Type::Disconnected;
                    out.reason = ev.data;
                    return true;
                }
            }
            continue;
        }
        if (TransportNow() >= end)
            return false;
    }
}
bool
EnetTransport::Send(std::uint32_t peer, const void* data, std::size_t size, WireChannel channel)
{
    if (!host_ || !data || size > 1048576)
        return false;
    ENetPeer* target = nullptr;
    std::uint32_t generation = 0;
    if (isHost_)
    {
        auto* i = FindPeer(peer);
        if (!i)
            return false;
        target = i->peer;
        generation = i->generation;
    }
    else
    {
        if (peer || !clientAccepted_ || !serverPeer_)
            return false;
        target = serverPeer_;
        generation = generation_;
    }
    ControlWriter w;
    w.U('P', 1);
    w.U(generation, 4);
    const auto* p = static_cast<const uint8_t*>(data);
    w.b.insert(w.b.end(), p, p + size);
    auto* packet = enet_packet_create(
      w.b.data(), w.b.size(), channel == WireChannel::Reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (enet_peer_send(target, static_cast<uint8_t>(channel), packet))
    {
        enet_packet_destroy(packet);
        return false;
    }
    enet_host_flush(host_);
    return true;
}
bool
EnetTransport::Broadcast(const void* data, std::size_t size, WireChannel channel)
{
    return BroadcastExcept(0, data, size, channel);
}
bool
EnetTransport::BroadcastExcept(std::uint32_t except,
                               const void* data,
                               std::size_t size,
                               WireChannel channel)
{
    if (!host_ || !data)
        return false;
    if (!isHost_)
        return Send(0, data, size, channel);
    for (auto& kv : peers_)
        if (kv.second.accepted && kv.second.id != except)
            Send(kv.second.id, data, size, channel);
    return true;
}
bool
EnetTransport::SendGoodbye()
{
    if (!host_ || isHost_ || !serverPeer_)
        return false;
    terminal_ = 1;
    std::memset(myToken_, 0, 16);
    resumeOnly_ = false;
    enet_peer_disconnect(serverPeer_, 1);
    enet_host_flush(host_);
    return true;
}
void
EnetTransport::CloseSession()
{
    if (!host_ || !isHost_)
        return;
    terminal_ = 3;
    acceptingNew_ = false;
    for (auto& kv : peers_)
    {
        kv.second.terminal = 3;
        std::memset(kv.second.token,0,16);
        enet_peer_disconnect(kv.first, 3);
    }
    orphans_.clear();
    enet_host_flush(host_);
}
void
EnetTransport::DisconnectPeer(std::uint32_t id, bool allowResume)
{
    if (!host_ || !isHost_)
        return;
    if (auto* i = FindPeer(id))
    {
        i->terminal=allowResume?0:2;
        i->graceful=!allowResume;
        if(!allowResume){
            TransportEvent ev;ev.type=TransportEvent::Type::Disconnected;ev.peer=i->id;ev.name=i->name;ev.slot=i->slot;ev.reason=LeaveKicked;pendingEvents_.push_back(ev);
            std::memset(i->token,0,16);i->accepted=false;i->deadline=TransportNow()+500;
        }
        enet_peer_disconnect(i->peer,i->terminal);enet_host_flush(host_);
    }
    else if (!allowResume)
        for (auto it = orphans_.begin(); it != orphans_.end(); ++it)
            if (it->id == id)
            {
                it->expiryMs = 0;
                it->terminalReason = LeaveKicked;
                break;
            }
}
std::uint32_t
EnetTransport::RttMs(std::uint32_t id) const
{
    if (!host_)
        return 0;
    if (isHost_)
    {
        auto* i = FindPeer(id);
        return i ? i->peer->roundTripTime : 0;
    }
    return serverPeer_ ? serverPeer_->roundTripTime : 0;
}
}
