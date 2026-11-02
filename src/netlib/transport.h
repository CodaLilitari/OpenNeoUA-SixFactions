#ifndef ZNDNET_TRANSPORT_H_INCLUDED
#define ZNDNET_TRANSPORT_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ZNDNet
{

// Phase 0/1 transport abstraction (design: INetTransport). SDL-free so the
// transport contract is testable without the engine. Channel 0 is
// reliable-ordered (lobby, snapshots, events), channel 1 is
// unreliable-sequenced (state streams).
enum class WireChannel : std::uint8_t { Reliable = 0, Unreliable = 1 };

struct TransportEvent
{
    enum class Type : std::uint8_t { Connected, Disconnected, Data };
    Type type = Type::Data;
    std::uint32_t peer = 0; // host side: peer id; client side: 0 = host
    std::uint8_t channel = 0;
    std::uint8_t reason = 0;          // Disconnected before acceptance: refusal code
    std::string name;                 // Connected on the host: peer name
    std::vector<std::uint8_t> data;   // Data only
};

class INetTransport
{
public:
    virtual ~INetTransport() {}
    virtual bool StartHost(const std::string &name, std::uint16_t port, std::size_t maxPeers) = 0;
    virtual bool Connect(const std::string &name, const std::string &host, std::uint16_t port) = 0;
    virtual void Stop() = 0;
    virtual bool Send(std::uint32_t peer, const void *data, std::size_t size, WireChannel channel) = 0;
    virtual bool Broadcast(const void *data, std::size_t size, WireChannel channel) = 0;
    // Services the transport and dequeues at most one external event.
    virtual bool Poll(TransportEvent &out, std::uint32_t timeoutMs) = 0;
    virtual std::uint32_t RttMs(std::uint32_t peer) const = 0;
    virtual std::uint16_t BoundPort() const { return 0; }
};

}

#endif
