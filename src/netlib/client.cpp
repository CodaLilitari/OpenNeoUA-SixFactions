#include "zndNet.h"
#include "os_random.h"
#include "world/net_protocol.h"

namespace ZNDNet
{

ZNDClient::ZNDClient(const std::string& servstring)
  : ZNDNet(servstring)
{
}

ZNDClient::~ZNDClient()
{
    // The base destructor runs after our strings and vectors have been
    // destroyed. Workers must stop while all client members still exist.
    Stop();
}

void
ZNDClient::Stop()
{
    // External-owner operation only. The update worker joins recv/send and
    // closes its socket; joining it first also handles natural termination.
    ZNDNet::Stop();
    threadsEnd = true;
    // A failed SDL_CreateThread may leave workers without an update worker.
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

    // No owned workers remain. Discard packets queued after a natural stop,
    // including retries and events from that run, before another Start.
    Send_Clear();
    Confirm_Clear();
    Pending_Clear();
    Recv_Clear();
    eEventWaitLock = 0;
    Events_Clear();
    rateBuckets.clear();
    eStatus = 0;
    cServAddress = IPaddress{};
    cServHasLobby = false;
    cLeader = false;
    cME = NetUser();
    cJoinedSessionName.clear();
    cTimeOut = 0;
    cAttempt = cSession = 0;
    cBinding = 0;
    cAccepted = cHaveRoster = false;
    cCancelled = true;
    cRoster = RosterState();
    cStagedRoster = RosterState();
    cSessions.clear();
    cUsers.clear();
    cSessionsReqTimeNext = 0;
    cSessionsMakeRequest = false;
    // cResumeToken intentionally survives. Admission/reclaim policy is not
    // changed by transport lifecycle cleanup.
}

bool
ZNDClient::GetResumeToken(World::NetProtocol::ResumeToken& out)
{
    SDL_LockMutex(eSyncMutex);
    memcpy(out.b, cResumeToken, 16);
    SDL_UnlockMutex(eSyncMutex);
    return World::NetProtocol::TokenValid(out);
}
bool
ZNDClient::SetResumeToken(const World::NetProtocol::ResumeToken& in)
{
    if (updateThread)
        return false;
    memcpy(cResumeToken, in.b, 16);
    cResumeOnly = World::NetProtocol::TokenValid(in);
    return true;
}
void
ZNDClient::ClearResumeToken()
{
    if (!updateThread)
    {
        memset(cResumeToken, 0, 16);
        cResumeOnly = false;
    }
}
bool
ZNDClient::SetResumeOnly(bool only)
{
    if (updateThread)
        return false;
    cResumeOnly = only;
    return true;
}
void
ZNDClient::CompleteAcceptance()
{
    if (cCancelled || cAdmissionClosed || !cAccepted || !cHaveRoster || cStagedRoster.session != cSession)
        return;
    const bool first = cME.status != NetUser::STATUS_CONNECTED;
    cME.status = NetUser::STATUS_CONNECTED;
    cME.sesID = cSession;
    eStatus |= FLAGS_SESSION_JOINED;
    if (first)
    {
        Events_Push(new EventNameID(EVENT_CONNECTED, 0, cME.name, cME.ID, cME.slot, cSession));
        Events_Push(new EventNameID(EVENT_SESSION_JOIN, 0, cJoinedSessionName, cSession, 0xff, cSession));
        if (cWasResumed)
            Events_Push(new Event(EVENT_SELF_RESUMED, 0, cSession));
    }
    cTimeOut = ttime.GetTicks() + TIMEOUT_USER;
}
void
ZNDClient::ApplyControl(const uint8_t* data, size_t size)
{
    RosterState next;
    uint64_t attempt = 0;
    uint32_t generation = 0;
    if (!DecodeRoster(data, size, next, attempt, generation) || attempt != cAttempt ||
        (cAccepted && (generation != cBinding || next.session != cSession)))
        return;
    if(cAccepted){
        const auto self=std::find_if(next.rows.begin(),next.rows.end(),[&](const RosterRow&row){return row.id==cME.ID;});
        if(self==next.rows.end()||self->slot!=cME.slot||self->name!=cME.name||self->status!=RosterOnline)return;
    }
    cStagedRoster = next;
    cHaveRoster = true;
    if (!cAccepted)
    {
        cBinding = generation;
        return;
    }
    std::vector<RosterChange> changes;
    bool consolidated = false;
    if (ApplyRosterState(cRoster, next, changes, consolidated))
    {
        cUsers.clear();
        for (const auto& row : cRoster.rows)
        {
            UserInfo u;
            u.ID = row.id;
            u.name = row.name;
            u.slot = row.slot;
            u.status = row.status;
            u.lead = row.slot == 0;
            cUsers.push_back(u);
        }
        eStatus |= FLAGS_USERS_LIST_GET | FLAGS_USERS_LIST_UPD;
        if (consolidated)
            Events_Push(new Event(EVENT_USER_LIST, 1, cRoster.session));
        else
            for (const auto& change : changes)
            {
                if (change.row.id == cME.ID)
                    continue;
                auto t = change.transition;
                if (t == Transition::Fresh || t == Transition::Resumed)
                    Events_Push(new EventNameID(EVENT_USER_ADD,
                                                t == Transition::Fresh ? JoinFresh : JoinResumed,
                                                change.row.name,
                                                change.row.id,change.row.slot,cRoster.session));
                else
                    Events_Push(new EventNameID(EVENT_USER_LEAVE,
                                                t == Transition::Away     ? LeaveAway
                                                : t == Transition::Left   ? LeaveLeft
                                                : t == Transition::Kicked ? LeaveKicked
                                                                          : LeaveExpired,
                                                change.row.name,
                                                change.row.id,change.row.slot,cRoster.session));
            }
    }
    CompleteAcceptance();
    ControlWriter w;
    w.U(SYS_MSG_ROSTER_ACK, 1);
    w.U(cAttempt, 8);
    w.U(cBinding, 4);
    w.U(cSession, 8);
    w.U(cRoster.revision, 8);
    auto* bytes = RefDataStatic::create(w.b.data(), w.b.size());
    Send_PushData(new SendingData(cServAddress, 0, bytes, PKT_FLAG_SYSTEM));
}

Pkt*
ZNDClient::Recv_PreparePacket(InRawPkt* pkt)
{
    // _UpdateThread holds eSyncMutex. Drop the owned raw packet before parsing,
    // assembly or SendDelivered can acknowledge a cancelled/finished attempt.
    if (cCancelled || threadsEnd)
    {
        delete pkt;
        return nullptr;
    }
    if (!pkt->Parse())
    {
        // Incorrect packet
        delete pkt;
        return nullptr;
    }

    if (pkt->hdr.flags & PKT_FLAG_SYSTEM) // System message
    {
        return new Pkt(pkt, nullptr);
    }
    else
    {
        if (pkt->hdr.flags & PKT_FLAG_PART) // incomplete data, do assembly
        {
            return Recv_Assemble(pkt,nullptr);
            // if not complete -> return NULL
        }
        else // not parted packet -> make it
        {
            return new Pkt(pkt, nullptr);
        }
    }

    return nullptr;
}

void
ZNDClient::ProcessSystemPkt(Pkt* pkt)
{
    if (cCancelled || !pkt)
        return;

    if (pkt->datasz < 1)
        return;

    MemReader rd(pkt->data + 1, pkt->datasz - 1);

    switch (pkt->data[0])
    {
        case SYS_MSG_CONNECTED:
        {
            if (cAdmissionClosed)
                return;
            ControlReader r(pkt->data, pkt->datasz);
            r.U(1);
            auto lobby = r.U(1);
            auto id = r.U(8);
            auto name = r.Str(20);
            World::NetProtocol::ResumeToken token{};
            for (auto& v : token.b)
                v = r.U(1);
            auto resumed = r.U(1), slot = r.U(1);
            auto grace = r.U(4), attempt = r.U(8);
            auto binding = r.U(4), session = r.U(8);
            auto sessionName = r.Str(32);
            if (!r.End() || lobby || !id || name.empty() || resumed > 1 || slot >= 6 ||
                grace < 100 || grace > 600000 || attempt != cAttempt || !binding || !session ||
                !World::NetProtocol::TokenValid(token))
                return;
            if (cAccepted && (cSession != session || cBinding != binding || cME.ID != id))
                return;
            if (cHaveRoster && (cStagedRoster.session != session || cBinding != binding))
            {
                cHaveRoster = false;
                cStagedRoster = RosterState();
            }
            cAccepted = true;
            cME.ID = id;
            cME.name = name;
            cME.slot = slot;
            cSession = session;
            cBinding = binding;
            cJoinedSessionName = sessionName;
            cWasResumed = resumed;
            resumeGraceMs = grace;
            memcpy(cResumeToken, token.b, 16);
            if (cHaveRoster)
            {
                auto bytes = EncodeRoster(cStagedRoster, cAttempt, cBinding);
                ApplyControl(bytes.data(), bytes.size());
            }
            break;
        }
        case SYS_MSG_DISCONNECT:
        {
            ControlReader r(pkt->data + 1, pkt->datasz - 1);
            auto reason = r.U(1), attempt = r.U(8), generation = r.U(4);
            if (!r.End() || reason > 4 || attempt != cAttempt || generation != cBinding)
                return;
            const uint64_t eventSession = cSession;
            if (reason == 3)
                Events_Push(new Event(EVENT_SESSION_END, 0, eventSession));
            if (reason != 0)
                CancelAttempt();
            else
            {
                eStatus = 0;
                cME.status = NetUser::STATUS_DISCONNECTED;
                threadsEnd = true;
            }
            Events_Push(new Event(EVENT_DISCONNECT, reason, eventSession));
            break;
        }
        case SYS_MSG_SES_JOIN:
            return;
            {
                cLeader = (rd.readU8() == 1);
                cME.sesID = rd.readU64();
                rd.readSzStr(cJoinedSessionName);
                eStatus &= ~(FLAGS_USERS_LIST_GET | FLAGS_USERS_LIST_UPD);
                eStatus |= FLAGS_SESSION_JOINED;

                Events_Push(new EventNameID(EVENT_SESSION_JOIN, 0, cJoinedSessionName, cME.sesID, 0xff, cME.sesID));
            }
            break;

        case SYS_MSG_SES_LEAVE:
        {
            const uint64_t eventSession = cME.sesID;
            cLeader = false;
            cME.sesID = 0;
            eStatus &= ~(FLAGS_SESSION_JOINED | FLAGS_USERS_LIST_GET | FLAGS_USERS_LIST_UPD);

            Events_Push(new Event(EVENT_SESSION_LEAVE, rd.readU8(), eventSession));
        }
        break;

        case SYS_MSG_SES_LEAD:
        {
            if (cME.sesID)
            {
                cLeader = (rd.readU8() == 1);
                Events_Push(new Event(EVENT_SESSION_LEAD, cLeader ? 1 : 0, cME.sesID));
            }
        }
        break;

        case SYS_MSG_PING:
            if (rd.size() == 4)
            {
                uint32_t seq = rd.readU32();
                RefDataWStream* rfdat = RefDataWStream::create();
                rfdat->writeU8(SYS_MSG_PING);
                rfdat->writeU32(seq);

                Send_PushData(new SendingData(cServAddress, 0, rfdat, PKT_FLAG_SYSTEM));

                if (cME.IsOnline())
                    cTimeOut = ttime.GetTicks() + TIMEOUT_USER;
            }
            break;

        case SYS_MSG_DELIVERED:
            if (rd.size() == 4)
            {
                uint32_t seq = rd.readU32();
                ConfirmReceive(AddrSeq(cServAddress, seq));
            }
            break;

        case SYS_MSG_RETRY:
            if (rd.size() == 12)
            {
                uint32_t seq = rd.readU32();
                uint32_t from = rd.readU32();
                uint32_t upto = rd.readU32();
                ConfirmRetry(AddrSeq(cServAddress, seq), from, upto);
            }
            break;

        case SYS_MSG_SES_CLOSE:
        {
            ControlReader r(pkt->data + 1, pkt->datasz - 1);
            auto remaining = r.U(4), attempt = r.U(8), generation = r.U(4);
            if (r.End() && attempt == cAttempt && generation == cBinding && cME.IsOnline())
            {
                cAdmissionClosed = true;
                memset(cResumeToken, 0, 16);
                cResumeOnly = false;
                const uint64_t eventSession = cSession;
                Events_Push(new Event(EVENT_SESSION_END, remaining, eventSession));
                if (!remaining)
                {
                    CancelAttempt();
                    Events_Push(new Event(EVENT_DISCONNECT, 3, eventSession));
                }
            }
            break;
        }

        case SYS_MSG_CONNERR:
            if (cME.status == NetUser::STATUS_CONNECTING && rd.size() == 1)
            {
                uint8_t type = rd.readU8();
                if (type >= ERR_CONN_FULL && type <= ERR_CONN_RANDOM)
                {
                    threadsEnd = true; // Go to
                    Events_Push(new Event(EVENT_CONNERR, type, cSession));
                }
            }
            break;

        default:
            break;
    }
}

void
ZNDClient::ProcessRegularPkt(Pkt* pkt)
{
    if (cCancelled || !pkt)
        return;

    if (pkt->datasz < 1)
        return;

    if (pkt->data[0] == 'U')
    {
        ApplyControl(pkt->data, pkt->datasz);
        return;
    }
    if (pkt->data[0] != USR_MSG_DATA || !cME.IsOnline())
        return;
    MemReader rd(pkt->data + 1, pkt->datasz - 1);

    switch (pkt->data[0])
    {
        case USR_MSG_DATA:
            if (rd.size() > 25)
            {
                auto generation = rd.readU32();
                if (generation != cBinding)
                    return;
                uint64_t from = rd.readU64();
                uint8_t cast = rd.readU8();
                uint64_t to = rd.readU64();
                uint32_t sz = rd.readU32();

                if (rd.size() == sz + rd.tell())
                {
                    if ((cast == 0 && to == cME.ID) || (cast == 1 && to == cME.sesID))
                    {
                        Events_Push(new EventData(EVENT_DATA,
                                                  pkt->flags & (PKT_FLAG_GARANT | PKT_FLAG_ASYNC),
                                                  from,
                                                  (cast == 1 ? true : false),
                                                  to,
                                                  sz,
                                                  pkt->data + 1 + rd.tell(),
                                                  pkt->uchnl, cSession));
                    }
                }
            }
            break;

        default:
            break;
    }
}

void
ZNDClient::Start(const std::string& name, const IPaddress& addr)
{
    if (!threadsEnd)
        return;

    // Do not overwrite handles or sock until the old update worker (which
    // owns recv/send shutdown) has finished, even after timeout or refusal.
    Stop();
    mode = MODE_CLIENT;
    // (restart after Stop without DISCONNECT presents the resume token)
    sock = SDLNet_UDP_Open(0);
    if (!sock)
        return;

    cServAddress = addr;
    cME.name = name;
    cME.status = NetUser::STATUS_CONNECTING;

    eStatus = 0;

    cSessionsReqTimeNext = 0;
    cSessionsMakeRequest = false;

    if (!OsRandom(&cAttempt, sizeof(cAttempt)))
    {
        Stop();
        Events_Push(new Event(EVENT_CONNERR, ERR_CONN_RANDOM));
        return;
    }
    // Stop above joined the previous workers. Only a deliberate new Start may
    // admit packets again; custody retained by ordinary Stop is left intact.
    cCancelled = cAdmissionClosed = false;
    cConnectDeadline = ttime.GetTicks() + TIMEOUT_CONNECT;
    cRetryNext = cConnectDeadline;
    threadsEnd = false;

    cTimeOut = ttime.GetTicks() + TIMEOUT_CONNECT;

    recvThread = SDL_CreateThread(_RecvThread, "", this);
    sendThread = SDL_CreateThread(_SendThread, "", this);
    if (!recvThread || !sendThread)
    {
        Stop();
        return;
    }

    updateThread = SDL_CreateThread(_UpdateThread, "", this);
    if (!updateThread)
    {
        Stop();
        return;
    }

    SDL_LockMutex(eSyncMutex);
    SendConnect();
    SDL_UnlockMutex(eSyncMutex);
}

void
ZNDClient::SendConnect()
{
    // Start and InterprocessUpdate both hold eSyncMutex.
    if (cCancelled || cAdmissionClosed || threadsEnd)
        return;
    ControlWriter w;
    w.U(SYS_MSG_HANDSHAKE, 1);
    w.U(servString.size(), 1);
    w.U(cME.name.size(), 1);
    w.b.insert(w.b.end(), servString.begin(), servString.end());
    w.b.insert(w.b.end(), cME.name.begin(), cME.name.end());
    w.U(World::NetProtocol::ProtoVer, 2);
    w.b.insert(w.b.end(), cResumeToken, cResumeToken + 16);
    w.U(cResumeOnly || World::NetProtocol::TokenValid(
                         *reinterpret_cast<World::NetProtocol::ResumeToken*>(cResumeToken)),
        1);
    w.U(cAttempt, 8);
    auto* bytes = RefDataStatic::create(w.b.data(), w.b.size());
    Send_PushData(new SendingData(cServAddress, 0, bytes, PKT_FLAG_SYSTEM));
    cRetryNext = ttime.GetTicks() + 1000;
}

void
ZNDClient::RequestGamesList()
{
    RefDataWStream* rfdata = RefDataWStream::create();
    rfdata->writeU8(SYS_MSG_LIST_GAMES);
    rfdata->writeU64(cME.ID);

    Send_PushData(new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}

uint8_t
ZNDClient::GetStatus()
{
    return cME.status;
}

bool
ZNDClient::GetSessions(SessionInfoVect& dst)
{
    if (!cME.IsOnline())
        return false;

    if (!(eStatus & FLAGS_SESSIONS_LIST_GET))
        return false;

    bool res = false;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        if (eStatus & FLAGS_SESSIONS_LIST_GET)
        {
            eStatus &= (~FLAGS_SESSIONS_LIST_UPD);
            dst = cSessions;
            res = true;
        }
        SDL_UnlockMutex(eSyncMutex);
    }

    return res;
}

void
ZNDClient::CreateSession(const std::string& name, const std::string& pass, uint32_t max_players)
{
    if (!cME.IsOnline())
        return;

    if (!SessionCheckName(name) || (pass.size() > 0 && !SessionCheckPswd(pass)))
        return;

    RefDataWStream* rfdata = RefDataWStream::create();
    rfdata->writeU8(SYS_MSG_SES_CREATE);
    rfdata->writeSzStr(name);
    rfdata->writeU32(max_players);
    rfdata->writeSzStr(pass);

    Send_PushData(new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}

void
ZNDClient::JoinSession(uint64_t SID, const std::string& pass)
{
    if (!cME.IsOnline())
        return;

    RefDataWStream* rfdata = RefDataWStream::create();
    rfdata->writeU8(SYS_MSG_SES_JOIN);
    rfdata->writeU64(SID);
    rfdata->writeSzStr(pass);

    Send_PushData(new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}

bool
ZNDClient::GetUsers(UserInfoVect& dst)
{
    if (!cME.IsOnline())
        return false;

    if (!(eStatus & FLAGS_USERS_LIST_GET))
        return false;

    bool res = false;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        if (eStatus & FLAGS_USERS_LIST_GET)
        {
            eStatus &= (~FLAGS_USERS_LIST_UPD);
            dst = cUsers;
            res = true;
        }
        SDL_UnlockMutex(eSyncMutex);
    }

    return res;
}

bool
ZNDClient::GetUser(UserInfo& dst, const char* name)
{
    if (!cME.IsOnline() || !(eStatus & FLAGS_USERS_LIST_GET))
        return false;

    bool res = false;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        if (eStatus & FLAGS_USERS_LIST_GET)
        {
            for (UserInfoVect::iterator it = cUsers.begin(); it != cUsers.end(); it++)
            {
                if (strcmp(it->name.c_str(), name) == 0)
                {
                    dst = *it;
                    res = true;
                    break;
                }
            }
        }
        SDL_UnlockMutex(eSyncMutex);
    }

    return res;
}

bool
ZNDClient::GetUser(UserInfo& dst, uint64_t _ID)
{
    if (!cME.IsOnline() || !(eStatus & FLAGS_USERS_LIST_GET))
        return false;

    bool res = false;

    if (SDL_LockMutex(eSyncMutex) == 0)
    {
        if (eStatus & FLAGS_USERS_LIST_GET)
        {
            for (UserInfoVect::iterator it = cUsers.begin(); it != cUsers.end(); it++)
            {
                if (it->ID == _ID)
                {
                    dst = *it;
                    res = true;
                    break;
                }
            }
        }
        SDL_UnlockMutex(eSyncMutex);
    }

    return res;
}

void
ZNDClient::InterprocessUpdate()
{
    if (cCancelled || threadsEnd)
        return;
    uint64_t curTime = ttime.GetTicks();

    if (!cAdmissionClosed && !cME.IsOnline() && cME.status == NetUser::STATUS_CONNECTING && curTime >= cRetryNext)
        SendConnect();
    if (curTime > (cME.status == NetUser::STATUS_CONNECTING ? cConnectDeadline : cTimeOut))
    {
        threadsEnd = true;

        if (cME.status == NetUser::STATUS_CONNECTING)
            Events_Push(new Event(EVENT_CONNERR, ERR_CONN_TIMEOUT, cSession));
        else
            Events_Push(new Event(EVENT_DISCONNECT, ERR_DISCONNECT_TIMEOUT, cSession));

        return;
    }

    if (cSessionsMakeRequest && cSessionsReqTimeNext < curTime)
    {
        cSessionsMakeRequest = false;
        cSessionsReqTimeNext = curTime + DELAY_SESS_REQ;

        RequestGamesList();
    }

    PendingCheck(); // Delete timeout packets
    ConfirmQueueCheck();
}

void
ZNDClient::CancelAttempt()
{
    // Serialized with packet admission and retries by eSyncMutex. Keep Stop's
    // join/socket ownership unchanged: this only asks the workers to finish.
    Send_Clear(); // No queued handshake may follow server terminal intent.
    cCancelled = cAdmissionClosed = true;
    cAttempt = cSession = 0;
    cBinding = 0;
    cAccepted = cHaveRoster = cWasResumed = false;
    cRoster = RosterState();
    cStagedRoster = RosterState();
    cUsers.clear();
    eStatus = 0;
    memset(cResumeToken, 0, 16);
    cResumeOnly = false;
    // Natural-stop cleanup writes status outside this mutex after threadsEnd.
    // Do not race that cleanup or use its socket once shutdown has begun.
    if (!threadsEnd)
        cME.status = NetUser::STATUS_DISCONNECTED;
    threadsEnd = true;
}

void
ZNDClient::SendDisconnect()
{
    SDL_LockMutex(eSyncMutex);
    Send_Clear();
    if (!threadsEnd && sock && cAccepted)
    {
        ControlWriter w;
        w.U(SYS_MSG_DISCONNECT, 1);
        w.U(cAttempt, 8);
        w.U(cBinding, 4);
        // Send terminal intent synchronously so an immediately following Stop
        // cannot clear the queued goodbye before the send worker sees it.
        std::vector<uint8_t> raw(1 + w.b.size());
        raw[0] = PKT_FLAG_SYSTEM;
        std::copy(w.b.begin(), w.b.end(), raw.begin() + 1);
        UDPpacket packet{};
        packet.address = cServAddress;
        packet.data = raw.data();
        packet.len = raw.size();
        packet.maxlen = packet.len;
        packet.channel = -1;
        SDLNet_UDP_Send(sock, -1, &packet);

    }
    CancelAttempt();
    SDL_UnlockMutex(eSyncMutex);
}

void
ZNDClient::ShowSession(bool show)
{
    if (!cME.IsOnline() || !(eStatus & FLAGS_SESSION_JOINED) || !cLeader)
        return;

    RefDataWStream* rfdata = RefDataWStream::create(4);
    rfdata->writeU8(SYS_MSG_SES_SHOW);
    rfdata->writeU8(show ? 1 : 0);
    Send_PushData(new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}

void
ZNDClient::CloseSession(uint32_t closeTime)
{
    if (!cME.IsOnline() || !(eStatus & FLAGS_SESSION_JOINED) || !cLeader)
        return;

    if (closeTime > 60000)
        closeTime = 60000;

    RefDataWStream* rfdata = RefDataWStream::create(8);
    rfdata->writeU8(SYS_MSG_SES_CLOSE);
    rfdata->writeU32(closeTime);
    Send_PushData(new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}

void
ZNDClient::KickUser(uint64_t _ID)
{
    if (!cME.IsOnline() || !(eStatus & FLAGS_SESSION_JOINED) || !cLeader)
        return;

    RefDataWStream* rfdata = RefDataWStream::create(16);
    rfdata->writeU8(SYS_MSG_SES_KICK);
    rfdata->writeU64(_ID);
    Send_PushData(new SendingData(cServAddress, 0, rfdata, PKT_FLAG_SYSTEM));
}

void
ZNDClient::SendData(uint64_t to, void* data, uint32_t sz, uint8_t flags, uint8_t channel)
{
    if (!data || !sz || !cME.IsOnline() || !(eStatus & FLAGS_SESSION_JOINED))
        return;

    RefData* rfdat = USRDataGenData(cME.ID, false, to, data, sz, cBinding);

    flags &= (PKT_FLAG_GARANT | PKT_FLAG_ASYNC);

    if (channel >= ZNDNET_USER_SCHNLS)
        channel = ZNDNET_USER_SCHNLS - 1;

    SendingData* snd = new SendingData(cServAddress, cME.GetSeq(), rfdat, flags);
    snd->SetChannel(0, channel);

    Send_PushData(snd);
}

void
ZNDClient::BroadcastData(void* data, uint32_t sz, uint8_t flags, uint8_t channel)
{
    if (!data || !sz || !cME.IsOnline() || !(eStatus & FLAGS_SESSION_JOINED))
        return;

    RefData* rfdat = USRDataGenData(cME.ID, true, cME.sesID, data, sz, cBinding);

    flags &= (PKT_FLAG_GARANT | PKT_FLAG_ASYNC);

    if (channel >= ZNDNET_USER_SCHNLS)
        channel = ZNDNET_USER_SCHNLS - 1;

    SendingData* snd = new SendingData(cServAddress, cME.GetSeq(), rfdat, flags);
    snd->SetChannel(0, channel);

    Send_PushData(snd);
}

uint64_t
ZNDClient::GetMyID()
{
    if (!cME.IsOnline())
        return 0;
    return cME.ID;
}

std::string
ZNDClient::GetMyName()
{
    if (!cME.IsOnline())
        return std::string();
    return cME.name;
}

uint64_t
ZNDClient::GetMySesID()
{
    if (!cME.IsOnline())
        return 0;
    return cME.sesID;
}

}
