#include "zndNet.h"
#include "os_random.h"
#include "world/net_protocol.h"

namespace ZNDNet
{
ZNDSingle::ZNDSingle(const std::string& servstring)
  : ZNDNet(servstring)
  , users(ZNDNET_SES_USERS_MAX)
{
    eStatus = 0;
}

ZNDSingle::~ZNDSingle()
{
    Stop();
}
void
ZNDSingle::Stop()
{
    ZNDNet::Stop();
    threadsEnd = true;
    if (recvThread)
    {
        SDL_WaitThread(recvThread, nullptr);
        recvThread = nullptr;
    }
    if (sendThread)
    {
        SDL_WaitThread(sendThread, nullptr);
        sendThread = nullptr;
    }
    if (sock)
    {
        SDLNet_UDP_Close(sock);
        sock = nullptr;
    }
    Send_Clear();
    Confirm_Clear();
    Pending_Clear();
    Recv_Clear();
    Events_Clear();
    users.Init();
    session.clear();
    roster = RosterState();
    ME = NetUser();
    eStatus = 0;
    rateBuckets.clear();
    handshakeBudget = HandshakeBudget();
}

void
ZNDSingle::Start(const std::string& name,
                 uint16_t port,
                 const std::string& sname,
                 uint32_t max_players)
{
    Stop();
    mode = MODE_SINGLE;
    sock = SDLNet_UDP_Open(port);

    users.Init();

    ME.net = true;
    ME.ID = GenerateID();
    ME.name = name;
    ME.status = NetUser::STATUS_CONNECTED;

    session.Init(GenerateID(), sname, false);
    session.max_players = std::max(1u, std::min(6u, max_players ? max_players : 6));
    roster = RosterState();
    roster.session = session.ID;
    roster.name = sname.substr(0, 32);
    if (!OsRandom(&nextBinding, sizeof(nextBinding)))
    {
        SDLNet_UDP_Close(sock);
        sock = nullptr;
        return;
    }
    closeFlushUntil = 0;
    ME.slot = 0;
    session.open = true;

    session.users.push_back(&ME);
    session.lead = &ME;
    roster.rows.push_back({ ME.ID, 0, RosterOnline, ME.name });

    eStatus = 0;

    threadsEnd = false;

    recvThread = SDL_CreateThread(_RecvThread, "", this);
    sendThread = SDL_CreateThread(_SendThread, "", this);
    updateThread = SDL_CreateThread(_UpdateThread, "", this);
}

Pkt*
ZNDSingle::Recv_PreparePacket(InRawPkt* pkt)
{
    if (!pkt->Parse())
    {
        // Incorrect packet
        delete pkt;
        return nullptr;
    }

    NetUser* from = users.FindByIP(pkt->addr);

    if (pkt->hdr.flags & PKT_FLAG_SYSTEM) // System message
    {
        return new Pkt(pkt, from);
    }
    else if (from && from->IsOnline()) // Not system msg and known online user
    {
        if (pkt->hdr.flags & PKT_FLAG_PART) // incomplete data, do assembly
        {
            return Recv_Assemble(pkt,from);
            // if not complete -> return NULL
        }
        else // not parted packet -> make it
        {
            return new Pkt(pkt, from);
        }
    }
    else // Not system and unknown user -> delete packet
    {
        delete pkt;
    }

    return nullptr;
}

void
ZNDSingle::SendControl(const IPaddress& addr, const std::vector<uint8_t>& bytes)
{
    auto* data = RefDataStatic::create(const_cast<uint8_t*>(bytes.data()), bytes.size());
    Send_PushData(new SendingData(addr, 0, data, PKT_FLAG_SYSTEM));
}
void
ZNDSingle::SendRoster(NetUser* usr)
{
    auto bytes = EncodeRoster(roster, usr->attempt, usr->bindingGeneration);
    auto* data = RefDataStatic::create(bytes.data(), bytes.size());
    Send_PushData(MkSendingData(usr, data, PKT_FLAG_GARANT, 0));
    usr->controlNext = TransportNow() + 250;
}
void
ZNDSingle::SendAcceptance(NetUser* usr)
{
    ControlWriter w;
    w.U(SYS_MSG_CONNECTED, 1);
    w.U(0, 1);
    w.U(usr->ID, 8);
    w.Str(usr->name);
    w.b.insert(w.b.end(), usr->resumeToken, usr->resumeToken + 16);
    w.U(usr->resumed, 1);
    w.U(usr->slot, 1);
    w.U(resumeGraceMs, 4);
    w.U(usr->attempt, 8);
    w.U(usr->bindingGeneration, 4);
    w.U(session.ID, 8);
    w.Str(session.name);
    SendControl(usr->addr, w.b);
    SendRoster(usr);
}
void
ZNDSingle::Changed(NetUser* usr, Transition transition)
{
    RosterRow row{
        usr->ID, usr->slot, usr->IsOnline() ? uint8_t(RosterOnline) : uint8_t(RosterAway), usr->name
    };
    roster.rows.erase(std::remove_if(roster.rows.begin(),
                                     roster.rows.end(),
                                     [&](const RosterRow& r) { return r.id == row.id; }),
                      roster.rows.end());
    if (transition == Transition::Fresh || transition == Transition::Resumed ||
        transition == Transition::Away)
        roster.rows.push_back(row);
    std::sort(roster.rows.begin(),
              roster.rows.end(),
              [](const RosterRow& a, const RosterRow& b) { return a.slot < b.slot; });
    roster.Changed(row, transition);
    for (auto* peer : session.users)
        if (peer->net && peer->IsOnline())
            SendRoster(peer);
    eStatus |= FLAGS_USERS_LIST_UPD;
}
void
ZNDSingle::ProcessSystemPkt(Pkt* pkt)
{
    if (!pkt || !pkt->datasz)
        return;
    const auto type = pkt->data[0];
    if (type == SYS_MSG_HANDSHAKE)
    {
        if (pkt->datasz > 512 || !handshakeBudget.Allow(pkt->addr.host))
            return;
        ControlReader r(pkt->data, pkt->datasz);
        r.U(1);
        auto sl = r.U(1), nl = r.U(1);
        if (!sl || !nl || sl > 255 || nl > 20 || sl + nl + 5 > pkt->datasz)
            return;
        std::string service(reinterpret_cast<char*>(pkt->data + r.pos), sl);
        r.pos += sl;
        std::string name(reinterpret_cast<char*>(pkt->data + r.pos), nl);
        r.pos += nl;
        auto version = r.U(2);
        if (service != servString)
            return;
        if (!World::NetProtocol::ProtoCompatible(version))
        {
            SrvSendConnErr(pkt->addr, ERR_CONN_VERSION);
            return;
        }
        World::NetProtocol::ResumeToken token{};
        for (auto& v : token.b)
            v = r.U(1);
        auto flags = r.U(1);
        auto attempt = r.U(8);
        if (!r.End() || flags > 1 || !attempt || name.find('\0') != std::string::npos)
            return;
        if(session.closeTimer){SrvSendConnErr(pkt->addr,ERR_CONN_CLOSED);return;}
        NetUser* old = nullptr;
        if (World::NetProtocol::TokenValid(token))
            for (auto* u : users)
            {
                World::NetProtocol::ResumeToken have{};
                memcpy(have.b, u->resumeToken, 16);
                if (World::NetProtocol::TokenEqual(token, have))
                {
                    old = u;
                    break;
                }
            }
        auto* occupant = users.FindByIP(pkt->addr);
        if (old && occupant && occupant != old && occupant->IsOnline())
        {
            SrvSendConnErr(pkt->addr, ERR_CONN_RESUME);
            return;
        }
        if (old && (!old->IsOnline() && old->resumeExpiry <= TransportNow()))
            old = nullptr;
        if (!old && (World::NetProtocol::TokenValid(token) || flags))
        {
            SrvSendConnErr(pkt->addr, ERR_CONN_RESUME);
            return;
        }
        // A lost first acceptance has not yet delivered its token. The same
        // source/name/attempt is still the original handshake, not a newcomer.
        if (!old && occupant && occupant->IsOnline() && occupant->attempt == attempt &&
            occupant->name == name)
            old = occupant;
        if (old)
        {
            if (old->attempt == attempt && IPCMP(old->addr, pkt->addr))
            {
                SendAcceptance(old);
                return;
            }
            Events_ClearDataFrom(old->ID);
            const IPaddress prior = old->addr;
            Send_Clear(prior);
            Confirm_Clear(prior);
            Pending_Clear(prior);
            old->addr = pkt->addr;
            old->attempt = attempt;
            old->bindingGeneration = ++nextBinding;
            old->status = NetUser::STATUS_CONNECTED;
            old->resumeExpiry = 0;
            old->resumed = true;
            old->ackRevision = 0;
            old->pingTime = old->pongTime = ttime.GetTicks();
            old->pingSeq = old->pongSeq = 0;
            Changed(old, Transition::Resumed);
            Events_Push(new EventNameID(EVENT_USER_ADD, JoinResumed, old->name, old->ID, old->slot));
            SendAcceptance(old);
            return;
        }
        if (occupant && occupant->IsOnline())
        {
            SrvSendConnErr(pkt->addr, ERR_CONN_RESUME);
            return;
        }
        if (!session.open || session.closeTimer)
        {
            SrvSendConnErr(pkt->addr, ERR_CONN_CLOSED);
            return;
        }
        if (session.users.size() >= session.max_players)
        {
            SrvSendConnErr(pkt->addr, ERR_CONN_FULL);
            return;
        }
        CorrectName(name);
        bool nameTaken=CallsignEqual(name,ME.name);for(auto *known:users)if(CallsignEqual(name,known->name))nameTaken=true;
        if (nameTaken)
        {
            SrvSendConnErr(pkt->addr, ERR_CONN_NAME);
            return;
        }
        auto* u = users.Alloc();
        if (!u)
        {
            SrvSendConnErr(pkt->addr, ERR_CONN_FULL);
            return;
        }
        if (!GenerateResumeToken(u))
        {
            users.Free(u);
            SrvSendConnErr(pkt->addr, ERR_CONN_RANDOM);
            return;
        }
        std::array<bool, 6> used{};
        for (auto* p : session.users)
            used[p->slot] = true;
        u->slot = 1;
        while (u->slot < 6 && used[u->slot])
            ++u->slot;
        u->addr = pkt->addr;
        u->status = NetUser::STATUS_CONNECTED;
        u->ID = GenerateID();
        u->name = name;
        u->net = true;
        u->sesID = session.ID;
        u->attempt = attempt;
        u->bindingGeneration = ++nextBinding;
        u->resumed = false;
        u->ackRevision = 0;
        u->resumeExpiry = 0;
        u->pingTime = u->pongTime = ttime.GetTicks();
        u->pingSeq = u->pongSeq = 0;
        session.users.push_back(u);
        Changed(u, Transition::Fresh);
        Events_Push(new EventNameID(EVENT_USER_ADD, JoinFresh, u->name, u->ID, u->slot));
        SendAcceptance(u);
        return;
    }
    auto* u = pkt->user;
    if (!u || !u->IsOnline() || !IPCMP(pkt->addr, u->addr))
        return;
    MemReader rd(pkt->data + 1, pkt->datasz - 1);
    switch (type)
    {
        case SYS_MSG_PING:
            if (rd.size() == 4)
            {
                auto seq = rd.readU32();
                if (seq > u->pongSeq && seq <= u->pingSeq)
                {
                    u->pongSeq = seq;
                    u->pongTime = ttime.GetTicks();
                }
            }
            break;
        case SYS_MSG_DELIVERED:
            if (rd.size() == 4)
                ConfirmReceive(AddrSeq(pkt->addr, rd.readU32()));
            break;
        case SYS_MSG_RETRY:
            if (rd.size() == 12)
            {
                auto seq = rd.readU32(), from = rd.readU32(), to = rd.readU32();
                ConfirmRetry(AddrSeq(pkt->addr, seq), from, to);
            }
            break;
        case SYS_MSG_ROSTER_ACK:
        {
            ControlReader r(pkt->data + 1, pkt->datasz - 1);
            auto attempt = r.U(8);
            auto gen = r.U(4);
            auto sid = r.U(8);
            auto revision = r.U(8);
            if (r.End() && attempt == u->attempt && gen == u->bindingGeneration &&
                sid == session.ID && revision <= roster.revision)
                u->ackRevision = std::max(u->ackRevision, revision);
            break;
        }
        case SYS_MSG_DISCONNECT:
        {
            ControlReader r(pkt->data + 1, pkt->datasz - 1);
            auto attempt = r.U(8);
            auto gen = r.U(4);
            if (r.End() && attempt == u->attempt && gen == u->bindingGeneration)
            {
                DisconnectUser(u, LeaveLeft);
                users.Free(u);
            }
            break;
        }
        default:
            break;
    }
}

void
ZNDSingle::ProcessRegularPkt(Pkt* pkt)
{
    if (!pkt || !pkt->user || !pkt->user->IsOnline())
        return;

    if (pkt->datasz < 1)
        return;

    MemReader rd(pkt->data + 1, pkt->datasz - 1);

    switch (pkt->data[0])
    {
        case USR_MSG_DATA:
            if (rd.size() > 25)
            {
                uint32_t generation = rd.readU32();
                if (generation != pkt->user->bindingGeneration ||
                    !IPCMP(pkt->addr, pkt->user->addr))
                    return;
                uint64_t from = rd.readU64();
                uint8_t cast = rd.readU8();
                uint64_t to = rd.readU64();
                uint32_t sz = rd.readU32();

                if (rd.size() == sz + rd.tell() && from == pkt->user->ID)
                {
                    if (cast == 0)
                    {
                        if (ME.ID == to)
                        {
                            Events_Push(
                              new EventData(EVENT_DATA,
                                            pkt->flags & (PKT_FLAG_GARANT | PKT_FLAG_ASYNC),
                                            from,
                                            (cast == 1 ? true : false),
                                            to,
                                            sz,
                                            pkt->data + 1 + rd.tell(),
                                            pkt->uchnl));
                        }
                        else
                        {
                            NetUser* usr = users.FindByID(to);
                            if (usr && usr->IsOnline())
                            {
                                RefData* dat = USRDataGenData(from,
                                                              false,
                                                              to,
                                                              pkt->data + 1 + rd.tell(),
                                                              sz,
                                                              usr->bindingGeneration);
                                SendingData* snd =
                                  new SendingData(usr->addr,
                                                  usr->GetSeq(),
                                                  dat,
                                                  pkt->flags & (PKT_FLAG_GARANT | PKT_FLAG_ASYNC));
                                snd->SetChannel(usr->__idx, pkt->uchnl);
                                Send_PushData(snd);
                            }
                        }
                    }
                    else if (cast == 1)
                    {
                        Events_Push(new EventData(EVENT_DATA,
                                                  pkt->flags & (PKT_FLAG_GARANT | PKT_FLAG_ASYNC),
                                                  from,
                                                  (cast == 1 ? true : false),
                                                  to,
                                                  sz,
                                                  pkt->data + 1 + rd.tell(),
                                                  pkt->uchnl));

                        RefData* dat =
                          USRDataGenData(from, true, to, pkt->data + 1 + rd.tell(), sz);
                        SrvSessionBroadcast(&session,
                                            dat,
                                            pkt->flags & (PKT_FLAG_GARANT | PKT_FLAG_ASYNC),
                                            pkt->uchnl,
                                            pkt->user);
                    }
                }
            }
            break;

        default:
            break;
    }
}

void
ZNDSingle::InterprocessUpdate()
{
    PendingCheck();ConfirmQueueCheck();
    const auto now = TransportNow();
    const auto ticks = ttime.GetTicks();
    for (auto it = users.begin(); it != users.end();)
    {
        auto* u = *it;
        ++it;
        if (u->IsOnline())
        {
            if (ticks > u->pongTime + TIMEOUT_USER)
            {
                DisconnectUser(u, LeaveAway);
                u->resumeExpiry = now + resumeGraceMs;
            }
            else
            {
                if (ticks > u->pingTime + DELAY_PING)
                {
                    u->pingTime = ticks;
                    ++u->pingSeq;
                    SrvSendPing(u);
                }
                if (u->ackRevision < roster.revision && now >= u->controlNext)
                    SendRoster(u);
            }
        }
        else if (u->resumeExpiry && now >= u->resumeExpiry)
        {
            DisconnectUser(u, LeaveExpired);
            users.Free(u);
        }
    }
    if (session.closeTimer && ticks >= session.closeTimer)
    {
        if (!closeFlushUntil)
        {
            for (auto* u : session.users)
                if (u->net && u->IsOnline())
                {
                    ControlWriter w;
                    w.U(SYS_MSG_DISCONNECT, 1);
                    w.U(3, 1);
                    w.U(u->attempt, 8);
                    w.U(u->bindingGeneration, 4);
                    SendControl(u->addr, w.b);
                }
            closeFlushUntil = now + 250;
        }
        else if (now >= closeFlushUntil)
            CloseSession();
    }
}

void
ZNDSingle::UserToSession(NetUser* usr, NetSession* ses)
{
    if (!usr || !ses)
        return;

    if (!ses->lobby)
    {
        if (!ses->users.empty())
        {
            RefData* dat = SrvDataGenUserJoin(usr);
            SrvSessionBroadcast(ses, dat, 0, 0, usr); // Send it in sync System channel
        }

        SrvSendSessionJoin(usr, ses, false);
    }

    ses->users.push_back(usr);

    if (usr->net)
    {
        RefData* dat = SrvDataGenUserList(ses);
        if (dat)
            Send_PushData(MkSendingData(usr, dat, 0, 0)); // Send it in sync System channel
    }
}

void
ZNDSingle::DisconnectUser(NetUser* usr, uint8_t reason)
{
    if (!usr || !usr->net)
        return;
    if (reason == LeaveAway && !usr->IsOnline())
        return;
    Send_Clear(usr->addr);
    Confirm_Clear(usr->addr);
    Pending_Clear(usr->addr);
    const bool online = usr->IsOnline();
    usr->status = NetUser::STATUS_DISCONNECTED;
    if (reason != LeaveAway)
    {
        session.users.remove(usr);
        memset(usr->resumeToken, 0, 16);
        usr->resumeExpiry = 0;
    }
    Changed(usr,
            reason == LeaveAway      ? Transition::Away
            : reason == LeaveKicked  ? Transition::Kicked
            : reason == LeaveExpired ? Transition::Expired
                                     : Transition::Left);
    Events_Push(new EventNameID(EVENT_USER_LEAVE, reason, usr->name, usr->ID, usr->slot));
    if (online)
    {
        ControlWriter w;
        w.U(SYS_MSG_DISCONNECT, 1);
        w.U(reason == LeaveKicked ? 2 : reason == LeaveLeft ? 1 : 0, 1);
        w.U(usr->attempt, 8);
        w.U(usr->bindingGeneration, 4);
        SendControl(usr->addr, w.b);
    }
}

void
ZNDSingle::ShowSession(bool show)
{
    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        session.open = show;
        SDL_UnlockMutex(eSyncMutex);
    }
}

void
ZNDSingle::CloseSession()
{
    Send_Clear();
    Confirm_Clear();
    Pending_Clear();

    for (NetUserList::iterator it = session.users.begin(); it != session.users.end();
         it = session.users.erase(it))
    {
        NetUser* usr = *it;
        if (usr)
        {
            if (usr->net)
            {
                SrvSendDisconnect(usr); // Send to user disconnect msg

                users.Free(usr);
            }
        }
    }

    eStatus = 0;
    ME.status = NetUser::STATUS_DISCONNECTED;
    Events_Push(new Event(EVENT_DISCONNECT, ERR_DISCONNECT_SERVER));

    threadsEnd = true;
}

void
ZNDSingle::CloseSession(uint32_t closeTime)
{
    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        if (!session.closeTimer)
        {
            if (closeTime > 60000)
                closeTime = 60000;

            session.closeTimer = ttime.GetTicks() + closeTime;

            RefDataWStream* dat = RefDataWStream::create(8);
            dat->writeU8(SYS_MSG_SES_CLOSE);
            dat->writeU32(closeTime);

            SrvSessionBroadcast(&session, dat, PKT_FLAG_SYSTEM, 0, nullptr);

            for(auto *u:session.users)if(u->net){memset(u->resumeToken,0,16);u->resumeExpiry=0;}
            // Terminal intent is sent before returning, so a caller may Stop
            // immediately without racing the send worker. UDP loss is still
            // possible; this does not promise notification under total loss.
            for (auto* u : session.users)
                if (u->net && u->IsOnline())
                {
                    ControlWriter w;
                    w.U(SYS_MSG_SES_CLOSE, 1);
                    w.U(closeTime, 4);
                    w.U(u->attempt, 8);
                    w.U(u->bindingGeneration, 4);
                    std::vector<uint8_t> raw(1 + w.b.size());
                    raw[0] = PKT_FLAG_SYSTEM;
                    std::copy(w.b.begin(), w.b.end(), raw.begin() + 1);
                    UDPpacket packet{};
                    packet.address = u->addr;
                    packet.channel = -1;
                    packet.data = raw.data();
                    packet.len = raw.size();
                    packet.maxlen = packet.len;
                    SDLNet_UDP_Send(sock, -1, &packet);
                }
            Events_Push(new Event(EVENT_SESSION_END, closeTime));
        }
        SDL_UnlockMutex(eSyncMutex);
    }
}

void
ZNDSingle::KickUser(uint64_t _ID)
{
    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        NetUser* usr = users.FindByID(_ID);
        if (usr && usr->net)
        {
            DisconnectUser(usr, 2);
            users.Free(usr);
        }

        SDL_UnlockMutex(eSyncMutex);
    }
}

bool
ZNDSingle::GetUsers(UserInfoVect& dst)
{
    if (!ME.IsOnline())
        return false;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        dst.resize(session.users.size());

        uint32_t i = 0;
        for (NetUserList::iterator it = session.users.begin(); it != session.users.end(); it++)
        {
            dst[i] = *(*it);

            if ((*it) == &ME)
                dst[i].lead = true;

            i++;
        }

        SDL_UnlockMutex(eSyncMutex);
    }
    return true;
}

bool
ZNDSingle::GetUser(UserInfo& dst, const char* name)
{
    if (!ME.IsOnline())
        return false;

    bool res = false;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        for (NetUserList::iterator it = session.users.begin(); it != session.users.end(); it++)
        {
            if (strcmp((*it)->name.c_str(), name) == 0)
            {
                dst = **it;
                if ((*it) == &ME)
                    dst.lead = true;

                res = true;
                break;
            }
        }

        SDL_UnlockMutex(eSyncMutex);
    }
    return res;
}

bool
ZNDSingle::GetUser(UserInfo& dst, uint64_t _ID)
{
    if (!ME.IsOnline())
        return false;

    bool res = false;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        for (NetUserList::iterator it = session.users.begin(); it != session.users.end(); it++)
        {
            if ((*it)->ID == _ID)
            {
                dst = **it;
                if ((*it) == &ME)
                    dst.lead = true;

                res = true;
                break;
            }
        }

        SDL_UnlockMutex(eSyncMutex);
    }
    return res;
}

void
ZNDSingle::SendData(uint64_t to, void* data, uint32_t sz, uint8_t flags, uint8_t channel)
{
    if (!data || !sz)
        return;

    flags &= (PKT_FLAG_GARANT | PKT_FLAG_ASYNC);

    if (channel >= ZNDNET_USER_SCHNLS)
        channel = ZNDNET_USER_SCHNLS - 1;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        NetUser* usr = users.FindByID(to);
        if (usr && usr->IsOnline())
        {
            RefData* rfdat = USRDataGenData(ME.ID, false, to, data, sz, usr->bindingGeneration);

            SendingData* snd = new SendingData(usr->addr, usr->GetSeq(), rfdat, flags);
            snd->SetChannel(usr->__idx, channel);
            Send_PushData(snd);
        }
        SDL_UnlockMutex(eSyncMutex);
    }
}

void
ZNDSingle::BroadcastData(void* data, uint32_t sz, uint8_t flags, uint8_t channel)
{
    if (!data || !sz)
        return;

    flags &= (PKT_FLAG_GARANT | PKT_FLAG_ASYNC);

    if (channel >= ZNDNET_USER_SCHNLS)
        channel = ZNDNET_USER_SCHNLS - 1;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        RefData* rfdat = USRDataGenData(ME.ID, true, session.ID, data, sz);

        SrvSessionBroadcast(&session, rfdat, flags, channel, &ME);

        SDL_UnlockMutex(eSyncMutex);
    }
}

uint64_t
ZNDSingle::GetMyID()
{
    return ME.ID;
}

std::string
ZNDSingle::GetMyName()
{
    return ME.name;
}

uint64_t
ZNDSingle::GetMySesID()
{
    return ME.sesID;
}

}
