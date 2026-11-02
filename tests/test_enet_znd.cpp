// ZNDEnet adapter loopback: windp-facing session/user/event semantics over
// EnetTransport. Requires SDL2/SDL2_net (ZNDNet base) like test_net_transport.
#include "../src/netlib/enet_znd.h"
#include "../src/world/net_protocol.h"
#include <SDL.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

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

static void Run()
{
    Check(SDL_Init(0) == 0, "SDL_Init failed");
    Check(SDLNet_Init() == 0, "SDLNet_Init failed");
    const std::string protocol = World::NetProtocol::ServiceId;

    ZNDEnet host(protocol);
    host.Start("hosty", 0, "enet-test-session", 6);
    const std::uint16_t port = host.BoundPort();
    Check(port != 0, "host bound port unknown");
    Check(host.GetMyID() == 1 && host.GetMySesID() == 1, "host identity wrong");

    IPaddress addr;
    Check(SDLNet_ResolveHost(&addr, "127.0.0.1", port) == 0, "resolve failed");
    ZNDEnet client(protocol);
    client.Start("player-2", addr);

    Event *ev = nullptr;
    Check(WaitEvent(client, true, EVENT_CONNECTED, 10000, ev),
          "client EVENT_CONNECTED missing");
    EventNameID *conn = (EventNameID *)ev;
    Check(conn->value == 0 && conn->id == 2 && conn->name == "player-2",
          "client EVENT_CONNECTED payload wrong");
    delete ev; ev = nullptr;
    Check(WaitEvent(client, true, EVENT_SESSION_JOIN, 10000, ev),
          "client EVENT_SESSION_JOIN missing");
    EventNameID *join = (EventNameID *)ev;
    Check(join->id == 1 && join->name == "enet-test-session",
          "client EVENT_SESSION_JOIN payload wrong");
    delete ev; ev = nullptr;
    Check(client.GetMyID() == 2 && client.GetMySesID() == 1 &&
          client.GetStatus() == NetUser::STATUS_CONNECTED, "client state wrong");

    Check(WaitEvent(host, false, EVENT_USER_ADD, 10000, ev),
          "host EVENT_USER_ADD missing");
    EventNameID *add = (EventNameID *)ev;
    Check(add->id == 2 && add->name == "player-2", "host EVENT_USER_ADD payload wrong");
    delete ev; ev = nullptr;

    UserInfoVect users;
    Check(host.GetUsers(users) && users.size() == 2 &&
          users[0].ID == 1 && users[0].lead && users[1].ID == 2,
          "host user list wrong");
    Check(client.GetUsers(users) && users.size() == 2 && users[1].name == "player-2",
          "client roster wrong");
    SessionInfoVect sessions;
    Check(client.GetSessions(sessions) && sessions.size() == 1 &&
          sessions[0].players == 2 && sessions[0].name == "enet-test-session",
          "client session list wrong");
    std::cout << "Adapter session, roster, and join events verified\n";

    // Game data both ways through the ZNDNet event surface.
    const char ping[] = "ping";
    client.SendData(1, (void *)ping, 4, PKT_FLAG_GARANT);
    Check(WaitEvent(host, false, EVENT_DATA, 10000, ev), "host EVENT_DATA missing");
    EventData *hd = (EventData *)ev;
    Check(hd->from == 2 && hd->size == 4 && std::memcmp(hd->data, "ping", 4) == 0,
          "host EVENT_DATA payload wrong");
    delete ev; ev = nullptr;
    const char pong[] = "pong!";
    host.BroadcastData((void *)pong, 5, PKT_FLAG_GARANT);
    Check(WaitEvent(client, false, EVENT_DATA, 10000, ev), "client EVENT_DATA missing");
    EventData *cd = (EventData *)ev;
    Check(cd->from == 1 && cd->size == 5 && std::memcmp(cd->data, "pong!", 5) == 0,
          "client EVENT_DATA payload wrong");
    delete ev; ev = nullptr;
    std::cout << "Adapter data events verified\n";

    // Graceful leave: host sees USER_LEAVE and the roster shrinks.
    client.SendDisconnect();
    Check(WaitEvent(host, false, EVENT_USER_LEAVE, 10000, ev),
          "host EVENT_USER_LEAVE missing");
    delete ev; ev = nullptr;
    Check(host.GetUsers(users) && users.size() == 1, "host roster did not shrink");
    std::cout << "Adapter graceful leave verified\n";

    client.Stop();
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
