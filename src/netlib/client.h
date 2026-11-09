#ifndef ZNDNET_CLIENT_H_INCLUDED
#define ZNDNET_CLIENT_H_INCLUDED

#include <vector>
#include <string>
#include <atomic>

namespace ZNDNet
{

class ZNDClient: public ZNDNet
{
public:
    ZNDClient(const std::string &servstring);
    ~ZNDClient() override;

    //Client methods
    void Start(const std::string &name, const IPaddress &addr);
    void Stop() override;
    bool GetResumeToken(World::NetProtocol::ResumeToken&) override;
    bool SetResumeToken(const World::NetProtocol::ResumeToken&) override;
    void ClearResumeToken() override;
    bool SetResumeOnly(bool) override;

    uint8_t GetStatus();

    bool GetSessions(SessionInfoVect &dst);
    void CreateSession(const std::string &name, const std::string &pass, uint32_t max_players);
    void JoinSession(uint64_t SID, const std::string &pass);
    void SendDisconnect();

    void CloseSession(uint32_t closeTime) override;
    void KickUser(uint64_t _ID) override;

    bool GetUsers(UserInfoVect &dst) override;
    bool GetUser(UserInfo &dst, const char *name) override;
    bool GetUser(UserInfo &dst, uint64_t _ID) override;

    void SendData(uint64_t to, void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR) override;
    void BroadcastData(void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR) override;

    void ShowSession(bool show) override;

    uint64_t GetMyID() override;
    std::string GetMyName() override;

    uint64_t GetMySesID() override;

protected:

    Pkt *Recv_PreparePacket(InRawPkt *pkt);

    void ProcessSystemPkt(Pkt *pkt);
    void ProcessRegularPkt(Pkt *pkt);

    void SendConnect();
    // Caller holds eSyncMutex; this never joins workers or closes their socket.
    void CancelAttempt();
    void ApplyControl(const uint8_t *data,size_t size);
    void CompleteAcceptance();
    void RequestGamesList();

    void InterprocessUpdate();

    static int _RecvThread(void *data);
    static int _SendThread(void *data);
    static int _UpdateThread(void *data);
// Data
public:

protected:

    std::atomic_uint_fast32_t eStatus{0};

    IPaddress   cServAddress{};
    bool        cServHasLobby = false;
    NetUser     cME;
    bool        cLeader = false;
    std::string cJoinedSessionName;
    uint64_t    cTimeOut = 0;
    unsigned char cResumeToken[16]{};
    bool cResumeOnly=false, cAccepted=false, cHaveRoster=false, cWasResumed=false;
    // A nonzero close countdown revokes admission/custody while the existing
    // binding may still service its countdown. Hard cancel also drops all input.
    bool cAdmissionClosed=false, cCancelled=true;
    uint64_t cAttempt=0, cConnectDeadline=0, cRetryNext=0, cSession=0;
    uint32_t cBinding=0;
    RosterState cRoster;
    RosterState cStagedRoster;


    SessionInfoVect cSessions;
    uint64_t        cSessionsReqTimeNext = 0;
    std::atomic_bool cSessionsMakeRequest{false};

    UserInfoVect    cUsers;
};

}

#endif
