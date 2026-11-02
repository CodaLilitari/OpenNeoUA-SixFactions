// ENet transport loopback: handshake accept/reject, two-channel data,
// broadcast, and resume-token identity restoration. No SDL required.
// Both endpoints are serviced cooperatively (ENet has no background thread).
#include "../src/netlib/enet_transport.h"
#include "../src/world/net_protocol.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace ZNDNet;

static void Check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// Services both transports until `watch` yields the wanted event type.
static bool Drive(EnetTransport &a, EnetTransport &b, EnetTransport &watch,
                  TransportEvent::Type want, unsigned milliseconds,
                  TransportEvent &out)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end)
    {
        TransportEvent ev;
        if (a.Poll(ev, 5) && &a == &watch && ev.type == want) { out = ev; return true; }
        if (b.Poll(ev, 5) && &b == &watch && ev.type == want) { out = ev; return true; }
    }
    return false;
}

// Services both transports until `watch` yields a Data event.
static bool DriveData(EnetTransport &a, EnetTransport &b, EnetTransport &watch,
                      unsigned milliseconds, TransportEvent &out)
{
    return Drive(a, b, watch, TransportEvent::Type::Data, milliseconds, out);
}

static std::string Bytes(const TransportEvent &ev)
{
    return std::string((const char *)ev.data.data(), ev.data.size());
}

static void Run()
{
    const std::string protocol = World::NetProtocol::ServiceId;
    EnetTransport host(protocol);
    Check(host.StartHost("host", 0, 6), "host start failed");
    const std::uint16_t port = host.BoundPort();
    Check(port != 0, "host bound port unknown");

    // Wrong service identity is rejected before Connected.
    {
        EnetTransport wrong("UA:SOURCE TEST NETWORK");
        Check(wrong.Connect("old-version", "127.0.0.1", port), "wrong connect failed");
        TransportEvent ev;
        Check(Drive(host, wrong, wrong, TransportEvent::Type::Disconnected, 8000, ev),
              "wrong protocol was not rejected");
        wrong.Stop();
    }
    std::cout << "Wrong service identity rejected\n";

    EnetTransport client(protocol);
    Check(client.Connect("player-2", "127.0.0.1", port), "client connect failed");
    TransportEvent ev;
    Check(Drive(host, client, host, TransportEvent::Type::Connected, 8000, ev),
          "host did not accept client");
    const std::uint32_t peerId = ev.peer;
    Check(peerId != 0 && ev.name == "player-2", "host peer identity wrong");
    Check(Drive(host, client, client, TransportEvent::Type::Connected, 8000, ev),
          "client did not complete handshake");
    Check(client.MyId() == peerId, "client/host peer id mismatch");
    std::cout << "Handshake accepted with matching peer id\n";

    // Reliable client -> host, unreliable host -> client, host broadcast.
    Check(client.Send(0, "hello", 5, WireChannel::Reliable), "client send failed");
    Check(DriveData(host, client, host, 8000, ev) &&
          ev.peer == peerId && ev.channel == 0 && Bytes(ev) == "hello",
          "reliable data not received by host");
    Check(host.Send(peerId, "world", 5, WireChannel::Unreliable), "host send failed");
    Check(DriveData(host, client, client, 8000, ev) &&
          ev.channel == 1 && Bytes(ev) == "world",
          "unreliable data not received by client");
    Check(host.Broadcast("all", 3, WireChannel::Reliable), "broadcast failed");
    Check(DriveData(host, client, client, 8000, ev) && Bytes(ev) == "all",
          "broadcast not received by client");
    std::cout << "Two-channel data and broadcast verified\n";

    // Resume: drop silently (Stop sends no application disconnect because
    // ENet disconnect_now is a transport teardown the host sees as a drop),
    // then reconnect with the same object so the stored token is presented.
    client.Stop();
    Check(Drive(host, client, host, TransportEvent::Type::Disconnected, 8000, ev) &&
          ev.peer == peerId, "host did not notice the drop");
    Check(client.Connect("player-2", "127.0.0.1", port), "resume connect failed");
    Check(Drive(host, client, host, TransportEvent::Type::Connected, 8000, ev),
          "host did not accept the resume");
    Check(ev.peer == peerId, "resume restored a different peer id");
    Check(Drive(host, client, client, TransportEvent::Type::Connected, 8000, ev),
          "client did not complete resume handshake");
    Check(client.MyId() == peerId, "resumed client id mismatch");
    std::cout << "Resume restored the original peer id\n";

    client.Stop();
    host.Stop();
}

int main()
{
    int result = 0;
    try { Run(); }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    return result;
}
