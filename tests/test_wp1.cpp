#include <SDL2/SDL.h>
#include "../src/netlib/zndNet.h"
#include "../src/netlib/enet_znd.h"
#include "../src/world/net_protocol.h"
#include <chrono>
#include <thread>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace ZNDNet;
static void
Check(bool v, const char* m)
{
    if (!v)
        throw std::runtime_error(m);
}
template<class F>
static bool
Wait(F f, unsigned ms)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do
    {
        if (f())
            return true;
        SDL_Delay(5);
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
class Host : public ZNDSingle
{
  public:
    using ZNDSingle::ZNDSingle;
    unsigned Port()
    {
        auto a = SDLNet_UDP_GetPeerAddress(sock, -1);
        return a ? SDLNet_Read16(&a->port) : 0;
    }
};
static size_t
Count(ZNDNet::ZNDNet& p)
{
    UserInfoVect u;
    return p.GetUsers(u) ? u.size() : 0;
}
static void
Run(bool enet)
{
    const std::string svc = World::NetProtocol::ServiceId;
    std::unique_ptr<ZNDNet::ZNDNet> host, one, two;
    IPaddress addr{};
    if (enet)
    {
        auto p = new ZNDEnet(svc);
        p->Start("host", 0, "reserve", 6);
        SDLNet_ResolveHost(&addr, "127.0.0.1", p->BoundPort());
        host.reset(p);
        one.reset(new ZNDEnet(svc));
        two.reset(new ZNDEnet(svc));
    }
    else
    {
        auto p = new Host(svc);
        p->Start("host", 0, "reserve", 6);
        SDLNet_ResolveHost(&addr, "127.0.0.1", p->Port());
        host.reset(p);
        one.reset(new ZNDClient(svc));
        two.reset(new ZNDClient(svc));
    }
    auto start = [&](ZNDNet::ZNDNet& p, const char* n)
    {
        if (enet)
            static_cast<ZNDEnet&>(p).Start(n, addr);
        else
            static_cast<ZNDClient&>(p).Start(n, addr);
    };
    std::vector<std::unique_ptr<ZNDNet::ZNDNet>> extras;
    for (unsigned i = 0; i < 3; ++i)
    {
        extras.emplace_back(enet ? static_cast<ZNDNet::ZNDNet*>(new ZNDEnet(svc))
                                 : static_cast<ZNDNet::ZNDNet*>(new ZNDClient(svc)));
        start(*extras.back(), ("extra" + std::to_string(i)).c_str());
    }
    start(*one, "one");
    start(*two, "two");
    bool initial = Wait([&] { return Count(*one) == 6 && Count(*two) == 6; }, 10000);
    if (!initial)
    {
        std::cerr << "counts " << Count(*host) << " " << Count(*one) << " " << Count(*two)
                  << " ids " << one->GetMyID() << " " << two->GetMyID() << "\n";
        for (auto* p : { host.get(), one.get(), two.get() })
            while (auto e = p->Events_Pop())
            {
                std::cerr << "event " << e->type << " " << e->value << "\n";
                delete e;
            }
    }
    Check(initial, "initial roster missing");
    const uint64_t id = one->GetMyID();
    Check(id != 0, "missing identity");
    one->Stop();
    bool away = false;
    Check(Wait(
            [&]
            {
                while (auto e = host->Events_Pop())
                {
                    if (e->type == EVENT_USER_LEAVE)
                        away = true;
                    delete e;
                }
                return away;
            },
            22000),
          "host did not observe recoverable loss");
    Check(Count(*host) == 6, "recoverable loss released the reserved roster row");
    std::unique_ptr<ZNDNet::ZNDNet> outsider(enet
                                               ? static_cast<ZNDNet::ZNDNet*>(new ZNDEnet(svc))
                                               : static_cast<ZNDNet::ZNDNet*>(new ZNDClient(svc)));
    start(*outsider, "outsider");
    Check(!Wait([&] { return outsider->GetMyID() != 0; }, 1500),
          "outsider consumed an away reservation");
    host->ShowSession(false);
    start(*one, "one");
    Check(Wait([&] { return one->GetMyID() == id && Count(*one) == 6; }, 10000),
          "full hidden resume failed");
    Check(Count(*host) == 6, "resume exceeded capacity");
    outsider->Stop();
    one->Stop();
    two->Stop();
    for (auto& p : extras)
        p->Stop();
    host->Stop();
}

struct Fixture
{
    bool enet;
    std::unique_ptr<ZNDNet::ZNDNet> host;
    std::vector<std::unique_ptr<ZNDNet::ZNDNet>> clients;
    IPaddress addr{};
    Fixture(bool e, unsigned capacity = 3, unsigned grace = 60000)
      : enet(e)
    {
        const std::string svc = World::NetProtocol::ServiceId;
        if (enet)
        {
            auto* p = new ZNDEnet(svc);
            host.reset(p);
            Check(p->SetResumeGraceMs(grace), "grace configuration refused");
            p->Start("host", 0, "lifecycle", capacity);
            Check(SDLNet_ResolveHost(&addr, "127.0.0.1", p->BoundPort()) == 0, "resolve");
        }
        else
        {
            auto* p = new Host(svc);
            host.reset(p);
            Check(p->SetResumeGraceMs(grace), "grace configuration refused");
            p->Start("host", 0, "lifecycle", capacity);
            Check(SDLNet_ResolveHost(&addr, "127.0.0.1", p->Port()) == 0, "resolve");
        }
    }
    ~Fixture()
    {
        for (auto& p : clients)
            p->Stop();
        host->Stop();
    }
    ZNDNet::ZNDNet& New()
    {
        const std::string svc = World::NetProtocol::ServiceId;
        clients.emplace_back(enet ? static_cast<ZNDNet::ZNDNet*>(new ZNDEnet(svc))
                                  : static_cast<ZNDNet::ZNDNet*>(new ZNDClient(svc)));
        return *clients.back();
    }
    void Start(ZNDNet::ZNDNet& p, const char* name)
    {
        if (enet)
            static_cast<ZNDEnet&>(p).Start(name, addr);
        else
            static_cast<ZNDClient&>(p).Start(name, addr);
    }
    ZNDNet::ZNDNet& Join(const char* name)
    {
        auto& p = New();
        Start(p, name);
        Check(Wait([&] { return p.GetMyID() != 0 && Count(p) >= 2; }, 10000),
              "join did not complete");
        return p;
    }
    static void Drain(ZNDNet::ZNDNet& p)
    {
        while (auto* e = p.Events_Pop())
            delete e;
    }
    bool Event(ZNDNet::ZNDNet& p, unsigned type, unsigned value, unsigned ms)
    {
        return Wait(
          [&]
          {
              while (auto* e = p.Events_Pop())
              {
                  bool match = e->type == type && e->value == value;
                  delete e;
                  if (match)
                      return true;
              }
              return false;
          },
          ms);
    }
};
static UserInfo
Find(ZNDNet::ZNDNet& p, uint64_t id)
{
    UserInfoVect users;
    p.GetUsers(users);
    for (const auto& u : users)
        if (u.ID == id)
            return u;
    throw std::runtime_error("identity missing from roster");
}
// Fails if online-token lookup admits a fresh identity, old connection remains
// authoritative, new client cannot import credentials, or migration changes slot.
static void
Migration(bool enet)
{
    Fixture f(enet);
    auto& old = f.Join("one");
    auto& watch = f.Join("two");
    auto id = old.GetMyID();
    auto before = Find(*f.host, id);
    World::NetProtocol::ResumeToken token{};
    Check(old.GetResumeToken(token), "token export failed");
    Fixture::Drain(*f.host);
    Fixture::Drain(watch);
    f.host->ShowSession(false);
    auto& fresh = f.New();
    Check(fresh.SetResumeToken(token), "token import failed");
    f.Start(fresh, "changed-callsign");
    Check(Wait([&] { return fresh.GetMyID() == id && Count(fresh) == 3; }, 10000),
          "online migration did not restore identity");
    auto after = Find(*f.host, id);
    Check(after.slot == before.slot && after.name == "one" && after.status == RosterOnline,
          "migration changed reserved identity");
    Check(f.Event(*f.host, EVENT_USER_ADD, JoinResumed, 1000), "host resumed event missing");
    Check(f.Event(watch, EVENT_USER_ADD, JoinResumed, 1000), "watcher resumed event missing");
    Check(f.Event(fresh, EVENT_SELF_RESUMED, 0, 1000), "self resumed event missing");
    Fixture::Drain(*f.host);
    unsigned char stale = 17;
    old.BroadcastData(&stale, 1, PKT_FLAG_GARANT);
    Check(!Wait(
            [&]
            {
                while (auto* e = f.host->Events_Pop())
                {
                    bool stale = e->type == EVENT_DATA && static_cast<EventData*>(e)->size == 1 &&
                                 static_cast<EventData*>(e)->data[0] == 17;
                    delete e;
                    if (stale)
                        return true;
                }
                return false;
            },
            300),
          "superseded endpoint sent accepted traffic");
    unsigned char live = 23;
    fresh.BroadcastData(&live, 1, PKT_FLAG_GARANT);
    Check(Wait(
            [&]
            {
                while (auto* e = f.host->Events_Pop())
                {
                    std::unique_ptr<Event> hold(e);
                    if (e->type == EVENT_DATA)
                    {
                        auto* d = static_cast<EventData*>(e);
                        if (d->size == 1 && d->data[0] == 23 && d->from == id)
                            return true;
                    }
                }
                return false;
            },
            3000),
          "migrated endpoint could not send");
}
// Fails if immediate goodbye+Stop orphans instead of releasing, or terminal
// token custody allows replay on an otherwise open host.
static void
Terminal(bool enet)
{
    Fixture f(enet);
    auto& one = f.Join("one");
    auto& watch = f.Join("two");
    auto id = one.GetMyID();
    auto slot = Find(*f.host, id).slot;
    World::NetProtocol::ResumeToken token{};
    Check(one.GetResumeToken(token), "token export");
    Fixture::Drain(*f.host);
    Fixture::Drain(watch);
    if (enet)
        static_cast<ZNDEnet&>(one).SendDisconnect();
    else
        static_cast<ZNDClient&>(one).SendDisconnect();
    one.Stop();
    World::NetProtocol::ResumeToken cleared{};
    Check(!one.GetResumeToken(cleared), "explicit leave retained token");
    Check(f.Event(*f.host, EVENT_USER_LEAVE, LeaveLeft, 3000),
          "explicit leave did not release terminal row");
    Check(f.Event(watch, EVENT_USER_LEAVE, LeaveLeft, 3000), "watcher terminal reason missing");
    Check(Count(*f.host) == 2, "explicit leave kept reservation");
    auto& replay = f.New();
    Check(replay.SetResumeToken(token), "import replay token");
    f.Start(replay, "one");
    Check(f.Event(replay, EVENT_CONNERR, ERR_CONN_RESUME, 3000),
          "revoked token was not explicitly refused");
    Check(Count(*f.host) == 2, "revoked token admitted new row");
    auto& replacement = f.Join("replacement");
    Check(Find(*f.host, replacement.GetMyID()).slot == slot,
          "replacement did not reuse freed slot");
}
// Fails if expiry is only checked during a subsequent handshake or if kick of
// an away reservation is mislabeled as expiration.
static void
Expiry(bool enet, bool kick)
{
    Fixture f(enet, 3, kick ? 60000 : 300);
    auto& one = f.Join("one");
    auto& watch = f.Join("two");
    auto id = one.GetMyID();
    World::NetProtocol::ResumeToken token{};
    Check(one.GetResumeToken(token), "token export");
    Fixture::Drain(*f.host);
    Fixture::Drain(watch);
    one.Stop();
    Check(f.Event(*f.host, EVENT_USER_LEAVE, LeaveAway, 22000), "away event missing");
    Check(Find(*f.host, id).status == RosterAway, "away status missing");
    if (kick)
        f.host->KickUser(id);
    Check(f.Event(*f.host, EVENT_USER_LEAVE, kick ? LeaveKicked : LeaveExpired, 3000),
          "terminal away reason missing");
    Check(f.Event(watch, EVENT_USER_LEAVE, kick ? LeaveKicked : LeaveExpired, 3000),
          "watcher away terminal reason missing");
    Check(Count(*f.host) == 2, "terminal away reservation retained");
    auto& replay = f.New();
    Check(replay.SetResumeToken(token), "token import");
    f.Start(replay, "one");
    Check(f.Event(replay, EVENT_CONNERR, ERR_CONN_RESUME, 3000), "expired token not refused");
}

// A real UDP relay deliberately loses the first handshake and acceptance and
// delivers a roster before acceptance. No production code is replaced.
class LossProxy
{
    UDPsocket socket = nullptr;
    IPaddress host{}, client{};
    std::thread thread;
    std::atomic<bool> end{ false };
    bool droppedH = false, droppedA = false, droppedRoster = false;
    std::vector<uint8_t> held;
    void Send(const IPaddress& to, const uint8_t* data, size_t n)
    {
        UDPpacket p{};
        p.address = to;
        p.channel = -1;
        p.data = const_cast<uint8_t*>(data);
        p.len = n;
        p.maxlen = n;
        SDLNet_UDP_Send(socket, -1, &p);
    }
    void Pump()
    {
        auto* p = SDLNet_AllocPacket(4096);
        while (!end)
        {
            if (SDLNet_UDP_Recv(socket, p))
            {
                bool fromHost = IPCMP(p->address, host);
                if (!fromHost)
                {
                    client = p->address;
                    if (p->len > 1 && p->data[0] == PKT_FLAG_SYSTEM &&
                        p->data[1] == SYS_MSG_HANDSHAKE && !droppedH)
                    {
                        droppedH = true;
                        continue;
                    }
                    Send(host, p->data, p->len);
                }
                else
                {
                    if (p->len > 1 && p->data[0] == PKT_FLAG_SYSTEM &&
                        p->data[1] == SYS_MSG_CONNECTED)
                    {
                        if (!droppedA)
                        {
                            droppedA = true;
                            continue;
                        }
                        if (held.empty())
                        {
                            held.assign(p->data, p->data + p->len);
                            continue;
                        }
                    }
                    bool roster =
                      p->len > 6 && !(p->data[0] & PKT_FLAG_SYSTEM) && p->data[6] == 'U';
                    if (roster && !droppedRoster)
                    {
                        droppedRoster = true;
                        continue;
                    }
                    Send(client, p->data, p->len);
                    if (roster && !held.empty())
                    {
                        Send(client, held.data(), held.size());
                        held.clear();
                    }
                }
            }
            else
                SDL_Delay(1);
        }
        SDLNet_FreePacket(p);
    }

  public:
    explicit LossProxy(const IPaddress& a)
      : host(a)
    {
        socket = SDLNet_UDP_Open(0);
        Check(socket != nullptr, "proxy bind failed");
        thread = std::thread([&] { Pump(); });
    }
    ~LossProxy()
    {
        end = true;
        thread.join();
        SDLNet_UDP_Close(socket);
    }
    unsigned Port()
    {
        return SDLNet_Read16(&SDLNet_UDP_GetPeerAddress(socket, -1)->port);
    }
};
static void
Retry()
{
    Fixture f(false);
    LossProxy proxy(f.addr);
    auto& one = f.New();
    IPaddress route{};
    SDLNet_ResolveHost(&route, "127.0.0.1", proxy.Port());
    static_cast<ZNDClient&>(one).Start("one", route);
    Check(Wait([&] { return one.GetMyID() != 0 && Count(one) == 2; }, 8000),
          "lost/reordered acceptance bundle did not recover");
    unsigned adds = 0;
    while (auto* e = f.host->Events_Pop())
    {
        if (e->type == EVENT_USER_ADD)
            ++adds;
        delete e;
    }
    Check(adds == 1 && Count(*f.host) == 2, "handshake retry duplicated identity or event");
}

static void
Close(bool enet)
{
    Fixture f(enet);
    auto& one = f.Join("one");
    Fixture::Drain(one);
    f.host->CloseSession(0);
    f.host->Stop();
    Check(f.Event(one, EVENT_SESSION_END, 0, 3000),
          "host close intent lost through immediate Stop");
    World::NetProtocol::ResumeToken token{};
    Check(!one.GetResumeToken(token), "session end retained token");
}
static void
UnknownResume(bool enet)
{
    Fixture f(enet);
    auto& one = f.New();
    Check(one.SetResumeOnly(true), "resume-only configuration");
    f.Start(one, "one");
    Check(f.Event(one, EVENT_CONNERR, ERR_CONN_RESUME, 3000),
          "zero-token resume-only was fresh-admitted");
    Check(Count(*f.host) == 1, "unknown resume consumed row");
    one.Stop();
    one.ClearResumeToken();
    Check(one.SetResumeOnly(false), "fresh mode reset");
    f.Start(one, "one");
    Check(Wait([&] { return one.GetMyID() != 0 && Count(one) == 2; }, 5000),
          "explicit fresh mode could not connect");
}
static void
Backlog(bool enet)
{
    Fixture f(enet);
    auto& one = f.Join("one");
    auto id = one.GetMyID();
    one.Stop();
    unsigned char stale = 0x5a;
    for (unsigned i = 0; i < 8; ++i)
        one.BroadcastData(&stale, 1, PKT_FLAG_GARANT);
    Fixture::Drain(*f.host);
    f.Start(one, "one");
    Check(Wait([&] { return one.GetMyID() == id && Count(one) == 2; }, 5000),
          "resume after stopped sends failed");
    Check(!Wait(
            [&]
            {
                while (auto* e = f.host->Events_Pop())
                {
                    std::unique_ptr<Event> hold(e);
                    if (e->type == EVENT_DATA && static_cast<EventData*>(e)->size == 1 &&
                        static_cast<EventData*>(e)->data[0] == stale)
                        return true;
                }
                return false;
            },
            300),
          "disconnected send backlog replayed");
}
class RawSocket
{
  public:
    UDPsocket s = nullptr;
    explicit RawSocket()
    {
        s = SDLNet_UDP_Open(0);
        Check(s != nullptr, "raw bind");
    }
    ~RawSocket()
    {
        SDLNet_UDP_Close(s);
    }
    void Send(const IPaddress& a, const std::vector<uint8_t>& bytes)
    {
        UDPpacket p{};
        p.address = a;
        p.channel = -1;
        p.data = const_cast<uint8_t*>(bytes.data());
        p.len = bytes.size();
        p.maxlen = p.len;
        Check(SDLNet_UDP_Send(s, -1, &p) == 1, "raw send failed");
    }
    std::vector<uint8_t> Receive(unsigned ms)
    {
        auto* p = SDLNet_AllocPacket(4096);
        std::vector<uint8_t> out;
        Wait(
          [&]
          {
              if (!SDLNet_UDP_Recv(s, p))
                  return false;
              out.assign(p->data, p->data + p->len);
              return true;
          },
          ms);
        SDLNet_FreePacket(p);
        return out;
    }
};
// Reuses the exact UDP address while an old-generation datagram is in flight.
// A source-address-only migration implementation would accept the stale byte.
static void
Binding()
{
    Fixture f(false, 2);
    RawSocket raw;
    World::NetProtocol::ResumeToken token{};
    uint32_t generation = 0;
    uint64_t id = 0;
    auto handshake = [&](uint64_t attempt)
    {
        ControlWriter w;
        w.U(PKT_FLAG_SYSTEM, 1);
        w.U(SYS_MSG_HANDSHAKE, 1);
        std::string svc = World::NetProtocol::ServiceId;
        w.U(svc.size(), 1);
        w.U(3, 1);
        w.b.insert(w.b.end(), svc.begin(), svc.end());
        w.b.insert(w.b.end(), { 'r', 'a', 'w' });
        w.U(2, 2);
        w.b.insert(w.b.end(), token.b, token.b + 16);
        w.U(attempt == 12 ? 1 : 0, 1);
        w.U(attempt, 8);
        raw.Send(f.addr, w.b);
        bool accepted = Wait(
          [&]
          {
              auto b = raw.Receive(50);
              if (b.size() < 3 || b[0] != PKT_FLAG_SYSTEM || b[1] != SYS_MSG_CONNECTED)
                  return false;
              ControlReader r(b.data() + 2, b.size() - 2);
              r.U(1);
              id = r.U(8);
              r.Str(20);
              for (auto& v : token.b)
                  v = r.U(1);
              r.U(1);
              r.U(1);
              r.U(4);
              auto echoed = r.U(8);
              generation = r.U(4);
              return echoed == attempt && generation;
          },
          2000);
        Check(accepted, "raw binding acceptance missing");
    };
    handshake(11);
    auto old = generation;
    auto data = [&](uint32_t gen, uint8_t value)
    {
        ControlWriter w;
        w.U(0, 1);
        w.U(1, 4);
        w.U(1, 1);
        w.U(USR_MSG_DATA, 1);
        w.U(gen, 4);
        w.U(id, 8);
        w.U(1, 1);
        w.U(0, 8);
        w.U(1, 4);
        w.U(value, 1);
        raw.Send(f.addr, w.b);
    };
    data(old, 6);
    SDL_Delay(80);
    handshake(12);
    Check(generation != old, "new attempt reused binding generation");
    bool queued = false;
    while (auto* e = f.host->Events_Pop())
    {
        if (e->type == EVENT_DATA)
            queued = true;
        delete e;
    }
    Check(!queued, "migration retained queued data from superseded binding");
    data(old, 7);
    Check(!Wait(
            [&]
            {
                while (auto* e = f.host->Events_Pop())
                {
                    bool hit = e->type == EVENT_DATA;
                    delete e;
                    if (hit)
                        return true;
                }
                return false;
            },
            200),
          "same-address stale binding was accepted");
    data(generation, 9);
    Check(Wait(
            [&]
            {
                while (auto* e = f.host->Events_Pop())
                {
                    std::unique_ptr<Event> hold(e);
                    if (e->type == EVENT_DATA && static_cast<EventData*>(e)->data[0] == 9)
                        return true;
                }
                return false;
            },
            2000),
          "current binding data rejected");
}

// Duplicate valid H packets must be bounded before token lookup/response work.
static void
Flood()
{
    Fixture f(false, 2);
    RawSocket raw;
    ControlWriter w;
    w.U(PKT_FLAG_SYSTEM, 1);
    w.U(SYS_MSG_HANDSHAKE, 1);
    std::string svc = World::NetProtocol::ServiceId;
    w.U(svc.size(), 1);
    w.U(3, 1);
    w.b.insert(w.b.end(), svc.begin(), svc.end());
    w.b.insert(w.b.end(), { 'r', 'a', 'w' });
    w.U(2, 2);
    for (unsigned i = 0; i < 16; ++i)
        w.U(0, 1);
    w.U(0, 1);
    w.U(101, 8);
    for (unsigned i = 0; i < 40; ++i)
        raw.Send(f.addr, w.b);
    unsigned accepts = 0;
    auto end = TransportNow() + 800;
    while (TransportNow() < end)
    {
        auto b = raw.Receive(10);
        if (b.size() > 1 && b[0] == PKT_FLAG_SYSTEM && b[1] == SYS_MSG_CONNECTED)
            ++accepts;
    }
    Check(accepts > 0 && accepts <= 16, "handshake response work was not rate bounded");
    Check(Count(*f.host) == 2, "duplicate handshake consumed reservations");
}

static void
HostRestart()
{
    const std::string svc = World::NetProtocol::ServiceId;
    Host h(svc);
    h.Start("host", 0, "first", 2);
    IPaddress addr{};
    SDLNet_ResolveHost(&addr, "127.0.0.1", h.Port());
    ZNDClient c(svc);
    c.Start("one", addr);
    Check(Wait([&] { return Count(c) == 2; }, 5000), "first host join");
    c.Stop();
    h.Stop();
    h.Start("host", 0, "second", 2);
    Check(h.Events_Pop() == nullptr, "host restart retained previous session events");
    h.Stop();
}

static void CapacityOne(){Fixture f(true,1);auto &one=f.New();f.Start(one,"one");Check(f.Event(one,EVENT_CONNERR,ERR_CONN_FULL,2000),"capacity-one host admitted a client");Check(Count(*f.host)==1,"capacity-one reservation exceeded");}
static void CaseNames(bool enet){Fixture f(enet,4);auto &one=f.Join("Alpha");auto id=one.GetMyID();auto &alias=f.New();f.Start(alias,"aLPHA");Check(f.Event(alias,EVENT_CONNERR,ERR_CONN_NAME,2000),"case-only callsign alias admitted");auto &hostAlias=f.New();f.Start(hostAlias,"HOST");Check(f.Event(hostAlias,EVENT_CONNERR,ERR_CONN_NAME,2000),"case-only host callsign alias admitted");Check(Count(*f.host)==2&&Find(*f.host,id).name=="Alpha","name refusal changed reservation");}
class InspectHost:public Host{
 public:using Host::Host;
 struct Stats{size_t count=0,bytes=0,parts=0;};
 Stats Pending(){SDL_LockMutex(eSyncMutex);Stats s;for(auto*p:pendingPkt){++s.count;s.bytes+=p->len; s.parts+=p->parts.size();}SDL_UnlockMutex(eSyncMutex);return s;}
 void Gate(){SDL_LockMutex(eSyncMutex);}void Ungate(){SDL_UnlockMutex(eSyncMutex);}
 size_t Queued(){SDL_LockMutex(recvPktListMutex);auto n=recvPktList.size();SDL_UnlockMutex(recvPktListMutex);return n;}
};
class InspectClient:public ZNDClient{
 public:using ZNDClient::ZNDClient;
 void Gate(){SDL_LockMutex(eSyncMutex);}void Ungate(){SDL_UnlockMutex(eSyncMutex);}
 size_t Queued(){SDL_LockMutex(recvPktListMutex);auto n=recvPktList.size();SDL_UnlockMutex(recvPktListMutex);return n;}
 std::pair<size_t,size_t> PendingAtServerCancel(){
  // Freeze the real sender so a queued handshake cannot win a scheduling race.
  // SDL mutexes are recursive; the production clear takes this same send lock.
  SDL_LockMutex(eSyncMutex);SDL_LockMutex(sendModifyMutex);
  SendConnect();const auto before=sendPktList.size();
  ControlWriter w;w.U(PKT_FLAG_SYSTEM,1);w.U(SYS_MSG_DISCONNECT,1);w.U(2,1);w.U(cAttempt,8);w.U(cBinding,4);
  UDPpacket raw{};raw.address=cServAddress;raw.data=w.b.data();raw.len=w.b.size();raw.maxlen=raw.len;raw.channel=-1;
  Pkt *packet=Recv_PreparePacket(new InRawPkt(&raw));
  if(packet){ProcessSystemPkt(packet);delete packet;}
  const auto after=sendPktList.size();
  SDL_UnlockMutex(sendModifyMutex);SDL_UnlockMutex(eSyncMutex);
  return {before,after};
 }
 void Fragment(uint32_t seq,uint32_t size,uint32_t offset=0,uint8_t flags=PKT_FLAG_PART){
  SDL_LockMutex(eSyncMutex);ControlWriter w;w.U(flags,1);w.U(seq,4);w.U(1,1);w.U(size,4);w.U(offset,4);w.U(0,1);
  UDPpacket p{};p.address=cServAddress;p.channel=-1;p.data=w.b.data();p.len=w.b.size();p.maxlen=p.len;SDLNet_UDP_Send(sock,-1,&p);SDL_UnlockMutex(eSyncMutex);
 }
};
// Malformed datagrams reach the real production receive/assembly path. Sizes
// exceed the intended 1MiB bound by one byte (not a dangerous multi-GiB probe).
static void Multipart(const std::string &kind){
 const std::string svc=World::NetProtocol::ServiceId;InspectHost host(svc);host.Start("host",0,"fragments",2);IPaddress addr{};SDLNet_ResolveHost(&addr,"127.0.0.1",host.Port());InspectClient client(svc);client.Start("one",addr);Check(Wait([&]{return Count(client)==2;},5000),"fragment fixture join");
 if(kind=="size"){client.Fragment(100,1048577);SDL_Delay(100);Check(host.Pending().count==0,"oversized multipart retained an allocation");}
 else if(kind=="budget"){for(unsigned i=0;i<20;++i){client.Fragment(100+i,1048576);SDL_Delay(3);}SDL_Delay(100);Check(host.Pending().bytes<=8*1048576,"multipart allocation budget exceeded");}
 else if(kind=="duplicates"){for(unsigned i=0;i<80;++i){client.Fragment(100,4096,1000);SDL_Delay(1);}SDL_Delay(100);Check(host.Pending().parts<=1,"duplicate fragment retention was unbounded");}
 else if(kind=="count"){for(unsigned i=0;i<80;++i){client.Fragment(100+i,4096);SDL_Delay(1);}SDL_Delay(100);Check(host.Pending().count<=32,"multipart assembly count exceeded");}
 else if(kind=="rawqueue"){RawSocket raw;host.Gate();ControlWriter w;w.U(PKT_FLAG_SYSTEM,1);w.U(SYS_MSG_PING,1);w.U(1,4);for(unsigned i=0;i<600;++i){raw.Send(addr,w.b);SDL_Delay(1);}SDL_Delay(50);auto count=host.Queued();host.Ungate();Check(count<=256,"raw receive queue count exceeded");}
 else if(kind=="inconsistent"){client.Fragment(100,4096);client.Fragment(100,5000,1);SDL_Delay(100);Check(host.Pending().count==0,"inconsistent fragment geometry retained assembly");}
 client.Stop();host.Stop();
}

static void EventSlots(bool enet){Fixture f(enet,3);auto &watch=f.Join("watch");auto &one=f.Join("one");auto id=one.GetMyID();auto slot=Find(*f.host,id).slot;Fixture::Drain(watch);Fixture::Drain(*f.host);
 if(enet)static_cast<ZNDEnet&>(one).SendDisconnect();else static_cast<ZNDClient&>(one).SendDisconnect();one.Stop();Check(Wait([&]{return Count(*f.host)==2;},2000),"terminal row not removed");auto &replacement=f.Join("replacement");auto next=replacement.GetMyID();Check(Find(*f.host,next).slot==slot,"fixture slot not reused");
 for(auto *observer:{f.host.get(),&watch}){bool left=false,joined=false;Check(Wait([&]{while(auto *e=observer->Events_Pop()){std::unique_ptr<Event>hold(e);if(e->type!=EVENT_USER_LEAVE&&e->type!=EVENT_USER_ADD)continue;auto*n=static_cast<EventNameID*>(e);if(n->id==id){Check(n->slot==slot,"queued leave lost its event-time slot");left=true;}if(n->id==next){Check(n->slot==slot,"queued join lost its event-time slot");joined=true;}}return left&&joined;},2000),"slot reuse transitions missing");}}

static uint32_t LowJoin(EnetTransport &host,EnetTransport &client){uint32_t id=0;Check(Wait([&]{TransportEvent e;if(host.Poll(e,0)&&e.type==TransportEvent::Type::Connected)id=e.peer;client.Poll(e,0);return id&&client.MyId();},4000),"low-level ENet join");return id;}
static void KickRace(){const std::string svc=World::NetProtocol::ServiceId;EnetTransport host(svc),one(svc),replay(svc);Check(host.StartHost("host",0,2),"ENet host start");Check(one.Connect("one","127.0.0.1",host.BoundPort()),"ENet connect");auto id=LowJoin(host,one);World::NetProtocol::ResumeToken token{};Check(one.GetResumeToken(token),"ENet token export");host.DisconnectPeer(id,false);
 // Deliberately never poll the kicked client again: it cannot acknowledge the
 // disconnect before the competing token claim is validated.
 Check(replay.SetResumeToken(token)&&replay.Connect("replay","127.0.0.1",host.BoundPort()),"replay connect");bool refused=false,accepted=false;Check(Wait([&]{TransportEvent e;host.Poll(e,0);if(replay.Poll(e,0)){accepted|=e.type==TransportEvent::Type::Connected;refused|=e.type==TransportEvent::Type::Disconnected&&e.reason==EnetTransport::RefuseResume;}return refused||accepted;},4000),"kick replay no result");Check(refused&&!accepted,"kick intent did not revoke online bearer before ACK");}
class BareEnet{
 public:ENetHost *host=nullptr;ENetPeer *peer=nullptr;bool connected=false,disconnected=false;unsigned received=0;
 explicit BareEnet(unsigned port){host=enet_host_create(nullptr,1,2,0,0);Check(host!=nullptr,"bare ENet host");ENetAddress a{};enet_address_set_host(&a,"127.0.0.1");a.port=port;peer=enet_host_connect(host,&a,2,0);Check(peer!=nullptr,"bare ENet connect");}
 ~BareEnet(){enet_host_destroy(host);}
 void Pump(){ENetEvent e;while(enet_host_service(host,&e,0)>0){if(e.type==ENET_EVENT_TYPE_CONNECT)connected=true;if(e.type==ENET_EVENT_TYPE_DISCONNECT)disconnected=true;if(e.type==ENET_EVENT_TYPE_RECEIVE){++received;enet_packet_destroy(e.packet);}}}
 void Send(const std::vector<uint8_t>&bytes){auto*p=enet_packet_create(bytes.data(),bytes.size(),ENET_PACKET_FLAG_RELIABLE);Check(enet_peer_send(peer,0,p)==0,"bare ENet send");}
 void Flush(){enet_host_flush(host);}
};
static void PollBound(){const std::string svc=World::NetProtocol::ServiceId;EnetTransport host(svc);Check(host.StartHost("host",0,2),"ENet host start");BareEnet bare(host.BoundPort());Check(Wait([&]{TransportEvent e;host.Poll(e,0);bare.Pump();return bare.connected;},3000),"bare connection");for(unsigned i=0;i<160;++i)bare.Send({'H'});bare.Flush();SDL_Delay(50);TransportEvent e;auto begin=TransportNow();host.Poll(e,1500);Check(TransportNow()-begin<500,"non-emitting ENet work monopolized Poll deadline");}
static void PendingEnet(){const std::string svc=World::NetProtocol::ServiceId;EnetTransport host(svc),legitimate(svc);Check(host.StartHost("host",0,5),"ENet host start");std::vector<std::unique_ptr<BareEnet>> pending;for(unsigned i=0;i<5;++i)pending.emplace_back(new BareEnet(host.BoundPort()));
 Check(Wait([&]{TransportEvent e;Check(!host.Poll(e,0)||e.type!=TransportEvent::Type::Connected,"unvalidated peer became gameplay row");bool all=true;for(auto&p:pending){p->Pump();all&=p->connected;}return all;},3000),"five pending connections did not connect");
 const uint8_t secret=0x5a;for(unsigned i=0;i<10;++i){host.Broadcast(&secret,1,WireChannel::Reliable);TransportEvent e;host.Poll(e,0);for(auto&p:pending)p->Pump();SDL_Delay(5);}for(auto&p:pending)Check(p->received==0,"pending peer received gameplay broadcast");
 Check(legitimate.Connect("legitimate","127.0.0.1",host.BoundPort()),"legitimate client connect");auto id=LowJoin(host,legitimate);Check(id!=0,"pending peers consumed seat capacity");
 Check(Wait([&]{TransportEvent e;host.Poll(e,0);legitimate.Poll(e,0);bool all=true;for(auto&p:pending){p->Pump();all&=p->disconnected;}return all;},11500),"pending peers did not expire without handshakes");}

static void SessionIncarnation(){Fixture f(true,3);auto&one=f.Join("one");const auto old=f.host->GetMySesID();Check(old&&one.GetMySesID()==old,"host/client session identity mismatch");World::NetProtocol::ResumeToken token{};Check(one.GetResumeToken(token),"export token");auto port=static_cast<ZNDEnet&>(*f.host).BoundPort();one.Stop();f.host->Stop();static_cast<ZNDEnet&>(*f.host).Start("host",port,"replacement",3);Check(f.host->GetMySesID()!=old,"host restart reused session incarnation");auto&stale=f.New();Check(stale.SetResumeToken(token),"import stale token");f.Start(stale,"one");Check(f.Event(stale,EVENT_CONNERR,ERR_CONN_RESUME,2000),"previous-incarnation token admitted");}
static void InitialRoster(bool enet){Fixture f(enet,4);f.Join("one");auto&two=f.Join("two");unsigned adds=0,lists=0;while(auto*e=two.Events_Pop()){if(e->type==EVENT_USER_ADD)++adds;if(e->type==EVENT_USER_LIST)++lists;delete e;}Check(adds==0&&lists==1,"initial roster synthesized existing-peer joins");}
static void CloseCountdown(bool enet){Fixture f(enet,3);auto&one=f.Join("one");World::NetProtocol::ResumeToken token{};Check(one.GetResumeToken(token),"export token");f.host->CloseSession(60000);auto&claim=f.New();Check(claim.SetResumeToken(token),"import token");f.Start(claim,"one");if(enet){Check(!Wait([&]{return claim.GetMyID()!=0;},200),"closed ENet host admitted bearer claim");}else{bool refused=f.Event(claim,EVENT_CONNERR,ERR_CONN_CLOSED,2000);Check(refused,"terminal close countdown still admitted bearer claim");}}
// The raw host deliberately withholds the initial roster. Acceptance therefore
// supplies custody while the real socket client is still CONNECTING.
struct PartialAdmission
{
    RawSocket server;
    InspectClient client{World::NetProtocol::ServiceId};
    IPaddress addr{}, source{};
    uint64_t attempt=0;

    PartialAdmission()
    {
        SDLNet_ResolveHost(&addr,"127.0.0.1",
                          SDLNet_Read16(&SDLNet_UDP_GetPeerAddress(server.s,-1)->port));
        StartFresh("one");
        server.Send(source,Acceptance("one"));
        World::NetProtocol::ResumeToken token{};
        Check(Wait([&]{return client.GetResumeToken(token);},1000),"partial acceptance not stored");
        Check(client.GetMyID()==0,"partial bundle connected early");
    }
    void StartFresh(const std::string &name)
    {
        client.Start(name,addr);
        auto *packet=SDLNet_AllocPacket(4096);
        Check(packet!=nullptr,"partial fixture packet allocation");
        bool handshake=Wait([&]{
            if(!SDLNet_UDP_Recv(server.s,packet) || packet->len<2 ||
               packet->data[0]!=PKT_FLAG_SYSTEM || packet->data[1]!=SYS_MSG_HANDSHAKE)
                return false;
            source=packet->address;
            ControlReader r(packet->data+2,packet->len-2);
            auto serviceLength=r.U(1), nameLength=r.U(1);
            r.pos+=serviceLength+nameLength; r.U(2);
            World::NetProtocol::ResumeToken token{};
            for(auto &v:token.b)v=r.U(1);
            const auto resumeOnly=r.U(1), nextAttempt=r.U(8);
            Check(r.End()&&nextAttempt,"partial fixture malformed handshake");
            Check(!World::NetProtocol::TokenValid(token)&&!resumeOnly,
                  "fresh Start reused explicitly cancelled custody");
            Check(nextAttempt!=attempt,"fresh Start reused cancelled attempt");
            attempt=nextAttempt;
            return true;
        },2000);
        SDLNet_FreePacket(packet);
        Check(handshake,"partial fixture handshake");
    }
    std::vector<uint8_t> Acceptance(const std::string &name)
    {
        ControlWriter w;
        w.U(PKT_FLAG_SYSTEM,1); w.U(SYS_MSG_CONNECTED,1); w.U(0,1);
        w.U(2,8); w.Str(name); for(unsigned i=0;i<16;++i)w.U(7,1);
        w.U(0,1); w.U(1,1); w.U(60000,4); w.U(attempt,8);
        w.U(9,4); w.U(123,8); w.Str("partial");
        return w.b;
    }
    void Roster(const std::string &name)
    {
        RosterState roster;
        roster.session=123; roster.name="partial";
        roster.rows={{1,0,RosterOnline,"host"},{2,1,RosterOnline,name}};
        roster.Changed(roster.rows.back(),Transition::Fresh);
        const auto body=EncodeRoster(roster,attempt,9);
        ControlWriter wire;
        wire.U(PKT_FLAG_GARANT,1); wire.U(1,4); wire.U(1,1);
        wire.b.insert(wire.b.end(),body.begin(),body.end());
        server.Send(source,wire.b);
    }
    void Cancel()
    {
        client.SendDisconnect();
        World::NetProtocol::ResumeToken token{};
        Check(!client.GetResumeToken(token),"explicit cancellation retained token");
        Check(Wait([&]{
            const auto bytes=server.Receive(10);
            if(bytes.size()<2 || bytes[0]!=PKT_FLAG_SYSTEM || bytes[1]!=SYS_MSG_DISCONNECT)
                return false;
            ControlReader r(bytes.data()+2,bytes.size()-2);
            const auto sentAttempt=r.U(8), binding=r.U(4);
            return r.End()&&sentAttempt==attempt&&binding==9;
        },1000),"cancellation did not send goodbye before invalidating the binding");
    }
    void FreshControl()
    {
        client.Stop();
        StartFresh("fresh");
        server.Send(source,Acceptance("fresh"));
        Roster("fresh");
        Check(Wait([&]{return client.GetMyID()==2&&Count(client)==2;},2000),
              "fresh Start after cancellation could not complete admission");
        World::NetProtocol::ResumeToken token{};
        Check(client.GetResumeToken(token),"fresh admission did not establish custody");
        client.Stop();
        Check(client.GetResumeToken(token),"ordinary Stop discarded fresh custody");
        client.SendDisconnect();
        Check(!client.GetResumeToken(token),"explicit cancellation after Stop retained custody");
    }
};
static void PartialCustody()
{
    PartialAdmission f;
    f.client.SendDisconnect(); f.client.Stop();
    World::NetProtocol::ResumeToken token{};
    Check(!f.client.GetResumeToken(token),"partial explicit leave retained token");
}
static void CancelLateAcceptance()
{
    PartialAdmission f;
    f.Cancel();
    f.server.Send(f.source,f.Acceptance("one"));
    World::NetProtocol::ResumeToken token{};
    Check(!Wait([&]{return f.client.GetResumeToken(token);},400),
          "late duplicate acceptance restored explicitly cancelled token");
    f.Roster("one");
    Check(!Wait([&]{return f.client.GetMyID()!=0;},150),
          "late roster completed explicitly cancelled admission");
    Check(f.server.Receive(80).empty(),"cancelled client acknowledged or retried late control");
    while(auto *event=f.client.Events_Pop()){
        const auto type=event->type; delete event;
        Check(type!=EVENT_CONNECTED&&type!=EVENT_SESSION_JOIN&&type!=EVENT_USER_LIST&&type!=EVENT_DATA,
              "cancelled attempt published admission/control/data event");
    }
    f.FreshControl();
}
static void CancelRetry()
{
    PartialAdmission f;
    f.Cancel();
    Check(!Wait([&]{
        const auto bytes=f.server.Receive(10);
        return bytes.size()>1&&bytes[0]==PKT_FLAG_SYSTEM&&bytes[1]==SYS_MSG_HANDSHAKE;
    },1600),"explicit cancellation emitted a new no-token handshake retry");
    f.FreshControl();
}
static void CancelServerTerminal(bool countdown)
{
    PartialAdmission f;
    f.Roster("one");
    Check(Wait([&]{return f.client.GetMyID()==2;},2000),"server terminal fixture admission");
    Fixture::Drain(f.client);
    while(!f.server.Receive(15).empty()){}
    ControlWriter terminal;
    terminal.U(PKT_FLAG_SYSTEM,1);
    terminal.U(countdown?SYS_MSG_SES_CLOSE:SYS_MSG_DISCONNECT,1);
    terminal.U(countdown?60000:2,countdown?4:1);
    terminal.U(f.attempt,8); terminal.U(9,4);
    // Queue terminal intent and its delayed duplicate acceptance in the same
    // actual update batch. Merely ending the outer worker loop is insufficient.
    f.client.Gate();
    f.server.Send(f.source,terminal.b);
    const bool first=Wait([&]{return f.client.Queued()>=1;},500);
    f.server.Send(f.source,f.Acceptance("one"));
    const bool both=Wait([&]{return f.client.Queued()>=2;},500);
    f.client.Ungate();
    Check(first&&both,"server terminal fixture did not queue both datagrams");
    Check(Wait([&]{
        while(auto *event=f.client.Events_Pop()){
            const auto type=event->type; delete event;
            if(type==(countdown?EVENT_SESSION_END:EVENT_DISCONNECT))return true;
        }
        return false;
    },1000),"server terminal notification missing");
    World::NetProtocol::ResumeToken token{};
    Check(!Wait([&]{return f.client.GetResumeToken(token);},250),
          "queued duplicate acceptance restored server-revoked token");
    if(countdown){
        Check(f.client.GetMyID()==2,"nonzero close countdown terminated the active binding early");
        ControlWriter ping; ping.U(PKT_FLAG_SYSTEM,1); ping.U(SYS_MSG_PING,1); ping.U(42,4);
        f.server.Send(f.source,ping.b);
        Check(Wait([&]{const auto bytes=f.server.Receive(10);return bytes.size()>1&&bytes[1]==SYS_MSG_PING;},500),
              "nonzero close countdown stopped servicing its current binding");
    }
    f.FreshControl();
}
static void CancelAfterTimeout()
{
    PartialAdmission f;
    Check(Wait([&]{
        while(auto *event=f.client.Events_Pop()){
            const bool timeout=event->type==EVENT_CONNERR&&event->value==ERR_CONN_TIMEOUT;
            delete event; if(timeout)return true;
        }
        return false;
    },12000),"partial admission did not naturally time out");
    World::NetProtocol::ResumeToken token{};
    Check(f.client.GetResumeToken(token),"natural timeout lost ordinary retained custody");
    f.client.SendDisconnect();
    Check(!f.client.GetResumeToken(token),"explicit cancellation after natural timeout retained custody");
    f.client.Stop();
    while(!f.server.Receive(15).empty()){}
    f.FreshControl();
}
static void CancelPendingSystem()
{
    PartialAdmission f;
    const auto counts=f.client.PendingAtServerCancel();
    Check(counts.first!=0,"pending cancellation fixture did not queue production handshake");
    Check(counts.second==0,"server hard cancellation left a handshake for the final sender flush");
    f.FreshControl();
}

// The fallback permits a real runtime RED against the pre-metadata transport;
// it does not supply a session identity to production events.
template<class E> static auto EventSession(const E &event, int) -> decltype(event.sessionId)
{ return event.sessionId; }
template<class E> static uint64_t EventSession(const E &, long) { return 0; }

static void EventSessions(bool enet)
{
    Fixture f(enet, 3);
    auto &one = f.Join("one");
    auto &two = f.Join("two");
    const uint64_t session = f.host->GetMySesID();
    Check(session && one.GetMySesID() == session && two.GetMySesID() == session,
          "event-session fixture did not join the same session");
    bool connected = false, joined = false, added = false;
    auto drain = [&](ZNDNet::ZNDNet &node, unsigned wanted, uint8_t marker) {
        bool found = false;
        unsigned remaining = 256;
        while (remaining--)
        {
            std::unique_ptr<Event> event(node.Events_Pop());
            if (!event) break;
            Check(event->type != EVENT_CONNERR, "unexpected connection failure in event-session fixture");
            if (event->type == EVENT_SESSION_LIST) continue;
            Check(EventSession(*event, 0) == session,
                  "transport event lost its enqueue-time session identity");
            if (&node == &one && event->type == EVENT_CONNECTED) connected = true;
            if (&node == &one && event->type == EVENT_SESSION_JOIN) joined = true;
            if (&node == f.host.get() && event->type == EVENT_USER_ADD) added = true;
            if (event->type == wanted)
            {
                if (wanted != EVENT_DATA) found = true;
                else
                {
                    auto *data = static_cast<EventData *>(event.get());
                    if (data->size == 1 && data->data[0] == marker) found = true;
                }
            }
        }
        return found;
    };
    Check(Wait([&] {
        drain(*f.host, EVENT_CONNECTED, 0);
        drain(one, EVENT_CONNECTED, 0);
        drain(two, EVENT_CONNECTED, 0);
        return connected && joined && added;
    }, 2000), "admission metadata positive controls missing");

    uint8_t marker = 0xA1;
    one.SendData(f.host->GetMyID(), &marker, 1, PKT_FLAG_GARANT);
    Check(Wait([&] { return drain(*f.host, EVENT_DATA, marker); }, 2000),
          "client-to-host directed session metadata missing");
    marker = 0xA2;
    f.host->SendData(one.GetMyID(), &marker, 1, PKT_FLAG_GARANT);
    Check(Wait([&] { return drain(one, EVENT_DATA, marker); }, 2000),
          "host-to-client directed session metadata missing");
    marker = 0xA3;
    one.BroadcastData(&marker, 1, PKT_FLAG_GARANT);
    bool hostData = false, peerData = false;
    Check(Wait([&] {
        hostData = drain(*f.host, EVENT_DATA, marker) || hostData;
        peerData = drain(two, EVENT_DATA, marker) || peerData;
        return hostData && peerData;
    }, 2000), "relayed broadcast session metadata missing");

    f.host->KickUser(two.GetMyID());
    bool hostLeft = false, peerLeft = false;
    Check(Wait([&] {
        hostLeft = drain(*f.host, EVENT_USER_LEAVE, 0) || hostLeft;
        peerLeft = drain(one, EVENT_USER_LEAVE, 0) || peerLeft;
        return hostLeft && peerLeft;
    }, 3000), "terminal leave session metadata missing");
    // The kick event stays queued after admission ends. ZND's getters clear;
    // ENet retains its historical session value after disconnect. Neither supplies
    // the provenance of this already-queued event at pop time.
    Check(Wait([&] { return (enet ? static_cast<ZNDEnet &>(two).GetStatus()
                                 : static_cast<ZNDClient &>(two).GetStatus()) == NetUser::STATUS_DISCONNECTED; }, 3000),
          "kicked endpoint did not finish disconnecting");
    if (!enet)
        Check(two.GetMyID() == 0 && two.GetMySesID() == 0,
              "disconnected ZND endpoint still exposed a live admitted identity");
    Check(drain(two, EVENT_DISCONNECT, 0),
          "queued disconnect lost its original session after live identity cleared");
    f.host->CloseSession(0);
    if (enet)
    {
        Check(static_cast<ZNDEnet &>(*f.host).GetStatus() == NetUser::STATUS_DISCONNECTED,
              "ENet host close did not stop the endpoint");
        Check(drain(*f.host, EVENT_SESSION_END, 0),
              "ENet queued host-close event lost its session across Stop");
    }
    Check(Wait([&] { return drain(one, EVENT_SESSION_END, 0); }, 3000),
          "session-end event lost its old session context");
}
int
main(int argc, char** argv)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) || SDLNet_Init())
        return 2;
    int result = 0;
    try
    {
        std::string mode = argc > 1 ? argv[1] : "znd";
        bool enet = mode.find("enet") == 0;
        if(mode=="sessionincarnation")SessionIncarnation();
        else if(mode=="partialcustody")PartialCustody();
        else if(mode=="cancel_late_acceptance")CancelLateAcceptance();
        else if(mode=="cancel_retry")CancelRetry();
        else if(mode=="cancel_server_disconnect")CancelServerTerminal(false);
        else if(mode=="cancel_server_countdown")CancelServerTerminal(true);
        else if(mode=="cancel_after_timeout")CancelAfterTimeout();
        else if(mode=="cancel_pending_system")CancelPendingSystem();
        else if(mode.find("eventsession")!=std::string::npos)EventSessions(enet);
        else if(mode.find("initialroster")!=std::string::npos)InitialRoster(enet);
        else if(mode.find("countdown")!=std::string::npos)CloseCountdown(enet);
        else if(mode=="kickrace")KickRace();
        else if(mode=="pollbound")PollBound();
        else if(mode=="pending")PendingEnet();
        else if(mode.find("eventslots")!=std::string::npos)EventSlots(enet);
        else if(mode=="capacityone")CapacityOne();
        else if(mode.find("casenames")!=std::string::npos)CaseNames(enet);
        else if(mode.find("multipart_")==0)Multipart(mode.substr(10));
        else if (mode == "hostrestart")
            HostRestart();
        else if (mode == "flood")
            Flood();
        else if (mode == "binding")
            Binding();
        else if (mode.find("close") != std::string::npos)
            Close(enet);
        else if (mode.find("unknown") != std::string::npos)
            UnknownResume(enet);
        else if (mode.find("backlog") != std::string::npos)
            Backlog(enet);
        else if (mode == "retry")
            Retry();
        else if (mode.find("migration") != std::string::npos)
            Migration(enet);
        else if (mode.find("terminal") != std::string::npos)
            Terminal(enet);
        else if (mode.find("expiry") != std::string::npos)
            Expiry(enet, false);
        else if (mode.find("awaykick") != std::string::npos)
            Expiry(enet, true);
        else
            Run(enet);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    SDLNet_Quit();
    SDL_Quit();
    return result;
}
