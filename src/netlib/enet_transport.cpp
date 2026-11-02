#include "enet_transport.h"
#include "world/net_protocol.h"
#include <atomic>
#include <cstring>

namespace ZNDNet
{
namespace {

std::atomic<int> g_enetRefs(0);

bool EnetAcquire()
{
    if (g_enetRefs.fetch_add(1) == 0 && enet_initialize() != 0)
    {
        g_enetRefs.fetch_sub(1);
        return false;
    }
    return true;
}
void EnetRelease()
{
    if (g_enetRefs.fetch_sub(1) == 1)
        enet_deinitialize();
}

void PutU16(std::vector<std::uint8_t> &buf, std::uint16_t v)
{
    buf.push_back((std::uint8_t)(v & 0xFF));
    buf.push_back((std::uint8_t)((v >> 8) & 0xFF));
}
void PutU32(std::vector<std::uint8_t> &buf, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        buf.push_back((std::uint8_t)((v >> (8 * i)) & 0xFF));
}
std::uint16_t GetU16(const std::uint8_t *p)
{
    return (std::uint16_t)(p[0] | ((std::uint16_t)p[1] << 8));
}
std::uint32_t GetU32(const std::uint8_t *p)
{
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
           ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}

} // namespace

EnetTransport::EnetTransport(const std::string &servstring)
    : servString_(servstring)
{
}

EnetTransport::~EnetTransport()
{
    Stop();
}

bool EnetTransport::StartHost(const std::string &name, std::uint16_t port, std::size_t maxPeers)
{
    Stop();
    if (!EnetAcquire())
        return false;
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;
    host_ = enet_host_create(&address, maxPeers, 2, 0, 0);
    if (!host_)
    {
        EnetRelease();
        return false;
    }
    name_ = name;
    isHost_ = true;
    return true;
}

bool EnetTransport::Connect(const std::string &name, const std::string &host, std::uint16_t port)
{
    Stop();
    if (!EnetAcquire())
        return false;
    host_ = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!host_)
    {
        EnetRelease();
        return false;
    }
    ENetAddress address;
    if (enet_address_set_host(&address, host.c_str()) != 0)
    {
        Stop();
        return false;
    }
    address.port = port;
    serverPeer_ = enet_host_connect(host_, &address, 2, 0);
    if (!serverPeer_)
    {
        Stop();
        return false;
    }
    name_ = name;
    isHost_ = false;
    clientAccepted_ = false;
    myId_ = 0;
    return true;
}

void EnetTransport::Stop()
{
    if (host_)
    {
        for (auto &kv : peers_)
            enet_peer_disconnect_now(kv.first, 0);
        if (serverPeer_)
            enet_peer_disconnect_now(serverPeer_, 0);
        enet_host_destroy(host_);
        host_ = nullptr;
        EnetRelease();
    }
    peers_.clear();
    orphans_.clear();
    serverPeer_ = nullptr;
    clientAccepted_ = false;
    myId_ = 0;
    isHost_ = false;
    // myToken_ intentionally persists across Stop/Connect for resume.
}

std::uint16_t EnetTransport::BoundPort() const
{
    if (!host_ || !isHost_)
        return 0;
    ENetAddress address;
    if (enet_socket_get_address(host_->socket, &address) != 0)
        return 0;
    return address.port;
}

EnetTransport::PeerInfo *EnetTransport::FindPeer(std::uint32_t id)
{
    for (auto &kv : peers_)
        if (kv.second.accepted && kv.second.id == id)
            return &kv.second;
    return nullptr;
}
const EnetTransport::PeerInfo *EnetTransport::FindPeer(std::uint32_t id) const
{
    for (const auto &kv : peers_)
        if (kv.second.accepted && kv.second.id == id)
            return &kv.second;
    return nullptr;
}

void EnetTransport::GenerateToken(unsigned char out[16], std::uint32_t id)
{
    std::uint64_t z = (std::uint64_t)enet_time_get()
        ^ ((std::uint64_t)id * 0x9E3779B97F4A7C15ULL)
        ^ (++tokenCounter_ * 0xD1B54A32D192ED03ULL);
    for (int i = 0; i < 2; ++i)
    {
        z += 0x9E3779B97F4A7C15ULL;
        std::uint64_t x = z;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        x = x ^ (x >> 31);
        std::memcpy(&out[i * 8], &x, 8);
    }
    bool any = false;
    for (int i = 0; i < 16; ++i)
        if (out[i]) any = true;
    if (!any)
        out[0] = 1;
}

bool EnetTransport::HandleHandshake(PeerInfo &info, const std::uint8_t *data,
                                    std::size_t len, TransportEvent &out)
{
    // 'H' svcLen svc nameLen name verU16 token16
    if (len < 1 + 1 + 1 + 2 + 16 || data[0] != 'H')
        return false;
    const std::size_t svcLen = data[1];
    if (len < 1 + 1 + svcLen + 1 + 2 + 16)
        return false;
    const std::string svc((const char *)data + 2, svcLen);
    const std::size_t nameOff = 2 + svcLen;
    const std::size_t nameLen = data[nameOff];
    if (len != nameOff + 1 + nameLen + 2 + 16)
        return false;
    const std::string name((const char *)data + nameOff + 1, nameLen);
    const std::uint16_t ver = GetU16(data + nameOff + 1 + nameLen);
    const std::uint8_t *token = data + nameOff + 1 + nameLen + 2;

    auto reject = [&](std::uint8_t reason) {
        std::uint8_t msg[2] = { 'R', reason };
        ENetPacket *pkt = enet_packet_create(msg, 2, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(info.peer, 0, pkt);
        enet_host_flush(host_);
        enet_peer_disconnect_later(info.peer, 0);
        return false;
    };
    if (svc != servString_)
        return reject(1);
    if (!World::NetProtocol::ProtoCompatible(ver))
        return reject(2);

    World::NetProtocol::ResumeToken want;
    std::memcpy(want.b, token, 16);
    const std::uint64_t now = enet_time_get();
    if (World::NetProtocol::TokenValid(want))
    {
        for (auto it = orphans_.begin(); it != orphans_.end(); ++it)
        {
            if (it->expiryMs <= now)
                continue;
            World::NetProtocol::ResumeToken have;
            std::memcpy(have.b, it->token, 16);
            if (!World::NetProtocol::TokenEqual(want, have))
                continue;
            info.id = it->id;
            info.name = it->name;
            std::memcpy(info.token, it->token, 16);
            orphans_.erase(it);
            info.accepted = true;
            std::vector<std::uint8_t> accept;
            accept.push_back('A');
            PutU32(accept, info.id);
            accept.insert(accept.end(), info.token, info.token + 16);
            ENetPacket *pkt = enet_packet_create(accept.data(), accept.size(),
                                                 ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(info.peer, 0, pkt);
            out.type = TransportEvent::Type::Connected;
            out.peer = info.id;
            out.name = info.name;
            return true;
        }
    }

    info.id = nextId_++;
    info.name = name;
    GenerateToken(info.token, info.id);
    info.accepted = true;
    std::vector<std::uint8_t> accept;
    accept.push_back('A');
    PutU32(accept, info.id);
    accept.insert(accept.end(), info.token, info.token + 16);
    ENetPacket *pkt = enet_packet_create(accept.data(), accept.size(),
                                         ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(info.peer, 0, pkt);
    out.type = TransportEvent::Type::Connected;
    out.peer = info.id;
    out.name = info.name;
    return true;
}

bool EnetTransport::Poll(TransportEvent &out, std::uint32_t timeoutMs)
{
    if (!host_)
        return false;
    const std::uint32_t deadline = enet_time_get() + timeoutMs;
    for (;;)
    {
        const std::uint32_t now = enet_time_get();
        const std::uint32_t wait = (std::int32_t)(deadline - now) > 0
            ? deadline - now : 0;
        ENetEvent ev;
        const int rc = enet_host_service(host_, &ev, wait);
        if (rc < 0)
            return false;
        if (rc == 0)
            return false; // timeout with nothing to report

        if (ev.type == ENET_EVENT_TYPE_CONNECT)
        {
            if (isHost_)
            {
                PeerInfo info;
                info.peer = ev.peer;
                peers_[ev.peer] = info;
            }
            else
            {
                // Send handshake: 'H' svcLen svc nameLen name ver token
                std::vector<std::uint8_t> hs;
                hs.push_back('H');
                hs.push_back((std::uint8_t)servString_.size());
                hs.insert(hs.end(), servString_.begin(), servString_.end());
                hs.push_back((std::uint8_t)name_.size());
                hs.insert(hs.end(), name_.begin(), name_.end());
                PutU16(hs, (std::uint16_t)World::NetProtocol::ProtoVer);
                hs.insert(hs.end(), myToken_, myToken_ + 16);
                ENetPacket *pkt = enet_packet_create(hs.data(), hs.size(),
                                                     ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(ev.peer, 0, pkt);
                enet_host_flush(host_);
            }
            continue;
        }

        if (ev.type == ENET_EVENT_TYPE_RECEIVE)
        {
            const std::uint8_t *data = ev.packet->data;
            const std::size_t len = ev.packet->dataLength;
            const std::uint8_t channel = (std::uint8_t)ev.channelID;
            bool emit = false;
            if (isHost_)
            {
                auto it = peers_.find(ev.peer);
                if (it != peers_.end())
                {
                    if (!it->second.accepted)
                    {
                        emit = HandleHandshake(it->second, data, len, out);
                    }
                    else if (channel == 0 && len == 1 && data[0] == 'X')
                    {
                        it->second.graceful = true;
                        enet_peer_disconnect_later(ev.peer, 0);
                    }
                    else
                    {
                        out.type = TransportEvent::Type::Data;
                        out.peer = it->second.id;
                        out.channel = channel;
                        out.data.assign(data, data + len);
                        emit = true;
                    }
                }
            }
            else if (!clientAccepted_)
            {
                if (len == 1 + 4 + 16 && data[0] == 'A')
                {
                    myId_ = GetU32(data + 1);
                    std::memcpy(myToken_, data + 5, 16);
                    clientAccepted_ = true;
                    out.type = TransportEvent::Type::Connected;
                    out.peer = 0;
                    emit = true;
                }
                else if (len == 2 && data[0] == 'R')
                {
                    out.type = TransportEvent::Type::Disconnected;
                    out.peer = 0;
                    emit = true;
                }
            }
            else if (channel == 0 && len == 1 && data[0] == 'X')
            {
                // Host is closing: surface as a disconnect, not game data.
                clientAccepted_ = false;
                out.type = TransportEvent::Type::Disconnected;
                out.peer = 0;
                emit = true;
            }
            else
            {
                out.type = TransportEvent::Type::Data;
                out.peer = 0;
                out.channel = channel;
                out.data.assign(data, data + len);
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
                    const bool wasAccepted = it->second.accepted;
                    const bool wasGraceful = it->second.graceful;
                    const std::uint32_t id = it->second.id;
                    if (wasAccepted && !wasGraceful)
                    {
                        Orphan orphan;
                        std::memcpy(orphan.token, it->second.token, 16);
                        orphan.id = id;
                        orphan.name = it->second.name;
                        orphan.expiryMs = (std::uint64_t)enet_time_get() + 60000;
                        orphans_.push_back(orphan);
                    }
                    peers_.erase(it);
                    if (wasAccepted)
                    {
                        out.type = TransportEvent::Type::Disconnected;
                        out.peer = id;
                        return true;
                    }
                }
            }
            else
            {
                const bool wasAccepted = clientAccepted_;
                clientAccepted_ = false;
                serverPeer_ = nullptr;
                if (wasAccepted)
                {
                    out.type = TransportEvent::Type::Disconnected;
                    out.peer = 0;
                    return true;
                }
            }
            continue;
        }
        // ENET_EVENT_TYPE_NONE: keep servicing until deadline.
        if ((std::int32_t)(enet_time_get() - deadline) >= 0)
            return false;
    }
}

bool EnetTransport::Send(std::uint32_t peer, const void *data, std::size_t size,
                         WireChannel channel)
{
    if (!host_ || !data)
        return false;
    ENetPeer *target = nullptr;
    if (isHost_)
    {
        PeerInfo *info = FindPeer(peer);
        if (!info)
            return false;
        target = info->peer;
    }
    else
    {
        if (peer != 0 || !clientAccepted_ || !serverPeer_)
            return false;
        target = serverPeer_;
    }
    const enet_uint32 flags = channel == WireChannel::Reliable
        ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket *pkt = enet_packet_create(data, size, flags);
    if (enet_peer_send(target, (std::uint8_t)channel, pkt) != 0)
    {
        enet_packet_destroy(pkt);
        return false;
    }
    enet_host_flush(host_);
    return true;
}

bool EnetTransport::Broadcast(const void *data, std::size_t size, WireChannel channel)
{
    if (!host_ || !data)
        return false;
    if (!isHost_)
        return Send(0, data, size, channel);
    const enet_uint32 flags = channel == WireChannel::Reliable
        ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket *pkt = enet_packet_create(data, size, flags);
    enet_host_broadcast(host_, (std::uint8_t)channel, pkt);
    enet_host_flush(host_);
    return true;
}

bool EnetTransport::SendGoodbye()
{
    if (!host_ || isHost_ || !clientAccepted_ || !serverPeer_)
        return false;
    const std::uint8_t msg[1] = { 'X' };
    ENetPacket *pkt = enet_packet_create(msg, 1, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(serverPeer_, 0, pkt);
    enet_host_flush(host_);
    enet_peer_disconnect_later(serverPeer_, 0);
    return true;
}

void EnetTransport::DisconnectPeer(std::uint32_t peer, bool allowResume)
{
    if (!host_ || !isHost_)
        return;
    PeerInfo *info = FindPeer(peer);
    if (!info || !info->peer)
        return;
    if (!allowResume)
        info->graceful = true;
    enet_peer_disconnect(info->peer, 0);
}

std::uint32_t EnetTransport::RttMs(std::uint32_t peer) const
{
    if (!host_)
        return 0;
    if (isHost_)
    {
        const PeerInfo *info = FindPeer(peer);
        return info && info->peer ? info->peer->roundTripTime : 0;
    }
    return serverPeer_ ? serverPeer_->roundTripTime : 0;
}

} // namespace ZNDNet
