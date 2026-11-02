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
    void Stop();

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
    void LockUsers() const;
    void UnlockUsers() const;

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
};

}

#endif
