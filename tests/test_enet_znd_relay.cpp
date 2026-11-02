// ZNDEnet three-party relay: the windp session model is a star (host relays
// client traffic, exactly like ZNDSingle). With three or more players a
// client's broadcast must reach every other client, and a client's directed
// send must reach only its addressee, with the original sender identity.
// Also checks that a duplicate callsign is rejected instead of deadlocking
// the name-addressed lobby. Requires SDL2/SDL2_net like test_enet_znd.
#include "../src/netlib/enet_znd.h"
#include "../src/world/net_protocol.h"
#include <SDL.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ZNDNet;

static void Check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static bool WaitEvent(::ZNDNet::ZNDNet &node, bool system, std::uint32_t want,
                      unsigned milliseconds, Event *&out)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end)
    {
        Event *ev = system ? node.Events_PopSystem() : node.Events_Pop();
        if (ev)
        {
            if (ev->type == want) { out = ev; return true; }
            delete ev;
        }
        SDL_Delay(5);
    }
    return false;
}

static bool WaitData(::ZNDNet::ZNDNet &node, const char *payload, std::uint64_t from,
                     unsigned milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    const std::size_t size = std::strlen(payload);
    while (std::chrono::steady_clock::now() < end)
    {
        Event *ev = nullptr;
        if (!WaitEvent(node, false, EVENT_DATA, 50, ev))
            continue;
        EventData *d = (EventData *)ev;
        const bool match = d->size == size && d->from == from &&
                           std::memcmp(d->data, payload, size) == 0;
        delete ev;
        if (match)
            return true;
    }
    return false;
}

static bool NoData(::ZNDNet::ZNDNet &node, const char *payload, unsigned milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    const std::size_t size = std::strlen(payload);
    while (std::chrono::steady_clock::now() < end)
    {
        Event *ev = nullptr;
        if (!WaitEvent(node, false, EVENT_DATA, 50, ev))
            continue;
        EventData *d = (EventData *)ev;
        const bool match = d->size == size && std::memcmp(d->data, payload, size) == 0;
        delete ev;
        if (match)
            return false;
    }
    return true;
}

static void Join(ZNDEnet &client, const IPaddress &addr, const char *name)
{
    client.Start(name, addr);
    Event *ev = nullptr;
    Check(WaitEvent(client, true, EVENT_CONNECTED, 10000, ev), "client EVENT_CONNECTED missing");
    delete ev; ev = nullptr;
    Check(WaitEvent(client, true, EVENT_SESSION_JOIN, 10000, ev), "client EVENT_SESSION_JOIN missing");
    delete ev;
}

static void Run()
{
    Check(SDL_Init(0) == 0, "SDL_Init failed");
    Check(SDLNet_Init() == 0, "SDLNet_Init failed");
    const std::string protocol = World::NetProtocol::ServiceId;

    ZNDEnet host(protocol);
    host.Start("hosty", 0, "relay-session", 6);
    const std::uint16_t port = host.BoundPort();
    Check(port != 0, "host bound port unknown");
    IPaddress addr;
    Check(SDLNet_ResolveHost(&addr, "127.0.0.1", port) == 0, "resolve failed");

    ZNDEnet a(protocol), b(protocol);
    Join(a, addr, "alpha");
    Join(b, addr, "bravo");
    const std::uint64_t aId = a.GetMyID(), bId = b.GetMyID();
    Check(aId > 1 && bId > 1 && aId != bId, "client ids wrong");

    // Rosters converge on all three peers.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    UserInfoVect users;
    while (std::chrono::steady_clock::now() < deadline)
    {
        UserInfoVect ua, ub;
        if (host.GetUsers(users) && users.size() == 3 && a.GetUsers(ua) && ua.size() == 3 &&
            b.GetUsers(ub) && ub.size() == 3)
            break;
        SDL_Delay(10);
    }
    Check(users.size() == 3, "host roster did not reach three users");
    std::cout << "Three-party roster verified\n";

    // Client broadcast reaches the host and the other client, attributed to A.
    a.BroadcastData((void *)"from-alpha", 10, PKT_FLAG_GARANT);
    Check(WaitData(host, "from-alpha", aId, 10000), "host missed client broadcast");
    Check(WaitData(b, "from-alpha", aId, 10000), "other client missed relayed broadcast");
    Check(NoData(a, "from-alpha", 300), "broadcast echoed back to its sender");
    std::cout << "Client broadcast relayed to every other peer\n";

    // Directed client-to-client send reaches only its addressee.
    a.SendData(bId, (void *)"alpha-to-bravo", 14, PKT_FLAG_GARANT);
    Check(WaitData(b, "alpha-to-bravo", aId, 10000), "directed client send not relayed");
    Check(NoData(host, "alpha-to-bravo", 300), "directed client send leaked to host");
    std::cout << "Directed client send relayed to its addressee only\n";

    // Host broadcast and directed host send keep host identity.
    host.BroadcastData((void *)"from-host", 9, PKT_FLAG_GARANT);
    Check(WaitData(a, "from-host", 1, 10000) && WaitData(b, "from-host", 1, 10000),
          "host broadcast missing");
    host.SendData(bId, (void *)"host-to-bravo", 13, PKT_FLAG_GARANT);
    Check(WaitData(b, "host-to-bravo", 1, 10000), "host directed send missing");
    Check(NoData(a, "host-to-bravo", 300), "host directed send leaked");
    std::cout << "Host broadcast and directed send verified\n";

    // A duplicate callsign is refused up front (ZNDSingle ERR_CONN_NAME parity)
    // instead of joining and deadlocking name-addressed lobby messages.
    ZNDEnet dup(protocol);
    dup.Start("alpha", addr);
    Event *ev = nullptr;
    Check(WaitEvent(dup, true, EVENT_CONNERR, 10000, ev), "duplicate callsign was not refused");
    Check(ev->value == ERR_CONN_NAME, "duplicate callsign refused for the wrong reason");
    delete ev; ev = nullptr;
    Check(dup.GetStatus() != NetUser::STATUS_CONNECTED, "duplicate callsign connected");
    Check(host.GetUsers(users) && users.size() == 3, "duplicate callsign entered the roster");
    dup.Stop();
    std::cout << "Duplicate callsign rejected\n";

    // A hidden session (game started) takes no new players.
    host.ShowSession(false);
    ZNDEnet late(protocol);
    late.Start("charlie", addr);
    Check(WaitEvent(late, true, EVENT_CONNERR, 10000, ev), "closed session accepted a newcomer");
    Check(ev->value == ERR_CONN_FULL, "closed session refused for the wrong reason");
    delete ev; ev = nullptr;
    late.Stop();
    std::cout << "Closed session refuses newcomers\n";

    a.SendDisconnect();
    b.SendDisconnect();
    a.Stop();
    b.Stop();
    host.Stop();
    SDLNet_Quit();
    SDL_Quit();
}

int main(int, char *[])
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
