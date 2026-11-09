// Production client lifecycle over real loopback UDP. A raw endpoint is used
// only to hold a handshake unanswered or deliver an explicit refusal.
#include "../src/netlib/zndNet.h"
#include "../src/world/net_protocol.h"
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

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

class TestHost : public ZNDSingle
{
public:
    using ZNDSingle::ZNDSingle;
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
    // Read only under the production mutex, or after Stop has joined workers.
    bool HasEvent(uint32_t type)
    {
        SDL_LockMutex(eEventMutex);
        bool found = false;
        for (Event *event : eEventList) if (event->type == type) found = true;
        SDL_UnlockMutex(eEventMutex);
        return found;
    }
    std::array<unsigned char, 16> Token()
    {
        SDL_LockMutex(eSyncMutex);
        std::array<unsigned char, 16> result;
        std::memcpy(result.data(), cResumeToken, result.size());
        SDL_UnlockMutex(eSyncMutex);
        return result;
    }
    void QueueOldDisconnects(const IPaddress &oldServer)
    {
        // Deterministic old-run backlog, inserted while no workers exist.
        // These are complete production wire packets, not modelled events.
        uint8_t bytes[] = {PKT_FLAG_SYSTEM, SYS_MSG_DISCONNECT};
        UDPpacket packet = {};
        packet.data = bytes; packet.len = sizeof(bytes); packet.address = oldServer;
        for (int i = 0; i < 8; ++i) Recv_PushInRaw(new InRawPkt(&packet));
    }
    void CheckStopped()
    {
        Check(!recvThread && !sendThread && !updateThread, "Stop retained owned thread handles");
        Check(sock == nullptr, "Stop retained a closed socket pointer");
        Check(cUsers.empty() && cSessions.empty() && cJoinedSessionName.empty(),
              "Stop retained the previous roster or session");
        Check(!eStatus && !cLeader && !cServHasLobby && !cSessionsMakeRequest,
              "Stop retained session flags");
        Check(cME.status == NetUser::STATUS_DISCONNECTED && cME.ID == 0 && cME.sesID == 0,
              "Stop retained an active identity");
        Check(sendPktList.empty() && confirmQueue.empty() && pendingPkt.empty() && recvPktList.empty(),
              "Stop retained pending traffic");
    }
};

static void WaitData(::ZNDNet::ZNDNet &receiver, const char *text)
{
    Check(Until([&] {
        while (Event *event = receiver.Events_Pop())
        {
            std::unique_ptr<Event> owned(event);
            if (event->type != EVENT_DATA) continue;
            auto *data = static_cast<EventData *>(event);
            Check(data->size == std::strlen(text) && std::memcmp(data->data, text, data->size) == 0,
                  "old or corrupted payload arrived after restart");
            return true;
        }
        return false;
    }, 5000), "data exchange failed after restart");
}

static void ConnectAndExchange(TestClient &client, TestHost &host, const std::string &name)
{
    client.Start(name, host.Address());
    bool connected = false, listed = false;
    Check(Until([&] {
        while (Event *event = client.Events_Pop())
        {
            std::unique_ptr<Event> owned(event);
            Check(event->type != EVENT_CONNERR && event->type != EVENT_DISCONNECT,
                  "previous failure event survived Start");
            if (event->type == EVENT_CONNECTED) connected = true;
            if (event->type == EVENT_USER_LIST) listed = true;
        }
        return connected && listed;
    }, 5000), "client did not connect after restart");
    UserInfoVect users;
    Check(client.GetUsers(users) && users.size() == 2, "restart retained an old roster");
    char request[] = "fresh-client-payload";
    client.SendData(host.GetMyID(), request, sizeof(request) - 1, PKT_FLAG_GARANT);
    WaitData(host, request);
    char reply[] = "fresh-host-payload";
    host.SendData(client.GetMyID(), reply, sizeof(reply) - 1, PKT_FLAG_GARANT);
    WaitData(client, reply);
}

static void Orderly()
{
    TestClient client(World::NetProtocol::ServiceId);
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        TestHost host(World::NetProtocol::ServiceId);
        host.Start("host", 0, "lifecycle", 6);
        ConnectAndExchange(client, host, "returning");
        const auto token = client.Token();
        bool any = false;
        for (unsigned char value : token) any = any || value != 0;
        Check(any, "host did not issue a resume token");
        client.Stop();
        client.Stop();
        client.CheckStopped();
        Check(client.Token() == token, "Stop changed the opaque resume token");
        host.Stop();
    }
    std::cout << "Three orderly restart cycles exchanged fresh data and retained the token\n";
}

static void NaturalFailure(bool refuse)
{
    TestClient client(World::NetProtocol::ServiceId);
    UDPsocket socket = SDLNet_UDP_Open(0);
    Check(socket != nullptr, "failure endpoint could not bind");
    IPaddress *bound = SDLNet_UDP_GetPeerAddress(socket, -1);
    IPaddress address;
    Check(bound && SDLNet_ResolveHost(&address, "127.0.0.1", SDLNet_Read16(&bound->port)) == 0,
          "failure endpoint resolve failed");
    client.Start("returning", address);
    if (refuse)
    {
        std::array<uint8_t, 512> bytes = {};
        UDPpacket packet = {};
        packet.data = bytes.data(); packet.maxlen = bytes.size(); packet.channel = -1;
        Check(Until([&] { return SDLNet_UDP_Recv(socket, &packet) != 0; }, 5000),
              "client did not send a handshake");
        Check(packet.len > 1 && bytes[0] == PKT_FLAG_SYSTEM && bytes[1] == SYS_MSG_HANDSHAKE,
              "expected a real client handshake");
        bytes[0] = PKT_FLAG_SYSTEM; bytes[1] = SYS_MSG_CONNERR; bytes[2] = ERR_CONN_VERSION;
        packet.len = 3;
        Check(SDLNet_UDP_Send(socket, -1, &packet) == 1, "refusal send failed");
    }
    // Deliberately leave the natural failure event unconsumed. Start must join
    // the ended update worker and discard its event before launching a new run.
    Check(Until([&] { return client.HasEvent(EVENT_CONNERR); }, 13000),
          "natural connection failure did not occur");
    TestHost host(World::NetProtocol::ServiceId);
    host.Start("host", 0, "after-failure", 6);
    ConnectAndExchange(client, host, "returning");
    client.Stop();
    client.CheckStopped();
    host.Stop();
    SDLNet_UDP_Close(socket);
    std::cout << (refuse ? "Refusal" : "Timeout") << " restarted without explicit Stop and exchanged data\n";
}

static void RemoteDisconnect()
{
    TestClient client(World::NetProtocol::ServiceId);
    TestHost oldHost(World::NetProtocol::ServiceId);
    oldHost.Start("old-host", 0, "old-session", 6);
    ConnectAndExchange(client, oldHost, "returning");
    oldHost.CloseSession(0);
    Check(Until([&] { return client.HasEvent(EVENT_DISCONNECT); }, 5000),
          "production host did not disconnect client");
    TestHost host(World::NetProtocol::ServiceId);
    host.Start("new-host", 0, "new-session", 6);
    ConnectAndExchange(client, host, "returning");
    client.Stop();
    client.CheckStopped();
    host.Stop();
    oldHost.Stop();
    std::cout << "Remote disconnect restarted without explicit Stop and exchanged data\n";
}

static void OldBacklog()
{
    TestClient client(World::NetProtocol::ServiceId);
    TestHost host(World::NetProtocol::ServiceId);
    host.Start("host", 0, "after-backlog", 6);
    client.Stop();
    client.QueueOldDisconnects(host.Address());
    client.Stop();
    client.CheckStopped();
    Check(!client.HasEvent(EVENT_DISCONNECT), "Stop processed old packets instead of discarding them");
    // Start must also drain packets left by the previous run, without relying
    // on a separate caller Stop. None may disconnect the fresh connection.
    client.QueueOldDisconnects(host.Address());
    ConnectAndExchange(client, host, "returning");
    client.Stop();
    client.CheckStopped();
    host.Stop();
    std::cout << "Multi-packet old receive backlog discarded without replay on Stop or Start\n";
}

int main(int argc, char **argv)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) || SDLNet_Init()) return 1;
    int result = 0;
    try
    {
        Check(argc == 2, "select orderly/refusal/timeout/disconnect/backlog");
        const std::string mode = argv[1];
        if (mode == "orderly") Orderly();
        else if (mode == "refusal") NaturalFailure(true);
        else if (mode == "timeout") NaturalFailure(false);
        else if (mode == "disconnect") RemoteDisconnect();
        else if (mode == "backlog") OldBacklog();
        else Check(false, "unknown test mode");
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    SDLNet_Quit(); SDL_Quit();
    return result;
}
