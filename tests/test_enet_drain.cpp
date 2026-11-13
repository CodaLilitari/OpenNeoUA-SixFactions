// ZNDEnet::WaitSendIdle over real loopback UDP: a guaranteed broadcast sent
// just before the client leaves reaches the host application only when the
// client first waits for its acknowledgment. Without the wait the host's
// ENet can read the data and the disconnect in one receive pass and drop the
// data undelivered. The game's quit announcement (UAMSG_EXIT) depends on it.
#include "../src/netlib/enet_znd.h"
#include "../src/world/net_protocol.h"
#include <SDL.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace ZNDNet;

static void Check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Predicate> static bool Until(Predicate done, unsigned milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do { if (done()) return true; SDL_Delay(5); }
    while (std::chrono::steady_clock::now() < end);
    return false;
}

struct Seen
{
    bool data = false;
    bool leave = false;
    bool dataBeforeLeave = false;
};

// One quit as the game does it: guaranteed broadcast, then goodbye and stop.
static Seen QuitOnce(bool drain, int cycle)
{
    const std::string protocol = World::NetProtocol::ServiceId;
    ZNDEnet host(protocol);
    host.Start("host", 0, "enet-drain", 6);
    const std::uint16_t port = host.BoundPort();
    Check(port != 0, "host bound port unknown");
    IPaddress addr;
    Check(SDLNet_ResolveHost(&addr, "127.0.0.1", port) == 0, "resolve failed");

    ZNDEnet client(protocol);
    client.Start("leaver" + std::to_string(cycle), addr);
    bool joined = false;
    Check(Until([&] {
        while (Event *event = client.Events_Pop())
        {
            std::unique_ptr<Event> owned(event);
            Check(event->type != EVENT_CONNERR && event->type != EVENT_DISCONNECT, "connection failed");
            if (event->type == EVENT_SESSION_JOIN) joined = true;
        }
        return joined;
    }, 10000), "client did not join");
    Check(Until([&] {
        bool added = false;
        while (Event *event = host.Events_Pop())
        {
            std::unique_ptr<Event> owned(event);
            if (event->type == EVENT_USER_ADD) added = true;
        }
        return added;
    }, 10000), "host did not see the client join");

    char text[] = "quit-announcement";
    client.BroadcastData(text, sizeof(text) - 1, PKT_FLAG_GARANT);
    if (drain)
        Check(client.WaitSendIdle(3000), "the announcement was not acknowledged in 3 s");
    client.SendDisconnect();
    client.Stop();

    Seen seen;
    Check(Until([&] {
        while (Event *event = host.Events_Pop())
        {
            std::unique_ptr<Event> owned(event);
            if (event->type == EVENT_DATA)
            {
                auto *data = static_cast<EventData *>(event);
                Check(data->size == std::strlen(text) && std::memcmp(data->data, text, data->size) == 0,
                      "unexpected payload");
                seen.data = true;
                seen.dataBeforeLeave = !seen.leave;
            }
            else if (event->type == EVENT_USER_LEAVE)
                seen.leave = true;
        }
        return seen.leave;
    }, 10000), "host did not see the client leave");
    host.Stop();
    return seen;
}

static void Delivered()
{
    const int cycles = 12;
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        const Seen seen = QuitOnce(true, cycle);
        Check(seen.data, "an acknowledged quit announcement did not reach the host");
        Check(seen.dataBeforeLeave, "the announcement arrived after the leave");
    }
    std::cout << cycles << " quits with WaitSendIdle: the announcement reached the host before the leave\n";
}

// Not a test: how often the announcement is lost without the wait. The
// outcome depends on whether the host reads both datagrams in one pass.
static void Measure()
{
    const int cycles = 20;
    int delivered = 0;
    for (int cycle = 0; cycle < cycles; ++cycle)
        if (QuitOnce(false, cycle).data)
            ++delivered;
    std::cout << "Without the wait: " << delivered << " of " << cycles << " announcements reached the host\n";
}

static void Idle()
{
    const std::string protocol = World::NetProtocol::ServiceId;
    ZNDEnet lone(protocol);
    Check(lone.WaitSendIdle(0), "an endpoint that was never started is not idle");
    ZNDEnet host(protocol);
    host.Start("host", 0, "enet-drain", 6);
    const auto start = std::chrono::steady_clock::now();
    Check(host.WaitSendIdle(2000), "a host without peers was reported busy");
    Check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1000),
          "an idle host took the whole wait");
    host.Stop();
    std::cout << "Idle ENet endpoints return at once\n";
}

int main(int argc, char **argv)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) || SDLNet_Init()) return 1;
    int result = 0;
    try
    {
        Check(argc == 2, "select delivered/idle/measure");
        const std::string mode = argv[1];
        if (mode == "delivered") Delivered();
        else if (mode == "idle") Idle();
        else if (mode == "measure") Measure();
        else Check(false, "unknown test mode");
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    SDLNet_Quit(); SDL_Quit();
    return result;
}
