#ifndef ZNDNET_ENET_TRANSPORT_H_INCLUDED
#define ZNDNET_ENET_TRANSPORT_H_INCLUDED

#include "transport.h"
#include "transport_state.h"
#include "world/net_protocol.h"
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
//
// Not thread-safe (ENet hosts are not): a caller that services Poll on one
// thread and sends from another must serialize every call (see ZNDEnet).
class EnetTransport : public INetTransport
{
public:
    // Handshake refusal codes carried by 'R' and TransportEvent::reason.
    enum Refusal : std::uint8_t
    {
        RefuseService = 1,  // different fork/service identity
        RefuseVersion = 2,  // wire protocol or ENet framing version
        RefuseName = 3,     // callsign already present in the session
        RefuseResume = 5,
        RefuseClosed = 6,
        RefuseRandom = 7,
        RefuseFull = 4      // no free client slot, or session closed
    };
    // Bumped whenever the ENet-only framing changes (it is independent of
    // the ZNDNet wire, so ZNDNet compatibility is unaffected).
    enum : std::uint8_t { FramingVersion = 3 };

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
    // Whether connected peers have no queued outgoing commands or outstanding
    // reliable acknowledgments. The caller holds the adapter lock. This does
    // not prove remote application consumption or delivery of discarded data.
    bool ReliableIdle() const
    {
        if (!host_)
            return true;
        const auto idle = [](const ENetPeer *peer)
        {
            return !peer || peer->state != ENET_PEER_STATE_CONNECTED ||
                   (enet_list_empty(&peer->outgoingCommands) &&
                    enet_list_empty(&peer->outgoingSendReliableCommands) &&
                    enet_list_empty(&peer->sentReliableCommands));
        };
        if (!idle(serverPeer_))
            return false;
        for (const auto &kv : peers_)
            if (!idle(kv.first))
                return false;
        return true;
    }

    std::uint32_t MyId() const { return myId_; }
    // Host: broadcast to accepted peers except one (0 = nobody excluded).
    bool BroadcastExcept(std::uint32_t exceptPeer, const void *data, std::size_t size,
                         WireChannel channel);
    // Host: refuse new (non-resume) handshakes while false.
    void SetAcceptingNew(bool accept) { acceptingNew_ = accept; }
    // Client: graceful leave ('X' control) so the host frees the slot
    // immediately instead of orphaning it for resume.
    bool SendGoodbye();
    void CloseSession();
    bool GetResumeToken(World::NetProtocol::ResumeToken &out) const;
    bool SetResumeToken(const World::NetProtocol::ResumeToken &in);
    void ClearResumeToken();
    bool SetResumeOnly(bool only);
    bool SetResumeGraceMs(std::uint32_t ms);
    std::uint32_t GetResumeGraceMs() const { return graceMs_; }
    std::uint64_t SessionId() const { return session_; }
    std::uint64_t Attempt() const { return attempt_; }
    std::uint32_t Generation() const { return generation_; }
    bool Resumed() const { return resumed_; }
    std::uint8_t MySlot() const { return slot_; }
    void RetryHandshake();
    bool PeerBinding(std::uint32_t id,std::uint64_t &attempt,std::uint32_t &generation)const;

    // Host: drop one peer; allowResume=false also suppresses the orphan.
    void DisconnectPeer(std::uint32_t peer, bool allowResume);

private:
    struct PeerInfo
    {
        ENetPeer *peer = nullptr;
        std::uint32_t id = 0;
        std::string name;
        bool accepted = false;
        std::uint8_t slot = 0;
        std::uint32_t generation = 0, terminal = 0;
        std::uint64_t attempt = 0, deadline = 0;
        bool resumed = false;
        bool graceful = false; // explicit leave/kick: no resume orphan
        unsigned char token[16] = {};
    };
    struct Orphan
    {
        unsigned char token[16] = {};
        std::uint32_t id = 0;
        std::string name;
        std::uint64_t expiryMs = 0;
        std::uint8_t slot = 0;
        std::uint8_t terminalReason = LeaveExpired;
    };

    bool HandleHandshake(PeerInfo &info, const std::uint8_t *data, std::size_t len,
                         TransportEvent &out);
    void Accept(PeerInfo &info);
    void Handshake();
    bool Maintenance(TransportEvent &out);
    bool GenerateToken(unsigned char out[16], std::uint32_t id);
    PeerInfo *FindPeer(std::uint32_t id);
    const PeerInfo *FindPeer(std::uint32_t id) const;

    std::string servString_;
    std::string name_;
    ENetHost *host_ = nullptr;
    bool isHost_ = false;
    std::map<ENetPeer *, PeerInfo> peers_;
    std::vector<Orphan> orphans_;
    std::deque<TransportEvent> pendingEvents_;
    std::uint32_t nextId_ = 1;
    std::uint64_t tokenCounter_ = 0;
    std::size_t maxClients_ = 0;
    bool acceptingNew_ = true;

    ENetPeer *serverPeer_ = nullptr;
    bool clientAccepted_ = false;
    std::uint32_t myId_ = 0;
    unsigned char myToken_[16] = {};
    bool resumeOnly_ = false, resumed_ = false, refused_ = false;
    std::uint8_t slot_ = 0;
    std::uint32_t graceMs_ = 60000, generation_ = 0, nextGeneration_ = 0, terminal_ = 0;
    HandshakeBudget handshakeBudget_;
    std::uint64_t session_ = 0, attempt_ = 0, deadline_ = 0, retryNext_ = 0;

};

}

#endif
