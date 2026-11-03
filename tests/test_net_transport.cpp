// Real loopback UDP endpoints using the production transport. This does not
// replace a six-machine game/lobby/rendering test.
#include <SDL2/SDL.h>
#include "../src/netlib/zndNet.h"
#include "../src/world/net_factions.h"
#include "../src/world/net_protocol.h"
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace ZNDNet;

class TestHost : public ZNDSingle
{
public:
    using ZNDSingle::ZNDSingle;
    uint16_t Port()
    {
        IPaddress *bound = sock ? SDLNet_UDP_GetPeerAddress(sock, -1) : nullptr;
        if (!bound) throw std::runtime_error("host UDP bind failed");
        return SDLNet_Read16(&bound->port);
    }
};

template<class Predicate> static bool Until(Predicate done, unsigned milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do { if (done()) return true; SDL_Delay(10); }
    while (std::chrono::steady_clock::now() < end);
    return false;
}

static size_t Count(ZNDNet::ZNDNet &node)
{
    UserInfoVect users;
    return node.GetUsers(users) ? users.size() : 0;
}

static void Check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static void Run()
{
    constexpr int count = World::NetFactions::MaxPlayers;
    const std::string protocol = World::NetProtocol::ServiceId;
    TestHost host(protocol);
    host.Start("host", 0, "six-faction transport test", count);
    IPaddress address;
    Check(SDLNet_ResolveHost(&address, "127.0.0.1", host.Port()) == 0, "loopback resolve failed");

    // Reject a different protocol while there is space, so the capacity gate
    // cannot accidentally make this version test pass.
    {
        ZNDClient wrong("UA:SOURCE TEST NETWORK"); // Exact upstream production identity.
        wrong.Start("old-version", address);
        Check(!Until([&] { return wrong.GetMyID() != 0; }, 11000),
              "wrong protocol unexpectedly connected");
        Check(Count(host) == 1, "wrong protocol consumed a host slot");
        wrong.Stop();
    }

    std::vector<std::unique_ptr<ZNDClient>> clients;
    std::array<ZNDNet::ZNDNet *, count> peers;
    peers[0] = &host;
    for (int i = 1; i < count; ++i)
    {
        clients.emplace_back(new ZNDClient(protocol));
        clients.back()->Start("player-" + std::to_string(i + 1), address);
        peers[i] = clients.back().get();
    }
    Check(Until([&] {
        for (auto *peer : peers) if (Count(*peer) != count) return false;
        return true;
    }, 15000), "six endpoints did not converge on the six-player roster");
    std::cout << "All six endpoints see the six-player roster\n";

    std::array<uint64_t, count> ids;
    for (int i = 0; i < count; ++i) ids[i] = peers[i]->GetMyID();
    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j)
            Check(ids[i] != 0 && ids[i] != ids[j], "peer identities are not unique");

    // Exceed the transport MTU to exercise reliable multipart reassembly.
    std::array<std::array<bool, count>, count> received = {};
    for (int sender = 0; sender < count; ++sender)
    {
        received[sender][sender] = true;
        std::vector<uint8_t> payload(2048);
        for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i + sender);
        payload[0] = static_cast<uint8_t>(sender);
        peers[sender]->BroadcastData(payload.data(), payload.size(), PKT_FLAG_GARANT);
    }
    Check(Until([&] {
        for (int observer = 0; observer < count; ++observer)
        {
            while (Event *event = peers[observer]->Events_Pop())
            {
                std::unique_ptr<Event> owned(event);
                if (event->type != EVENT_DATA) continue;
                auto *data = static_cast<EventData *>(event);
                Check(data->size == 2048, "broadcast payload length changed");
                const int sender = data->data[0];
                Check(sender < count && data->from == ids[sender], "broadcast sender identity changed");
                for (size_t i = 1; i < data->size; ++i)
                    Check(data->data[i] == static_cast<uint8_t>(i + sender), "broadcast bytes changed");
                received[observer][sender] = true;
            }
        }
        for (const auto &row : received) for (bool value : row) if (!value) return false;
        return true;
    }, 10000), "not all 30 remote deliveries arrived");
    std::cout << "All 30 reliable multipart deliveries verified byte for byte\n";

    // A client accepts datagrams only from its host, which relays every
    // peer's traffic, so a client keeps no inbound budget of its own (the
    // host still limits each client's address). 900 packets from the host at
    // about 500/s exceed one source's budget (400 burst, 200/s); a bucket for
    // the host address passed about 760 of them.
    {
        ZNDClient *listener = clients.front().get();
        const uint64_t listenerId = listener->GetMyID();
        constexpr int burst = 900;
        std::vector<bool> seen(burst, false);
        int arrived = 0;
        const auto drain = [&] {
            while (Event *event = listener->Events_Pop())
            {
                std::unique_ptr<Event> owned(event);
                if (event->type != EVENT_DATA) continue;
                auto *data = static_cast<EventData *>(event);
                if (data->size != 4 || data->data[0] != 0xB5) continue;
                const int index = data->data[1] | (data->data[2] << 8);
                if (index < burst && !seen[index]) { seen[index] = true; ++arrived; }
            }
        };
        drain();
        for (int i = 0; i < burst; ++i)
        {
            uint8_t payload[4] = {0xB5, static_cast<uint8_t>(i & 0xFF), static_cast<uint8_t>(i >> 8), 0};
            host.SendData(listenerId, payload, sizeof(payload), 0);
            if (i % 2 == 1) SDL_Delay(4);
            drain();
        }
        Check(Until([&] { drain(); return arrived == burst; }, 8000),
              "a client throttled the relayed traffic from its host");
        std::cout << "A client accepted a 900-packet stream from its host at about 500/s\n";
    }

    {
        ZNDClient seventh(protocol);
        seventh.Start("seventh", address);
        Check(!Until([&] { return seventh.GetMyID() != 0; }, 11000),
              "seventh endpoint bypassed the six-player cap");
        Check(Count(host) == count, "capacity test altered the existing roster");
        seventh.Stop();
    }
    clients.back()->SendDisconnect();
    Check(Until([&] { return Count(host) == count - 1; }, 5000), "disconnect did not free a slot");
    clients.back()->Stop();
    clients.back().reset(new ZNDClient(protocol));
    clients.back()->Start("replacement", address);
    peers[count - 1] = clients.back().get();
    Check(Until([&] {
        for (auto *peer : peers) if (Count(*peer) != count) return false;
        return true;
    }, 10000), "replacement sixth endpoint did not join");
    std::cout << "Protocol isolation, capacity limit, disconnect, and sixth-slot reuse passed\n";

    // Resume: silently drop one client (Stop sends no DISCONNECT) so the host
    // orphans it after TIMEOUT_USER, then reconnect the same client object.
    // The resume token from CONNECTED must restore the original identity.
    {
        auto *returning = clients.front().get();
        const uint64_t originalId = returning->GetMyID();
        Check(originalId != 0, "resume candidate has no identity");
        returning->Stop();
        Check(Until([&] { return Count(host) == count - 1; }, 25000),
              "silent drop did not vacate the session slot");
        returning->Start("player-2", address);
        Check(Until([&] { return returning->GetStatus() == NetUser::STATUS_CONNECTED
                              && returning->GetMyID() == originalId; }, 15000),
              "resume did not restore the original identity");
        Check(Until([&] { return Count(host) == count; }, 10000),
              "resumed endpoint did not rejoin the roster");
        std::cout << "Resume after silent drop restored the original identity\n";
    }
    for (auto &client : clients) client->Stop();
    host.Stop();
}

int main(int, char **)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) != 0 || SDLNet_Init() != 0) return 1;
    int result = 0;
    try { Run(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    SDLNet_Quit();
    SDL_Quit();
    return result;
}
