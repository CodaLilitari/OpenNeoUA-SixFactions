// Real direct-host/client session-descriptor coverage. Negative raw frames are
// test inputs; production transport serializers/parsers perform all admission.
// No fake provider, game-policy replacement, or actual resume credential.
#include "../src/netlib/zndNet.h"
#include "../src/netlib/enet_znd.h"
#include "../src/world/net_protocol.h"
#include <SDL.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace ZNDNet;
static void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message); // Deliberately active under NDEBUG.
}
static bool Wait(const std::function<bool()>& predicate, unsigned ms = 3000)
{
    const auto begin = SDL_GetTicks();
    do {
        if (predicate()) return true;
        SDL_Delay(2);
    } while (uint32_t(SDL_GetTicks() - begin) < ms);
    return false;
}
class Watchdog {
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    std::thread worker;
public:
    Watchdog() : worker([this] {
        std::unique_lock<std::mutex> lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(20), [this] { return done; }))
            std::_Exit(124); // Includes cleanup if a production Stop ever hangs.
    }) {}
    ~Watchdog() {
        { std::lock_guard<std::mutex> lock(mutex); done = true; }
        changed.notify_one(); worker.join();
    }
};
class NativeHost : public ZNDSingle {
public:
    using ZNDSingle::ZNDSingle;
    uint16_t Port() const {
        return sock ? SDLNet_Read16(&SDLNet_UDP_GetPeerAddress(sock, -1)->port) : 0;
    }
};
class InspectClient : public ZNDClient {
public:
    using ZNDClient::ZNDClient;
    std::string SessionName() {
        SDL_LockMutex(eSyncMutex);
        const auto result = cJoinedSessionName;
        SDL_UnlockMutex(eSyncMutex);
        return result;
    }
    bool HaveRoster() {
        SDL_LockMutex(eSyncMutex); const bool value = cHaveRoster;
        SDL_UnlockMutex(eSyncMutex); return value;
    }
    bool Accepted() {
        SDL_LockMutex(eSyncMutex); const bool value = cAccepted;
        SDL_UnlockMutex(eSyncMutex); return value;
    }
};
struct Pair {
    std::unique_ptr<::ZNDNet::ZNDNet> host, client;
    bool enet;
    explicit Pair(bool useEnet, const std::string& name) : enet(useEnet) {
        const std::string service = World::NetProtocol::ServiceId;
        uint16_t port;
        if (enet) {
            auto* h = new ZNDEnet(service); host.reset(h);
            h->Start("WP3BLACK", 0, name, 2); port = h->BoundPort();
            client.reset(new ZNDEnet(service));
        } else {
            auto* h = new NativeHost(service); host.reset(h);
            h->Start("WP3BLACK", 0, name, 2); port = h->Port();
            client.reset(new InspectClient(service));
        }
        Check(port != 0, "valid descriptor did not create a listener");
        IPaddress address{};
        Check(SDLNet_ResolveHost(&address, "127.0.0.1", port) == 0, "resolve");
        if (enet) static_cast<ZNDEnet*>(client.get())->Start("WP3SULG", address);
        else static_cast<InspectClient*>(client.get())->Start("WP3SULG", address);
    }
    ~Pair() { if (client) client->Stop(); if (host) host->Stop(); }
};
static void VerifyRows(::ZNDNet::ZNDNet& node, uint64_t host, uint64_t peer)
{
    UserInfoVect rows;
    Check(node.GetUsers(rows) && rows.size() == 2, "complete roster missing");
    bool foundHost = false, foundPeer = false;
    for (const auto& row : rows) {
        foundHost |= row.ID == host && row.slot == 0 && row.lead && row.name == "WP3BLACK" && row.status == RosterOnline;
        foundPeer |= row.ID == peer && row.slot == 1 && !row.lead && row.name == "WP3SULG" && row.status == RosterOnline;
    }
    Check(foundHost && foundPeer, "roster identity/seat mismatch");
}
static void Marker(::ZNDNet::ZNDNet& from, ::ZNDNet::ZNDNet& to)
{
    uint8_t value = 77;
    from.SendData(to.GetMyID(), &value, 1, PKT_FLAG_GARANT);
    Check(Wait([&] {
        for (unsigned i = 0; i < 64; ++i) {
            std::unique_ptr<Event> event(to.Events_Pop());
            if (!event) break;
            Check(event->type != EVENT_CONNECTED && event->type != EVENT_SESSION_JOIN,
                  "late duplicate admission event during marker exchange");
            if (event->type == EVENT_DATA) {
                const auto* data = static_cast<EventData*>(event.get());
                return data->from == from.GetMyID() && data->size == 1 && data->data[0] == value;
            }
        }
        return false;
    }), "live reliable endpoint marker missing");
}
static void RoundTrip(bool enet, const std::string& name)
{
    Pair pair(enet, name);
    unsigned connected = 0, joined = 0;
    Check(Wait([&] {
        for (unsigned i = 0; i < 64; ++i) {
            std::unique_ptr<Event> event(pair.client->Events_Pop());
            if (!event) break;
            if (event->type == EVENT_CONNECTED) {
                auto* data = static_cast<EventNameID*>(event.get());
                Check(data->name == "WP3SULG" && data->id == pair.client->GetMyID() &&
                    event->sessionId == pair.host->GetMySesID(), "connected tuple differs");
                ++connected;
            }
            if (event->type == EVENT_SESSION_JOIN) {
                auto* data = static_cast<EventNameID*>(event.get());
                Check(data->name == name, "session descriptor truncated or changed");
                Check(data->id == pair.host->GetMySesID() && data->id == event->sessionId,
                      "joined session identity differs");
                ++joined;
            }
        }
        return connected && joined;
    }), "client admission events missing for full descriptor");
    Check(connected == 1 && joined == 1, "duplicate admission events");
    const auto h = pair.host->GetMyID(), c = pair.client->GetMyID();
    Check(h && c && h != c && pair.client->GetMySesID() == pair.host->GetMySesID(), "endpoint identities invalid");
    VerifyRows(*pair.host, h, c); VerifyRows(*pair.client, h, c);
    Marker(*pair.client, *pair.host); Marker(*pair.host, *pair.client);
    Check(!Wait([&] {
        for (unsigned i = 0; i < 64; ++i) {
            std::unique_ptr<Event> event(pair.client->Events_Pop());
            if (!event) break;
            if (event->type == EVENT_CONNECTED || event->type == EVENT_SESSION_JOIN) return true;
        }
        return false;
    }, 180), "late duplicate admission event after marker exchange");
}
static void InvalidHost(bool enet, const std::string& name, bool reused = false)
{
    if (enet) {
        ZNDEnet host(World::NetProtocol::ServiceId);
        if (reused) {
            host.Start("host", 0, "previous", 2);
            Check(host.BoundPort() != 0 && host.GetMySesID() != 0, "reused ENet control did not host");
        }
        host.Start("host", 0, name, 2);
        SessionInfoVect sessions;
        Check(host.BoundPort() == 0 && host.GetMySesID() == 0 && host.GetMyID() == 0 &&
              host.GetMyName().empty() && !host.GetSessions(sessions),
              "invalid descriptor created ENet listener/session");
    } else {
        NativeHost host(World::NetProtocol::ServiceId);
        if (reused) {
            host.Start("host", 0, "previous", 2);
            Check(host.Port() != 0 && host.GetMySesID() != 0, "reused ZND control did not host");
        }
        host.Start("host", 0, name, 2);
        Check(host.Port() == 0 && host.GetMySesID() == 0 && host.GetMyID() == 0 &&
              host.GetMyName().empty(),
              "invalid descriptor created ZND listener/session");
    }
}
static RosterState Roster(const std::string& name)
{
    RosterState state; state.session = 123; state.revision = 1; state.name = name;
    state.rows = {{1, 0, RosterOnline, "host"}, {2, 1, RosterOnline, "one"}};
    return state;
}
static std::vector<uint8_t> RawRoster(const std::string& name, uint64_t attempt)
{
    ControlWriter writer;
    writer.U('U', 1); writer.U(attempt, 8); writer.U(9, 4);
    writer.U(123, 8); writer.U(1, 8); writer.Str(name); writer.U(2, 1);
    for (const auto& row : Roster(name).rows) WriteRow(writer, row);
    writer.U(0, 1);
    return writer.b;
}
static void Codec()
{
    for (unsigned length : {1u, 32u, 33u, 51u, 255u}) {
        const auto state = Roster(std::string(length, 's'));
        const auto bytes = EncodeRoster(state, 8, 9);
        Check(bytes == RawRoster(state.name, 8), "valid descriptor changed existing wire layout");
        RosterState out; uint64_t attempt = 0; uint32_t generation = 0;
        Check(DecodeRoster(bytes.data(), bytes.size(), out, attempt, generation) &&
            out.name == state.name && attempt == 8 && generation == 9, "valid descriptor codec rejected");
    }
    for (const auto& name : {std::string(), std::string("a\0b", 3), std::string(256, 'x')}) {
        Check(EncodeRoster(Roster(name), 8, 9).empty(), "invalid descriptor encoded");
        const auto bytes = RawRoster(name, 8);
        RosterState out = Roster("unchanged"); uint64_t attempt = 44; uint32_t generation = 55;
        Check(!DecodeRoster(bytes.data(), bytes.size(), out, attempt, generation) &&
            out.name == "unchanged" && attempt == 44 && generation == 55,
            "invalid descriptor decoder mutated outputs");
    }
    auto bytes = RawRoster("valid", 8); bytes.pop_back();
    RosterState out; uint64_t attempt = 0; uint32_t generation = 0;
    Check(!DecodeRoster(bytes.data(), bytes.size(), out, attempt, generation), "truncated roster accepted");
}
struct RawServer {
    UDPsocket socket = SDLNet_UDP_Open(0);
    RawServer() { Check(socket != nullptr, "raw socket"); }
    ~RawServer() { if (socket) SDLNet_UDP_Close(socket); }
    uint16_t Port() const { return SDLNet_Read16(&SDLNet_UDP_GetPeerAddress(socket, -1)->port); }
    void Send(const IPaddress& to, const std::vector<uint8_t>& bytes) {
        UDPpacket packet{}; packet.address = to; packet.channel = -1;
        packet.data = const_cast<uint8_t*>(bytes.data()); packet.len = packet.maxlen = int(bytes.size());
        Check(SDLNet_UDP_Send(socket, -1, &packet) == 1, "raw send");
    }
};
struct RawFixture {
    RawServer server;
    InspectClient client{World::NetProtocol::ServiceId};
    IPaddress source{};
    uint64_t attempt = 0;
    uint32_t sequence = 0;
    RawFixture() {
        IPaddress address{}; SDLNet_ResolveHost(&address, "127.0.0.1", server.Port());
        client.Start("one", address);
        uint8_t buffer[1024]; UDPpacket packet{}; packet.data = buffer; packet.maxlen = sizeof(buffer);
        Check(Wait([&] {
            if (!SDLNet_UDP_Recv(server.socket, &packet)) return false;
            Check(packet.len > 4 && buffer[0] == PKT_FLAG_SYSTEM && buffer[1] == SYS_MSG_HANDSHAKE, "raw handshake");
            source = packet.address;
            ControlReader reader(buffer + 2, packet.len - 2);
            const auto service = reader.U(1), name = reader.U(1);
            Check(reader.ok && reader.pos <= reader.n && service + name <= reader.n - reader.pos,
                  "handshake strings exceed received bytes");
            reader.pos += service + name; reader.U(2);
            for (unsigned i = 0; i < 17; ++i) reader.U(1);
            attempt = reader.U(8); return reader.End() && attempt;
        }), "raw client handshake missing");
    }
    ~RawFixture() { client.Stop(); }
    void Acceptance(const std::string& name) {
        ControlWriter writer; writer.U(PKT_FLAG_SYSTEM, 1); writer.U(SYS_MSG_CONNECTED, 1); writer.U(0, 1);
        writer.U(2, 8); writer.Str("one"); for (unsigned i = 0; i < 16; ++i) writer.U(7, 1);
        writer.U(0, 1); writer.U(1, 1); writer.U(60000, 4); writer.U(attempt, 8);
        writer.U(9, 4); writer.U(123, 8); writer.Str(name); server.Send(source, writer.b);
    }
    void Control(const std::string& name) {
        const auto body = RawRoster(name, attempt);
        ControlWriter writer; writer.U(PKT_FLAG_GARANT, 1); writer.U(++sequence, 4); writer.U(1, 1);
        writer.b.insert(writer.b.end(), body.begin(), body.end()); server.Send(source, writer.b);
    }
    void NoAdmission() {
        Check(!Wait([&] { return client.GetMyID() != 0; }, 180), "bad descriptor completed admission");
        for (unsigned i = 0; i < 64; ++i) {
            std::unique_ptr<Event> event(client.Events_Pop()); if (!event) break;
            Check(event->type != EVENT_CONNECTED && event->type != EVENT_SESSION_JOIN &&
                event->type != EVENT_USER_LIST, "bad descriptor published admission event");
        }
    }
    void Complete() {
        Check(Wait([&] { return client.GetMyID() == 2 && client.GetMySesID() == 123; }), "valid follow-up admission failed");
        Check(client.SessionName() == "valid", "accepted descriptor differs");
    }
};
static void RawNegative(const std::string& mode)
{
    RawFixture fixture;
    if (mode == "accept_empty" || mode == "accept_nul" || mode == "accept_256") {
        fixture.Control("valid");
        fixture.Acceptance(mode == "accept_empty" ? std::string() : mode == "accept_nul" ? std::string("a\0b", 3) : std::string(256, 'x'));
        fixture.NoAdmission(); fixture.Acceptance("valid"); fixture.Complete();
    } else if (mode == "roster_first") {
        fixture.Control("valid"); Check(Wait([&] { return fixture.client.HaveRoster(); }), "roster was not staged first");
        fixture.Acceptance("different");
        fixture.NoAdmission(); fixture.Acceptance("valid"); fixture.Complete();
    } else if (mode == "accept_first") {
        fixture.Acceptance("valid"); Check(Wait([&] { return fixture.client.Accepted(); }), "acceptance was not staged first");
        fixture.Control("different");
        fixture.NoAdmission(); fixture.Control("valid"); fixture.Complete();
    } else if (mode == "changed_accept") {
        fixture.Acceptance("valid"); fixture.Control("valid"); fixture.Complete();
        for (unsigned i = 0; i < 64; ++i) {
            std::unique_ptr<Event> event(fixture.client.Events_Pop()); if (!event) break;
        }
        fixture.Acceptance("different"); SDL_Delay(180);
        UserInfoVect rows;
        Check(fixture.client.SessionName() == "valid" && fixture.client.GetMyID() == 2 &&
            fixture.client.GetMySesID() == 123 && fixture.client.GetUsers(rows) && rows.size() == 2,
            "changed acceptance altered descriptor or connected identity");
        for (unsigned i = 0; i < 64; ++i) {
            std::unique_ptr<Event> event(fixture.client.Events_Pop()); if (!event) break;
            Check(event->type != EVENT_CONNECTED && event->type != EVENT_SESSION_JOIN,
                "changed acceptance published duplicate admission");
        }
    } else throw std::runtime_error("unknown raw case");
}
int main(int argc, char** argv)
{
    Watchdog watchdog;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) || SDLNet_Init()) return 2;
    int result = 0;
    try {
        const std::string mode = argc > 1 ? argv[1] : "codec";
        if (mode == "intentional_failure") Check(false, "intentional NDEBUG failure");
        else if (mode == "codec") Codec();
        else if (mode.compare(0, 4, "raw_") == 0) RawNegative(mode.substr(4));
        else {
            const bool enet = mode.compare(0, 5, "enet_") == 0;
            auto kind = mode.substr(enet ? 5 : 4);
            const bool reused = kind.compare(0, 7, "rehost_") == 0;
            if (reused) kind = kind.substr(7);
            if (kind == "empty" || kind == "nul" || kind == "256")
                InvalidHost(enet, kind == "empty" ? std::string() : kind == "nul" ? std::string("a\0b", 3) : std::string(256, 'x'), reused);
            else {
                const std::string descriptor = kind == "frontend"
                    ? "201|WP3BLACK|JUL 09 1998  23:52:47-SIX-FACTIONS-MP1"
                    : std::string(std::stoul(kind), 's');
                RoundTrip(enet, descriptor);
            }
        }
        std::cout << "PASS " << mode << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    SDLNet_Quit(); SDL_Quit(); return result;
}
