// Real ZND endpoints: repair lost lifecycle history, deduplicate repeated control,
// and stop application-level retransmission only after the applied roster ACK.
#include <SDL2/SDL.h>
#include "../src/netlib/zndNet.h"
#include "../src/world/net_protocol.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

using namespace ZNDNet;

static void Check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template<class Predicate>
static bool Wait(Predicate predicate, uint32_t milliseconds)
{
    const uint32_t started = SDL_GetTicks();
    do
    {
        if (predicate())
            return true;
        SDL_Delay(2);
    } while (uint32_t(SDL_GetTicks() - started) < milliseconds);
    return false;
}

// Keep teardown bounded even if a production Stop/join regresses. CTest also has
// a 35-second process limit; the independent watchdog includes exception cleanup.
class ProcessDeadline
{
    std::mutex mutex_;
    std::condition_variable changed_;
    bool done_ = false;
    std::thread worker_;
public:
    ProcessDeadline() : worker_([this] {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!changed_.wait_for(lock, std::chrono::seconds(30), [this] { return done_; }))
        {
            std::cerr << "repair test exceeded its 30-second deadline, including cleanup\n";
            std::cerr.flush();
            std::_Exit(124);
        }
    }) {}
    ~ProcessDeadline()
    {
        { std::lock_guard<std::mutex> lock(mutex_); done_ = true; }
        changed_.notify_one();
        worker_.join();
    }
};

class Host : public ZNDSingle
{
public:
    using ZNDSingle::ZNDSingle;
    struct AppliedState { bool online = false; uint64_t applied = 0, current = 0; };
    uint16_t Port()
    {
        const auto *address = SDLNet_UDP_GetPeerAddress(sock, -1);
        Check(address != nullptr, "host bind failed");
        return SDLNet_Read16(&address->port);
    }
    AppliedState Applied(uint64_t id)
    {
        SDL_LockMutex(eSyncMutex);
        AppliedState state;
        state.current = roster.revision;
        if (const auto *user = users.FindByID(id))
        {
            state.online = user->status == NetUser::STATUS_CONNECTED;
            state.applied = user->ackRevision;
        }
        SDL_UnlockMutex(eSyncMutex);
        return state;
    }
};

class LossProxy
{
    UDPsocket socket_ = nullptr;
    UDPpacket *packet_ = nullptr;
    IPaddress server_{}, client_{};
    std::atomic<bool> end_{false}, finished_{false}, failed_{false};
    std::thread worker_;
    std::set<uint32_t> sequences_; // worker-owned, bounded below
    std::set<uint32_t> finalSequences_;

    void Send(const IPaddress &to)
    {
        UDPpacket outgoing = *packet_;
        outgoing.address = to;
        outgoing.channel = -1;
        if (SDLNet_UDP_Send(socket_, -1, &outgoing) != 1)
            failed_ = true;
    }
    void Pump()
    {
        while (!end_ && !failed_)
        {
            if (!SDLNet_UDP_Recv(socket_, packet_))
            {
                SDL_Delay(1);
                continue;
            }
            const bool fromHost = IPCMP(packet_->address, server_);
            if (!fromHost)
            {
                client_ = packet_->address;
                const bool appliedAck = packet_->len > 1 &&
                    packet_->data[0] == PKT_FLAG_SYSTEM && packet_->data[1] == SYS_MSG_ROSTER_ACK;
                if (appliedAck && dropAck)
                {
                    ++acksDropped;
                    continue;
                }
                Send(server_);
                if (appliedAck)
                    ++acksForwarded;
                continue;
            }

            // This small three-seat fixture fits one regular datagram. The wire
            // header is flags/sequence/channel; roster payload starts with 'U'.
            const bool roster = packet_->len > 6 &&
                !(packet_->data[0] & (PKT_FLAG_SYSTEM | PKT_FLAG_PART)) &&
                packet_->data[5] == 0 && packet_->data[6] == 'U';
            if (roster)
            {
                ++rostersSeen;
                sequences_.insert(readU32(packet_->data + 1));
                uniqueRosters = static_cast<unsigned>(sequences_.size());
                RosterState state;
                uint64_t attempt = 0;
                uint32_t generation = 0;
                if (!DecodeRoster(packet_->data + 6, packet_->len - 6, state, attempt, generation))
                {
                    failed_ = true;
                    break;
                }
                if (state.revision == observeRevision)
                {
                    finalSequences_.insert(readU32(packet_->data + 1));
                    uniqueFinalRosters = static_cast<unsigned>(finalSequences_.size());
                }
                if (sequences_.size() > 256)
                {
                    failed_ = true;
                    break;
                }
                if (dropRoster)
                {
                    ++rostersDropped;
                    continue;
                }
            }
            Send(client_);
            if (roster && duplicate)
            {
                Send(client_);
                ++duplicateCopies;
            }
        }
        finished_ = true;
    }
public:
    std::atomic<bool> dropRoster{false}, dropAck{false}, duplicate{false};
    std::atomic<unsigned> rostersSeen{0}, uniqueRosters{0}, rostersDropped{0};
    std::atomic<unsigned> acksDropped{0}, acksForwarded{0}, duplicateCopies{0};
    std::atomic<uint64_t> observeRevision{0};
    std::atomic<unsigned> uniqueFinalRosters{0};

    explicit LossProxy(const IPaddress &server) : server_(server)
    {
        socket_ = SDLNet_UDP_Open(0);
        Check(socket_ != nullptr, "proxy bind failed");
        packet_ = SDLNet_AllocPacket(4096);
        if (!packet_)
        {
            SDLNet_UDP_Close(socket_);
            throw std::runtime_error("proxy packet allocation failed");
        }
        try { worker_ = std::thread([this] { Pump(); }); }
        catch (...) { SDLNet_FreePacket(packet_); SDLNet_UDP_Close(socket_); throw; }
    }
    ~LossProxy()
    {
        end_ = true;
        if (!Wait([this] { return finished_.load(); }, 1000))
        {
            std::cerr << "proxy failed its one-second shutdown bound\n";
            std::cerr.flush();
            std::_Exit(124);
        }
        worker_.join();
        SDLNet_FreePacket(packet_);
        SDLNet_UDP_Close(socket_);
    }
    uint16_t Port() const
    {
        return SDLNet_Read16(&SDLNet_UDP_GetPeerAddress(socket_, -1)->port);
    }
    void CheckHealthy() const { Check(!failed_, "proxy send or bounded sequence budget failed"); }
};

static size_t Count(ZNDNet::ZNDNet &endpoint)
{
    UserInfoVect rows;
    return endpoint.GetUsers(rows) ? rows.size() : 0;
}

struct LifecycleCounts
{
    std::array<uint64_t, 2> ids{};
    std::array<unsigned, 2> adds{}, leaves{};
    uint64_t markerFrom = 0;
    uint8_t markerByte = 0;
    bool markerReceived = false;

    void Drain(ZNDClient &client, bool initial = false)
    {
        // Do not let a broken producer turn a check into an unbounded drain.
        for (unsigned count = 0; count < 64; ++count)
        {
            std::unique_ptr<Event> event(client.Events_Pop());
            if (!event)
                return;
            Check(initial || event->type != EVENT_USER_LIST,
                  "within-history repair unexpectedly consolidated lifecycle state");
            if (event->type == EVENT_DATA)
            {
                const auto &data = *static_cast<EventData *>(event.get());
                Check(markerFrom && data.from == markerFrom && data.size == 1 &&
                      data.data[0] == markerByte, "unexpected gameplay marker at watcher");
                markerReceived = true;
                continue;
            }
            if (event->type != EVENT_USER_ADD && event->type != EVENT_USER_LEAVE)
                continue;
            const auto &player = *static_cast<EventNameID *>(event.get());
            const size_t index = player.id == ids[0] ? 0 : player.id == ids[1] ? 1 : 2;
            Check(index < 2, "repair emitted an unexpected lifecycle identity");
            Check(player.slot == 2, "repair changed the historical fixed slot");
            if (event->type == EVENT_USER_ADD)
            {
                Check(event->value == JoinFresh, "repair invented a resume transition");
                Check(++adds[index] == 1, "duplicate repair repeated a fresh event");
            }
            else
            {
                Check(event->value == LeaveLeft, "repair lost explicit terminal leave reason");
                Check(++leaves[index] == 1, "duplicate repair repeated a terminal event");
            }
        }
        throw std::runtime_error("lifecycle event drain exceeded its 64-event bound");
    }
    bool Complete() const { return adds == std::array<unsigned, 2>{{1, 1}} && leaves == adds; }
};

static bool ReceiveMarker(ZNDNet::ZNDNet &endpoint, uint64_t from, uint8_t expected)
{
    for (unsigned i = 0; i < 64; ++i)
    {
        std::unique_ptr<Event> event(endpoint.Events_Pop());
        if (!event)
            return false;
        if (event->type == EVENT_DATA)
        {
            const auto &data = *static_cast<EventData *>(event.get());
            Check(data.from == from && data.size == 1 && data.data[0] == expected,
                  "unexpected gameplay marker at host");
            return true;
        }
    }
    throw std::runtime_error("host marker event drain exceeded its 64-event bound");
}

static void CheckFinalRows(ZNDClient &watch, uint64_t ownId)
{
    UserInfoVect rows;
    Check(watch.GetUsers(rows) && rows.size() == 2, "repaired roster did not converge to two rows");
    bool host = false, own = false;
    for (const auto &row : rows)
    {
        Check(row.status == RosterOnline, "repair left a surviving reservation away");
        host |= row.slot == 0 && row.name == "host";
        own |= row.slot == 1 && row.ID == ownId && row.name == "watch";
    }
    Check(host && own, "repair changed surviving identities or fixed slots");
}

static void Run()
{
    const std::string service = World::NetProtocol::ServiceId;
    Host host(service);
    host.Start("host", 0, "repair", 3);
    IPaddress direct{}, route{};
    Check(SDLNet_ResolveHost(&direct, "127.0.0.1", host.Port()) == 0, "resolve local host");
    LossProxy proxy(direct);
    Check(SDLNet_ResolveHost(&route, "127.0.0.1", proxy.Port()) == 0, "resolve local proxy");
    ZNDClient watch(service), flap(service);
    watch.Start("watch", route);
    Check(Wait([&] {
        proxy.CheckHealthy();
        const auto state = host.Applied(watch.GetMyID());
        return watch.GetMyID() && Count(watch) == 2 && state.online &&
               state.current && state.applied == state.current;
    }, 4000), "initial admission/applied ACK did not complete through proxy");
    const uint64_t ownId = watch.GetMyID();
    CheckFinalRows(watch, ownId);
    Check(proxy.rostersSeen > 0 && proxy.acksForwarded > 0, "initial proxy positive control missing");
    LifecycleCounts counts;
    counts.Drain(watch, true); // Initial consolidated roster must not invent peer joins.

    proxy.dropRoster = true;
    proxy.dropAck = true;
    proxy.duplicate = true;
    for (size_t i = 0; i < counts.ids.size(); ++i)
    {
        flap.Start("flap", direct);
        Check(Wait([&] { return flap.GetMyID() && Count(flap) == 3; }, 4000), "temporary client join failed");
        counts.ids[i] = flap.GetMyID();
        flap.SendDisconnect();
        flap.Stop();
        Check(Wait([&] { return Count(host) == 2; }, 2000), "terminal leave did not release temporary row");
    }
    Check(counts.ids[0] != counts.ids[1], "fresh joins did not create distinct lifecycle identities");
    Check(Wait([&] { proxy.CheckHealthy(); return proxy.rostersDropped > 0; }, 1000), "loss fixture dropped no roster");
    const auto lostState = host.Applied(ownId);
    Check(lostState.applied < lostState.current, "host prematurely marked dropped state applied");
    const uint64_t finalRevision = lostState.current;
    proxy.observeRevision = finalRevision;
    CheckFinalRows(watch, ownId);

    proxy.dropRoster = false;
    Check(Wait([&] { proxy.CheckHealthy(); counts.Drain(watch); return counts.Complete(); }, 3000),
          "lost lifecycle history did not repair exactly once");
    CheckFinalRows(watch, ownId);
    const unsigned before = proxy.uniqueFinalRosters;
    const unsigned droppedBefore = proxy.acksDropped;
    Check(Wait([&] {
        proxy.CheckHealthy(); counts.Drain(watch);
        const auto state = host.Applied(ownId);
        Check(state.current == finalRevision && state.applied < finalRevision,
              "final revision changed or was acknowledged while applied ACKs were dropped");
        return proxy.uniqueFinalRosters >= before + 2 && proxy.acksDropped > droppedBefore;
    }, 2000), "lost applied ACK did not trigger fresh roster retransmissions");
    Check(proxy.duplicateCopies > 0, "duplicate-control positive control missing");

    const unsigned forwardedBefore = proxy.acksForwarded;
    proxy.dropAck = false;
    Check(Wait([&] {
        proxy.CheckHealthy(); counts.Drain(watch);
        const auto state = host.Applied(ownId);
        Check(state.current == finalRevision, "host changed roster during ACK settlement");
        return state.online && state.applied == finalRevision && proxy.acksForwarded > forwardedBefore;
    }, 2000), "released applied ACK did not reach and settle the host");

    unsigned previous = proxy.rostersSeen;
    uint32_t quietSince = SDL_GetTicks();
    // Prove the live route still works in both directions during the quiet
    // interval; stale status alone cannot distinguish settlement from a stall.
    uint8_t toHost = 0xb9, toWatch = 0xa7;
    counts.markerFrom = host.GetMyID();
    counts.markerByte = toWatch;
    watch.SendData(host.GetMyID(), &toHost, 1, PKT_FLAG_GARANT);
    host.SendData(ownId, &toWatch, 1, PKT_FLAG_GARANT);
    bool hostMarker = false;
    Check(Wait([&] {
        proxy.CheckHealthy(); counts.Drain(watch);
        if (!hostMarker)
            hostMarker = ReceiveMarker(host, ownId, toHost);
        const unsigned current = proxy.rostersSeen;
        if (current != previous) { previous = current; quietSince = SDL_GetTicks(); }
        return hostMarker && counts.markerReceived && uint32_t(SDL_GetTicks() - quietSince) >= 650;
    }, 2000), "roster retransmission did not become quiet after the applied ACK");
    const auto settled = host.Applied(ownId);
    Check(settled.online && settled.applied == settled.current && watch.GetMyID() == ownId,
          "quiet transport was disconnected rather than settled");
    CheckFinalRows(watch, ownId);
    Check(counts.Complete(), "lifecycle history was not retained exactly once");
    std::cout << "PASS real ZND repair: two fresh/left histories at reused slot 2, "
              << "dropped rosters=" << proxy.rostersDropped
              << ", distinct roster sequences=" << proxy.uniqueRosters
              << ", final-revision sequences=" << proxy.uniqueFinalRosters
              << ", dropped applied ACKs=" << proxy.acksDropped
              << ", duplicate copies=" << proxy.duplicateCopies
              << ", settled revision=" << settled.applied << '\n';
    watch.Stop();
    flap.Stop();
    host.Stop();
}

int main(int, char **)
{
    ProcessDeadline deadline;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) || SDLNet_Init())
        return 2;
    int result = 0;
    try { Run(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    SDLNet_Quit();
    SDL_Quit();
    return result;
}
