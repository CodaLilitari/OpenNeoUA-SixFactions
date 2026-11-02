#ifndef ZNDNET_ENET_TRANSPORT_H_INCLUDED
#define ZNDNET_ENET_TRANSPORT_H_INCLUDED

#include "transport.h"
#include <enet/enet.h>
#include <map>
#include <string>
#include <vector>

namespace ZNDNet
{

// ENet-backed INetTransport (design Phase 1). The ZNDNet handshake contract
// is preserved at this layer: service identity + protocol version are
// validated before Connected is emitted, CONNECTED issues a 128-bit resume
// token, and a silently dropped peer is orphaned for 60 s so a reconnect
// with the token restores its peer id. Application payloads are opaque.
class EnetTransport : public INetTransport
{
public:
    explicit EnetTransport(const std::string &servstring);
    ~EnetTransport();

    bool StartHost(const std::string &name, std::uint16_t port, std::size_t maxPeers);
    bool Connect(const std::string &name, const std::string &host, std::uint16_t port);
    void Stop();
    bool Send(std::uint32_t peer, const void *data, std::size_t size, WireChannel channel);
    bool Broadcast(const void *data, std::size_t size, WireChannel channel);
    bool Poll(TransportEvent &out, std::uint32_t timeoutMs);
    std::uint32_t RttMs(std::uint32_t peer) const;
    std::uint16_t BoundPort() const;

    std::uint32_t MyId() const { return myId_; }
    // Client: graceful leave ('X' control) so the host frees the slot
    // immediately instead of orphaning it for resume.
    bool SendGoodbye();
    // Host: drop one peer; allowResume=false also suppresses the orphan.
    void DisconnectPeer(std::uint32_t peer, bool allowResume);

private:
    struct PeerInfo
    {
        ENetPeer *peer = nullptr;
        std::uint32_t id = 0;
        std::string name;
        bool accepted = false;
        bool graceful = false; // explicit leave/kick: no resume orphan
        unsigned char token[16] = {};
    };
    struct Orphan
    {
        unsigned char token[16] = {};
        std::uint32_t id = 0;
        std::string name;
        std::uint64_t expiryMs = 0;
    };

    bool HandleHandshake(PeerInfo &info, const std::uint8_t *data, std::size_t len,
                         TransportEvent &out);
    void GenerateToken(unsigned char out[16], std::uint32_t id);
    PeerInfo *FindPeer(std::uint32_t id);
    const PeerInfo *FindPeer(std::uint32_t id) const;

    std::string servString_;
    std::string name_;
    ENetHost *host_ = nullptr;
    bool isHost_ = false;
    std::map<ENetPeer *, PeerInfo> peers_;
    std::vector<Orphan> orphans_;
    std::uint32_t nextId_ = 1;
    std::uint64_t tokenCounter_ = 0;

    ENetPeer *serverPeer_ = nullptr;
    bool clientAccepted_ = false;
    std::uint32_t myId_ = 0;
    unsigned char myToken_[16] = {};
};

}

#endif
