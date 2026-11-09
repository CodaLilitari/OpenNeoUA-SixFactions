#ifndef ZNDNET_SINGLE_H_INCLUDED
#define ZNDNET_SINGLE_H_INCLUDED


namespace ZNDNet
{

class ZNDSingle: public ZNDNet
{
public:
    ZNDSingle(const std::string &servstring);
    ~ZNDSingle() override;
    void Stop() override;

    void Start(const std::string &name, uint16_t port, const std::string &sname, uint32_t max_players);

    void ShowSession(bool show) override;
    void CloseSession(uint32_t closeTime) override;

    void KickUser(uint64_t _ID) override;

    bool GetUsers(UserInfoVect &dst) override;
    bool GetUser(UserInfo &dst, const char *name) override;
    bool GetUser(UserInfo &dst, uint64_t _ID) override;

    void SendData(uint64_t to, void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR) override;
    void BroadcastData(void *data, uint32_t sz, uint8_t flags = 0, uint8_t channel = CHANNEL_USR) override;

    uint64_t GetMyID() override;
    std::string GetMyName() override;

    uint64_t GetMySesID() override;

protected:
    static int _RecvThread(void *data);
    static int _SendThread(void *data);
    static int _UpdateThread(void *data);

    void DisconnectUser(NetUser *usr, uint8_t type);
    void SendAcceptance(NetUser *usr);
    void SendRoster(NetUser *usr);
    void Changed(NetUser *usr, Transition transition);
    void SendControl(const IPaddress &addr, const std::vector<uint8_t>&bytes);

    Pkt *Recv_PreparePacket(InRawPkt *pkt);
    void ProcessSystemPkt(Pkt *pkt);
    void ProcessRegularPkt(Pkt *pkt);

    void InterprocessUpdate();

    void UserToSession(NetUser *usr, NetSession *ses);

    void CloseSession();

// Data
public:

protected:
    NetUser     ME;
    NetUsersPack users;

    NetSession  session;
    RosterState roster;
    uint64_t closeFlushUntil = 0;
    uint32_t nextBinding = 0;
    HandshakeBudget handshakeBudget;

    std::atomic_uint_fast32_t eStatus;
};

}

#endif
