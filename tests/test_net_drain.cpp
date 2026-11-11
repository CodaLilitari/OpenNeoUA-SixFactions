// ZNDNet::WaitSendIdle over real loopback UDP: guaranteed data queued just
// before a disconnect reaches the host only when the endpoint is given time
// to send it. The game's quit announcement (UAMSG_EXIT) depends on this.
#include "../src/netlib/zndNet.h"
#include "../src/world/net_protocol.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace ZNDNet;

static void Check(bool value, const char *why)
{
    if (!value) throw std::runtime_error(why);
}

template<class Predicate> static bool Until(Predicate done, unsigned milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do { if (done()) return true; SDL_Delay(5); }
    while (std::chrono::steady_clock::now() < end);
    return false;
}

static unsigned ElapsedMs(std::chrono::steady_clock::time_point since)
{
    return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - since).count());
}

class TestHost : public ZNDSingle
{
public:
    using ZNDSingle::ZNDSingle;
    // Keeps the host from sending anything, acknowledgments included.
    void HoldSender() { Check(SDL_LockMutex(sendModifyMutex) == 0, "cannot hold the host's sender"); }
    void ReleaseSender() { SDL_UnlockMutex(sendModifyMutex); }
    IPaddress Address()
    {
        IPaddress *bound = SDLNet_UDP_GetPeerAddress(sock, -1);
        Check(bound != nullptr, "host has no bound socket");
        IPaddress result;
        Check(SDLNet_ResolveHost(&result, "127.0.0.1", SDLNet_Read16(&bound->port)) == 0,
              "resolve failed");
        return result;
    }
};

class TestClient : public ZNDClient
{
public:
    using ZNDClient::ZNDClient;
    // Keeps the send worker from starting a pass, so that what the test
    // queues next is still queued when the test acts on it.
    void HoldSender() { Check(SDL_LockMutex(sendModifyMutex) == 0, "cannot hold the sender"); }
    void ReleaseSender() { SDL_UnlockMutex(sendModifyMutex); }
};

static void Connect(TestClient &client, TestHost &host, const std::string &name)
{
    client.Start(name, host.Address());
    bool connected = false, listed = false;
    Check(Until([&] {
        while (Event *event = client.Events_Pop())
        {
            std::unique_ptr<Event> owned(event);
            Check(event->type != EVENT_CONNERR && event->type != EVENT_DISCONNECT, "connection failed");
            if (event->type == EVENT_CONNECTED) connected = true;
            if (event->type == EVENT_USER_LIST) listed = true;
        }
        return connected && listed;
    }, 5000), "client did not connect");
    // Leave nothing of the join behind in the host's event queue.
    Check(Until([&] {
        bool added = false;
        while (Event *event = host.Events_Pop())
        {
            std::unique_ptr<Event> owned(event);
            if (event->type == EVENT_USER_ADD) added = true;
        }
        return added;
    }, 5000), "host did not see the client join");
}

struct Seen
{
    bool data = false;
    bool leave = false;
    bool dataBeforeLeave = false;
};

// Reads the host's events until the client's leave arrives.
static Seen HostUntilLeave(TestHost &host, const char *text)
{
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
    }, 5000), "host did not see the client leave");
    // Anything still in flight after the leave.
    Until([&] {
        while (Event *event = host.Events_Pop())
        {
            std::unique_ptr<Event> owned(event);
            if (event->type == EVENT_DATA) seen.data = true;
        }
        return false;
    }, 300);
    return seen;
}

// The quit as the game does it: guaranteed broadcast, then disconnect and
// stop. `drain` is the difference under test.
static Seen QuitOnce(bool drain, int cycle)
{
    TestHost host(World::NetProtocol::ServiceId);
    host.Start("host", 0, "drain", 6);
    TestClient client(World::NetProtocol::ServiceId);
    Connect(client, host, "leaver" + std::to_string(cycle));

    char text[] = "quit-announcement";
    client.HoldSender();
    client.BroadcastData(text, sizeof(text) - 1, PKT_FLAG_GARANT);
    if (drain)
    {
        client.ReleaseSender();
        Check(client.WaitSendIdle(3000), "the announcement was not sent and acknowledged in 3 s");
        client.SendDisconnect();
    }
    else
    {
        // The disconnect arrives before the send worker's next pass, as it
        // usually does in the game's level end.
        client.SendDisconnect();
        client.ReleaseSender();
    }
    client.Stop();

    const Seen seen = HostUntilLeave(host, text);
    host.Stop();
    return seen;
}

static void Delivered()
{
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        const Seen seen = QuitOnce(true, cycle);
        Check(seen.data, "a drained quit announcement did not reach the host");
        Check(seen.dataBeforeLeave, "the announcement arrived after the leave");
    }
    std::cout << "Five quits with WaitSendIdle: the announcement reached the host before the leave\n";
}

static void DroppedWithoutDrain()
{
    // Why the wait is needed: the disconnect clears the send queue.
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        const Seen seen = QuitOnce(false, cycle);
        Check(seen.leave, "the leave did not arrive");
        Check(!seen.data, "a disconnect no longer drops queued data: WaitSendIdle's contract changed");
    }
    std::cout << "Three quits without the wait: the host saw the leave and no announcement\n";
}

static void Idle()
{
    TestHost host(World::NetProtocol::ServiceId);
    host.Start("host", 0, "drain", 6);
    TestClient client(World::NetProtocol::ServiceId);
    Connect(client, host, "idle");
    // Let the join's own guaranteed traffic settle first.
    Check(client.WaitSendIdle(3000), "a connected client never became idle");
    const auto start = std::chrono::steady_clock::now();
    Check(client.WaitSendIdle(2000), "an idle client was reported busy");
    Check(ElapsedMs(start) < 1000, "an idle client took the whole wait");
    Check(client.WaitSendIdle(0), "a zero wait misreports an idle client");
    client.Stop();
    host.Stop();
    std::cout << "An idle client returns at once\n";
}

static void Bounded()
{
    // A host that receives but cannot answer: while its send worker is held,
    // the client's guaranteed packet is sent and stays unacknowledged.
    TestHost host(World::NetProtocol::ServiceId);
    host.Start("host", 0, "drain", 6);
    TestClient client(World::NetProtocol::ServiceId);
    Connect(client, host, "unanswered");
    Check(client.WaitSendIdle(3000), "a connected client never became idle");

    char text[] = "not-yet-acknowledged";
    host.HoldSender();
    client.BroadcastData(text, sizeof(text) - 1, PKT_FLAG_GARANT);
    const auto start = std::chrono::steady_clock::now();
    const bool idle = client.WaitSendIdle(200);
    const unsigned elapsed = ElapsedMs(start);
    const bool idleAtOnce = client.WaitSendIdle(0);
    host.ReleaseSender();

    Check(!idle, "unacknowledged data was reported as delivered");
    // The two clocks differ in resolution: allow for it on the low side.
    Check(elapsed >= 150, "the wait gave up well before its limit");
    Check(elapsed < 2000, "the wait did not respect its limit");
    Check(!idleAtOnce, "a zero wait misreports a busy client");
    // The same wait completes once the host answers.
    Check(client.WaitSendIdle(3000), "the wait did not end after the acknowledgment");
    client.Stop();
    host.Stop();
    std::cout << "An unacknowledged packet ends the wait at its limit (" << elapsed
              << " ms for 200), and the acknowledgment ends it early\n";
}

int main(int argc, char **argv)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) || SDLNet_Init()) return 1;
    int result = 0;
    try
    {
        Check(argc == 2, "select delivered/dropped/idle/bounded");
        const std::string mode = argv[1];
        if (mode == "delivered") Delivered();
        else if (mode == "dropped") DroppedWithoutDrain();
        else if (mode == "idle") Idle();
        else if (mode == "bounded") Bounded();
        else Check(false, "unknown test mode");
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    SDLNet_Quit(); SDL_Quit();
    return result;
}
