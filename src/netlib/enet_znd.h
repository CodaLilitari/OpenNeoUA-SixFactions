#ifndef ZNDNET_ENET_ZND_H_INCLUDED
#define ZNDNET_ENET_ZND_H_INCLUDED

#include "zndNet.h"
#include "enet_transport.h"
#include <atomic>
#include <map>

namespace ZNDNet
{

// ZNDNet-interface adapter over EnetTransport: lets NC_STACK_windp run the
// modernized ENet transport without changing its session/user/event model.
// Single-session direct play only (the ZNDSingle/ZNDClient pair semantics):
// the host owns the only session (id 1), clients auto-join it, and the host
// broadcasts a roster control message so every peer sees the same user list.
// ZND ids: host = 1, clients = transport peer id + 1.
// Opt-in at runtime (OPENNEOUA_TRANSPORT=enet); ZNDNet stays the default.
//
// Topology is a star, like ZNDSingle: every game payload travels as
//   type address(U64 LE) payload,  type 'D' = directed, 'B' = broadcast,
// where the address is the destination on client->host 'D' frames and the
// original sender on every host->client frame. The host delivers locally and
// relays to the other clients, so three to six players see each other.
// Roster frames are 'U' sesNameLen sesName countU16 { idU64 nameLen name }*.
// One lock serializes every EnetTransport call: the pump thread services the
// ENet host while the game thread sends, and ENet hosts are not thread-safe.
class ZNDEnet : public ZNDNet
{
public:
    explicit ZNDEnet(const std::string &servstring);
    ~ZNDEnet();

    // ZNDSingle-compatible host start.
    void Start(const std::string &name, std::uint16_t port,
               const std::string &sname, std::uint32_t max_players);
    // ZNDClient-compatible connect.
    void Start(const std::string &name, const IPaddress &addr);
    void Stop() override;
    bool GetResumeToken(World::NetProtocol::ResumeToken&) override;
    bool SetResumeToken(const World::NetProtocol::ResumeToken&) override;
    void ClearResumeToken() override;
    bool SetResumeOnly(bool) override;
    bool SetResumeGraceMs(uint32_t) override;
    uint32_t GetResumeGraceMs() override;


    std::uint8_t GetStatus();
    bool GetSessions(SessionInfoVect &dst);
    void CreateSession(const std::string &name, const std::string &pass,
                       std::uint32_t max_players);
    void JoinSession(std::uint64_t SID, const std::string &pass);
    void SendDisconnect();
    std::uint16_t BoundPort() const { return transport_.BoundPort(); }

    void ShowSession(bool show) override;
    void CloseSession(std::uint32_t closeTime) override;
    void KickUser(std::uint64_t _ID) override;
    bool GetUsers(UserInfoVect &dst) override;
    bool GetUser(UserInfo &dst, const char *name) override;
    bool GetUser(UserInfo &dst, std::uint64_t _ID) override;
    void SendData(std::uint64_t to, void *data, std::uint32_t sz,
                  std::uint8_t flags = 0, std::uint8_t channel = CHANNEL_USR) override;
    void BroadcastData(void *data, std::uint32_t sz,
                       std::uint8_t flags = 0, std::uint8_t channel = CHANNEL_USR) override;
    std::uint64_t GetMyID() override;
    std::string GetMyName() override;
    std::uint64_t GetMySesID() override;

private:
    static int PumpMain(void *data);
    void Pump();
    void SendRoster();
    void ApplyRoster(const std::uint8_t *data, std::size_t len);
    void HostDeliver(std::uint64_t from, const std::uint8_t *data, std::size_t len,
                     std::uint8_t channel);
    bool SendFrame(std::uint32_t peer, std::uint8_t type, std::uint64_t address,
                   const void *data, std::uint32_t sz, WireChannel channel);
    void LockUsers() const;
    void UnlockUsers() const;
    void LockNet() const;
    void UnlockNet() const;

    EnetTransport transport_;
    bool hosting_ = false;
    bool running_ = false;
    bool transportConnected_ = false;
    bool joined_ = false;
    std::string myName_;
    std::string sessionName_;
    std::uint32_t maxPlayers_ = 6;
    std::uint64_t mySesId_ = 0;
    std::map<std::uint64_t, std::string> users_;
    SDL_Thread *pumpThread_ = nullptr;
    std::atomic_bool pumpEnd_{false};
    SDL_mutex *usersMutex_ = nullptr;
    SDL_mutex *netMutex_ = nullptr; // guards transport_ (ENet is single-threaded)
    bool sessionOpen_ = true;
    RosterState roster_;
    std::map<std::uint64_t,std::uint64_t> acknowledgments_;
    std::uint64_t controlNext_ = 0, connectDeadline_ = 0, retryNext_ = 0;

};

}

#endif
