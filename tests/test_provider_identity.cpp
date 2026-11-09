// Actual production provider methods; fake transport/clock boundary only.
// Each CTest invocation runs one existing scenario in a fresh process.
#include "provider_identity_platform.hpp"
#include "platform/windp_resume.h"
#include "platform/windp_roster.h"
#include "platform/windp_events.h"
#include "netlib/events.h"
#include "provider_identity_transport.hpp"
#include "provider_identity_production.inc"
#include <cstdio>
#include "require.h"

namespace ProviderFixture {
static std::string transport;
static void Environment() {
#ifdef _WIN32
    _putenv_s("OPENNEOUA_TRANSPORT",transport.c_str());
#else
    setenv("OPENNEOUA_TRANSPORT",transport.c_str(),1);
#endif
    now=10000; resolveFails=false; configureFails=false; nextStart=StartSpec{};
    nextHostId=10; nextHostSession=100;
    resolvedEndpoint=IPaddress{0x04030201u,0x32efu}; ClearClockCounters(); traces.clear();
}
struct Fixture {
    NC_STACK_windp p;
    Fixture(bool connect=true) {
        REQUIRE(p.init()); REQUIRE(p.SetMode(false)==1); p.SetWantedName("FIVE");
        if(connect) {
            REQUIRE(p.Connect("host.example"));
            REQUIRE(p.GetMyIndex()==5); REQUIRE(p.GetPlayerCount()==3);
            REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Connected);
        }
    }
    ~Fixture() { p.Reset(nullptr); std::free(p.norm_block); p.norm_block=nullptr; }
};
static windp_recvMsg Receive(NC_STACK_windp &p) {
    windp_recvMsg result; REQUIRE(p.Recv(&result)); return result;
}
static void DrainTo(NC_STACK_windp &p,uint32_t wanted) {
    for(unsigned i=0;i<12;++i) if(Receive(p).msgType==wanted) return;
    Require(false,"expected provider event was not emitted within bounded drain");
}
static void Lose(NC_STACK_windp &p,uint32_t reason=ZNDNet::ERR_DISCONNECT_TIMEOUT) {
    p.zcon->status=ZNDNet::NetUser::STATUS_DISCONNECTED;
    p.zcon->Push(new ZNDNet::Event(ZNDNet::EVENT_DISCONNECT,reason));
}
static void CheckBlocked(NC_STACK_windp &p) {
    const auto before=SendCalls(); uint8_t bytes[]={0x9a,0x9b};
    REQUIRE(!p.Broadcast(bytes,sizeof bytes,true));
    REQUIRE(p.QueueBroadcast(bytes,sizeof bytes,false)==0);
    REQUIRE(p.BroadcastLowLevel(bytes,sizeof bytes,true)==0);
    REQUIRE(!p.Send(bytes,sizeof bytes,"HOST",true));
    REQUIRE(p.SendLowLevel(bytes,sizeof bytes,"HOST",false)==0);
    REQUIRE(p.FlushBroadcastBuffer()==0);
    REQUIRE(p.broadcastBuff_off==0); REQUIRE(SendCalls()==before);
}
static void Barrier() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    uint8_t oldBytes[]={1,2,3}, newBytes[]={7,8};
    REQUIRE(p.QueueBroadcast(oldBytes,sizeof oldBytes,true)==sizeof oldBytes);
    REQUIRE(p.SendLowLevel(oldBytes,sizeof oldBytes,"HOST",true)==sizeof oldBytes);
    auto old=p.zcon->trace;
    const auto oldToken=p.zcon->token;
    Lose(p);
    // Status already says disconnected before provider consumes the event.
    CheckBlocked(p);
    REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::WaitingRetry);
    REQUIRE(p.zcon->outgoing.empty()); REQUIRE(old->disconnects==1); // initial Connect only
    REQUIRE(World::NetProtocol::TokenEqual(oldToken,p.zcon->token));
    const auto saved=p._recovery.Saved();
    ClearClockCounters(); resolvedEndpoint={0xdeadbeefu,0xbeefu};
    nextStart.mode=StartMode::Silent;
    windp_recvMsg empty; REQUIRE(!p.Recv(&empty));
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Connecting);
    REQUIRE(old->destroyed==1); REQUIRE(p.zcon->trace!=old);
    REQUIRE(p.zcon->trace->starts.size()==1);
    const auto started=p.zcon->trace->starts[0];
    REQUIRE(started.address.host==saved.addressHost && started.address.port==saved.addressPort);
    REQUIRE(started.name==saved.name && started.resumeOnly);
    REQUIRE(World::NetProtocol::TokenEqual(started.token,saved.token));
    REQUIRE(p.zcon->graceMs==saved.graceMs);
    REQUIRE(resolveCalls==0 && delayCalls==0);
    CheckBlocked(p); REQUIRE(!p.CompleteRecovery());
    p.zcon->Accept(StartSpec{});
    DrainTo(p,RECVMSG_SELFRESUMED);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::AwaitingReclaim);
    CheckBlocked(p); REQUIRE(p.GetMyIndex()==5);
    REQUIRE(p.CompleteRecovery());
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Connected);
    REQUIRE(p.FlushBroadcastBuffer()==0); REQUIRE(p.zcon->trace->sends.empty());
    REQUIRE(p.Send(newBytes,sizeof newBytes,"HOST",true));
    REQUIRE(p.zcon->trace->sends.size()==1);
    REQUIRE(p.zcon->trace->sends[0].bytes==std::vector<uint8_t>({7,8}));
}
static void Fresh() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    p.zcon->token.b[1]=0x77;
    uint8_t bytes[]={2,3}; REQUIRE(p.Broadcast(bytes,sizeof bytes,true));
    Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    auto trace=p.zcon->trace; const auto clears=trace->tokenClears;
    p._recoveryFailurePending=true;
    nextStart.id=40; nextStart.session=200;
    resolvedEndpoint={0x88776655u,0x4433u};
    REQUIRE(p.Connect("new.example"));
    REQUIRE(trace->starts.size()==2);
    REQUIRE(!trace->starts.back().resumeOnly);
    REQUIRE(!World::NetProtocol::TokenValid(trace->starts.back().token));
    REQUIRE(trace->tokenClears==clears+1);
    REQUIRE(p._myID==40 && p._sesID==200 && p.GetMyIndex()==5);
    REQUIRE(p._recovery.Saved().addressHost==0x88776655u);
    REQUIRE(p._recovery.Saved().id==40 && p._recovery.Saved().session==200);
    REQUIRE(!p._recovery.Enabled() && !p._recoveryFailurePending);
    REQUIRE(p.broadcastBuff_off==0 && p.zcon->outgoing.empty());
}
static void EventMapping() {
    Fixture f; auto &p=f.p;
    // Latest roster reuses slot 2 before the queued old occupant leave is read.
    p.zcon->rows[1]={99,"REUSED",false,2,ZNDNet::RosterOnline};
    p.zcon->Push(new ZNDNet::EventNameID(ZNDNet::EVENT_USER_LEAVE,ZNDNet::LeaveLeft,"TWO",20,2));
    auto event=Receive(p);
    REQUIRE(event.msgType==RECVMSG_PLAYERGONE && event.playerId==20 && event.playerSlot==2);
    REQUIRE(event.senderName=="TWO" && p.GetPlayerName(2)=="REUSED");
    p.zcon->rows[2].status=ZNDNet::RosterAway;
    p.zcon->Push(new ZNDNet::EventNameID(ZNDNet::EVENT_USER_LEAVE,ZNDNet::LeaveAway,"FIVE",30,5));
    event=Receive(p); REQUIRE(event.msgType==RECVMSG_PLAYERAWAY && event.playerSlot==5);
    TDPPlayerData row; REQUIRE(p.GetPlayerData(5,&row)); REQUIRE(!row.IsOnline());
    REQUIRE(p.GetMyIndex()==5 && p.GetPlayerCount()==3);
    p.zcon->rows[2].status=ZNDNet::RosterOnline;
    p.zcon->Push(new ZNDNet::EventNameID(ZNDNet::EVENT_USER_ADD,ZNDNet::JoinResumed,"FIVE",30,5));
    event=Receive(p); REQUIRE(event.msgType==RECVMSG_PLAYERBACK && event.playerId==30 && event.playerSlot==5);
    REQUIRE(p.GetPlayerData(5,&row) && row.IsOnline());
    for(auto why:{ZNDNet::LeaveLeft,ZNDNet::LeaveKicked,ZNDNet::LeaveExpired}) {
        p.zcon->Push(new ZNDNet::EventNameID(ZNDNet::EVENT_USER_LEAVE,why,"REUSED",99,2));
        event=Receive(p); REQUIRE(event.msgType==RECVMSG_PLAYERGONE && event.reason==uint32_t(why));
    }
    p.zcon->Push(new ZNDNet::EventNameID(ZNDNet::EVENT_USER_ADD,ZNDNet::JoinFresh,"NEW",100,4));
    event=Receive(p); REQUIRE(event.msgType==RECVMSG_CREATEPLAYER && event.playerSlot==4);
    p.zcon->Push(new ZNDNet::Event(ZNDNet::EVENT_USER_LIST,1));
    event=Receive(p); REQUIRE(event.msgType==RECVMSG_ROSTERRESET);
    REQUIRE(event.playerId==0 && event.playerSlot==-1 && event.senderName.empty());
}
static void Malformed() {
    Fixture f; auto &p=f.p;
    for(const auto &spec:std::vector<std::pair<uint64_t,uint8_t>>{{0,2},{20,6},{20,255}}) {
        for(auto kind:{ZNDNet::EVENT_USER_ADD,ZNDNet::EVENT_USER_LEAVE}) {
            p.zcon->Push(new ZNDNet::EventNameID(kind,0,"TWO",spec.first,spec.second));
            REQUIRE(Receive(p).msgType==RECVMSG_SYSUNK);
        }
    }
    p.zcon->rows[2].slot=2;
    p.zcon->Push(new ZNDNet::Event(ZNDNet::EVENT_USER_LIST,1));
    // A malformed complete view cannot authorize a consolidated game reset.
    REQUIRE(Receive(p).msgType==RECVMSG_SYSUNK);
    REQUIRE(p.GetPlayerCount()==0 && p.GetMyIndex()==-1);
}
static void MetadataReset() {
    Fixture f; auto &p=f.p; uint8_t bytes[]={8,9};
    p.zcon->Push(new ZNDNet::EventData(ZNDNet::EVENT_DATA,0,20,true,30,sizeof bytes,bytes,0));
    windp_recvMsg event; REQUIRE(p.Recv(&event));
    REQUIRE(event.msgType==RECVMSG_NORMAL && event.size==2 && event.playerId==20);
    REQUIRE(event.data==event._data.data() && event._data==std::vector<uint8_t>({8,9}));
    REQUIRE(!p.Recv(&event));
    REQUIRE(event.msgType==RECVMSG_NONE && event.data==nullptr && event.size==0 && event._data.empty());
    REQUIRE(event.playerId==0 && event.playerSlot==-1 && event.reason==0 && event.senderName.empty());
    REQUIRE(!p.Recv(nullptr));
}
static void Terminal(uint32_t reason) {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    Lose(p,reason); const auto event=Receive(p);
    REQUIRE(event.msgType==RECVMSG_RECOVERYFAILED && event.reason==reason);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed);
    REQUIRE(!World::NetProtocol::TokenValid(p.zcon->token));
    REQUIRE(!p._recovery.Saved().Valid()); CheckBlocked(p);
    const auto count=traces.size(); now+=100000;
    windp_recvMsg empty; REQUIRE(!p.Recv(&empty)); REQUIRE(traces.size()==count);
}
static void Unarmed() {
    Fixture f; auto &p=f.p; Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_RECOVERYFAILED);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed); CheckBlocked(p);
}
static void SessionEnd() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    p.zcon->Push(new ZNDNet::Event(ZNDNet::EVENT_SESSION_END,44));
    p.zcon->Push(new ZNDNet::Event(ZNDNet::EVENT_DISCONNECT,3));
    auto event=Receive(p); REQUIRE(event.msgType==RECVMSG_SESSIONEND && event.reason==44);
    REQUIRE(p._recovery.Failure()==ZNDNet::CONNFAIL_CLOSED);
    REQUIRE(p.zcon->queue.empty()); REQUIRE(!World::NetProtocol::TokenValid(p.zcon->token));
    windp_recvMsg empty; REQUIRE(!p.Recv(&empty)); CheckBlocked(p);
}
static void Timeout() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    const auto lossAt=now; Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    nextStart.mode=StartMode::Silent; ClearClockCounters();
    windp_recvMsg event; REQUIRE(!p.Recv(&event));
    const auto attempt=p.zcon->trace; now+=5000;
    REQUIRE(!p.Recv(&event)); REQUIRE(!p.zcon->running);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::WaitingRetry);
    now+=999; REQUIRE(!p.Recv(&event)); REQUIRE(attempt->destroyed==0);
    now+=1; REQUIRE(!p.Recv(&event)); REQUIRE(attempt->destroyed==1);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Connecting);
    now=lossAt+60000; REQUIRE(p.Recv(&event));
    REQUIRE(event.msgType==RECVMSG_RECOVERYFAILED && event.reason==ZNDNet::CONNFAIL_NO_ANSWER);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed);
    REQUIRE(!p.Recv(&event)); REQUIRE(resolveCalls==0 && delayCalls==0); CheckBlocked(p);
}
static void Refusal(bool final) {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    nextStart.mode=StartMode::Refuse;
    nextStart.refusal=final?ZNDNet::ERR_CONN_RESUME:ZNDNet::ERR_CONN_FULL;
    const auto event=Receive(p);
    if(final) {
        REQUIRE(event.msgType==RECVMSG_RECOVERYFAILED && event.reason==ZNDNet::CONNFAIL_RESUME);
        REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed);
        REQUIRE(!World::NetProtocol::TokenValid(p.zcon->token));
    } else {
        REQUIRE(event.msgType==RECVMSG_NONE);
        REQUIRE(p.RecoveryPhase()==WindpResume::Phase::WaitingRetry);
        REQUIRE(World::NetProtocol::TokenValid(p.zcon->token));
        nextStart=StartSpec{}; now+=1000; DrainTo(p,RECVMSG_SELFRESUMED);
        REQUIRE(p.RecoveryPhase()==WindpResume::Phase::AwaitingReclaim);
    }
    CheckBlocked(p);
}
static void WrongIdentity(bool session) {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    if(session) nextStart.session=999; else nextStart.id=999;
    DrainTo(p,RECVMSG_RECOVERYFAILED);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed);
    REQUIRE(!World::NetProtocol::TokenValid(p.zcon->token)); CheckBlocked(p);
}
static void Disable() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    p.SetRecoveryEnabled(false);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed);
    REQUIRE(Receive(p).msgType==RECVMSG_RECOVERYFAILED);
    windp_recvMsg event; REQUIRE(!p.Recv(&event)); CheckBlocked(p);
}
static void ConfigureFailure() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    configureFails=true; const auto event=Receive(p);
    REQUIRE(event.msgType==RECVMSG_RECOVERYFAILED && event.reason==ZNDNet::CONNFAIL_RESUME);
    REQUIRE(p.zcon->trace->starts.empty()); REQUIRE(!World::NetProtocol::TokenValid(p.zcon->token));
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed); CheckBlocked(p);
}
static void RepeatLoss() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    const auto lossAt=now; Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    now+=30000; DrainTo(p,RECVMSG_SELFRESUMED); CheckBlocked(p);
    Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    now=lossAt+60000;
    REQUIRE(Receive(p).msgType==RECVMSG_RECOVERYFAILED);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed);
}
static void EnableAgain(bool connecting) {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    if(connecting) {
        nextStart.mode=StartMode::Silent;
        windp_recvMsg empty; REQUIRE(!p.Recv(&empty));
        REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Connecting);
    }
    p.SetRecoveryEnabled(true);
    // Complete the ordinary recovery acceptance, then lose transport once more
    // before reclaim. Repeated enable must not silently revoke retry authority.
    if(connecting) p.zcon->Accept(StartSpec{});
    DrainTo(p,RECVMSG_SELFRESUMED);
    Lose(p);
    const auto secondLoss=Receive(p);
    Require(secondLoss.msgType==RECVMSG_SELFLOST,
            "repeated SetRecoveryEnabled(true) silently disarmed subsequent recoverable loss");
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::WaitingRetry);
    REQUIRE(p._recovery.Saved().Valid()); CheckBlocked(p);
}
static void LateReclaim() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    const auto lossAt=now;
    Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    DrainTo(p,RECVMSG_SELFRESUMED);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::AwaitingReclaim);
    now=lossAt+60001;
    // No intervening Recv: completion itself must observe the fixed deadline.
    Require(!p.CompleteRecovery(),"CompleteRecovery reopened gameplay beyond the fixed total grace");
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed); CheckBlocked(p);
    REQUIRE(Receive(p).msgType==RECVMSG_RECOVERYFAILED);
}
static void FreshFailure(bool malformed) {
    Fixture f(false); auto &p=f.p;
    nextStart.malformedRoster=malformed; nextStart.noToken=!malformed;
    REQUIRE(!p.Connect("host.example"));
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Failed);
    REQUIRE(!World::NetProtocol::TokenValid(p.zcon->token)); CheckBlocked(p);
}
static void Reset() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true);
    Lose(p); REQUIRE(Receive(p).msgType==RECVMSG_SELFLOST);
    const auto before=traces.size(); p.Reset(nullptr);
    REQUIRE(!p.zcon && !p.zcli && !p.zenet && !p.zhost);
    REQUIRE(p.GetMode()==0 && p.GetPlayerCount()==0 && p.GetMyIndex()==-1);
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::Idle);
    REQUIRE(!p._recovery.Saved().Valid()); now+=100000;
    windp_recvMsg empty; REQUIRE(!p.Recv(&empty)); REQUIRE(traces.size()==before); CheckBlocked(p);
}
static void Host() {
    Fixture f(false); auto &p=f.p;
    REQUIRE(p.SetMode(true)==1); REQUIRE(p.Host("host-session",6));
    uint8_t bytes[]={3}; REQUIRE(p.Send(bytes,sizeof bytes,"TWO",true));
    REQUIRE(p.Broadcast(bytes,sizeof bytes,true)); REQUIRE(p.FlushBroadcastBuffer()==1);
    int lock=1; REQUIRE(p.LockSession(&lock)==1); lock=0; REQUIRE(p.LockSession(&lock)==1);
    REQUIRE(p.zcon->trace->showSettings==std::vector<bool>({false,true}));
    REQUIRE(p.LockSession(nullptr)==0);
}
}
namespace ProviderFixture {
struct IdentityCopy { bool valid=false; uint64_t session=0, own=0, host=0; };
template<class P> auto Identity(P &p,int) -> decltype(p._sessionIdentity,IdentityCopy{}) {
    auto copy=p._sessionIdentity;
    const bool valid=p.GetSessionIdentity(&copy);
    return {valid,copy.sessionId,copy.ownPlayerId,copy.hostPlayerId};
}
template<class P> IdentityCopy Identity(P &,long) { return {}; }
template<class P> auto Ready(const P &p,int) -> decltype(p.HasValidRoster()) { return p.HasValidRoster(); }
template<class P> bool Ready(const P &,long) { return false; }
template<class E> auto Context(const E &e,int) -> decltype(e.sessionId) { return e.sessionId; }
template<class E> uint64_t Context(const E &,long) { return 0; }
template<class P> auto SendID(P &p,void *data,size_t size,uint64_t id,int)
    -> decltype(p.SendToPlayer(data,size,id,true)) { return p.SendToPlayer(data,size,id,true); }
template<class P> bool SendID(P &,void *,size_t,uint64_t,long) { return false; }
template<class P> auto KickID(P &p,uint64_t id,int) -> decltype(p.KickPlayer(id)) { return p.KickPlayer(id); }
template<class P> bool KickID(P &,uint64_t,long) { return false; }
static void RequireIdentity(NC_STACK_windp &p,uint64_t session,uint64_t own,uint64_t host) {
    const auto got=Identity(p,0);
    Require(got.valid && got.session==session && got.own==own && got.host==host,
            "provider did not expose the validated immutable session/self/host tuple");
}
static void Inject(NC_STACK_windp &p,ZNDNet::Event *event,uint64_t session) {
    p.zcon->queue.push_back(WithContext(event,session));
}
static void IdentityClient() {
    Fixture f; RequireIdentity(f.p,100,30,10);
    Require(Ready(f.p,0),"complete current validated roster was not exposed as ready");
    REQUIRE(f.p.GetPlayerCount()==3);
}
static void IdentityHost() {
    Fixture f(false); auto &p=f.p; REQUIRE(p.SetMode(true)==1); REQUIRE(p.Host("session",6));
    RequireIdentity(p,100,10,10); REQUIRE(p._sesID==100); REQUIRE(Ready(p,0));
}
static void DataContext() {
    Fixture f; auto &p=f.p; uint8_t bytes[]={4};
    for(bool broadcast:{false,true}) {
        Inject(p,new ZNDNet::EventData(ZNDNet::EVENT_DATA,0,20,broadcast,broadcast?100:30,1,bytes,0),100);
        const auto event=Receive(p);
        Require(event.msgType==RECVMSG_NORMAL && Context(event,0)==100 && event.playerSlot==2,
                "normal data lost its event session or immutable sender slot");
    }
}
static void LifecycleContext() {
    Fixture f; auto &p=f.p;
    p.zcon->rows[1]={99,"REUSED",false,2,ZNDNet::RosterOnline};
    Inject(p,new ZNDNet::EventNameID(ZNDNet::EVENT_USER_LEAVE,ZNDNet::LeaveLeft,"TWO",20,2),100);
    const auto event=Receive(p);
    Require(event.msgType==RECVMSG_PLAYERGONE && Context(event,0)==100 && event.playerId==20 && event.playerSlot==2,
            "slot reuse replaced the old lifecycle event identity/session");
    uint8_t bytes[]={4};
    Inject(p,new ZNDNet::EventData(ZNDNet::EVENT_DATA,0,20,false,30,1,bytes,0),100);
    Require(Receive(p).msgType==RECVMSG_SYSUNK,"old player data was attributed to a reused slot");
}
static void StaleSession(bool terminal,bool absent) {
    Fixture f; auto &p=f.p; uint8_t bytes[]={4};
    ZNDNet::Event *event=terminal ? static_cast<ZNDNet::Event*>(new ZNDNet::Event(ZNDNet::EVENT_SESSION_END,0))
        : static_cast<ZNDNet::Event*>(new ZNDNet::EventData(ZNDNet::EVENT_DATA,0,20,false,30,1,bytes,0));
    const uint64_t old=absent?0:100;
    nextStart.session=200; REQUIRE(p.Connect("new.example"));
    Inject(p,event,old);
    const auto received=Receive(p);
    Require(received.msgType==RECVMSG_SYSUNK && Context(received,0)==old,
            "stale/absent event session was relabeled or admitted in the new session");
    REQUIRE(p.IsConnected()); RequireIdentity(p,200,30,10);
}
static void ConflictingIdentity(unsigned field) {
    Fixture f; auto &p=f.p;
    if(field==0) p.zcon->rows[0].ID=99;
    if(field==1) p.zcon->session=999;
    if(field==2) {p.zcon->id=999;p.zcon->rows.back().ID=999;}
    Inject(p,new ZNDNet::Event(ZNDNet::EVENT_USER_LIST,1),field==1?999:100);
    Receive(p);
    Require(p.GetPlayerCount()==0,"contradictory session/self/host identity left an admissible roster");
    Require(!Identity(p,0).valid && !Ready(p,0),"contradictory validated identity was silently rebound");
    p.zcon->session=100;p.zcon->id=30;p.zcon->rows=Rows();
    Inject(p,new ZNDNet::Event(ZNDNet::EVENT_USER_LIST,1),100); Receive(p);
    Require(!Identity(p,0).valid,"identity rejection was not latched until explicit fresh reset");
    REQUIRE(p.Connect("fresh.example")); RequireIdentity(p,100,30,10);
}
static void RosterUnavailable() {
    Fixture f;auto &p=f.p;p.zcon->failGetUsers=true;
    Inject(p,new ZNDNet::Event(ZNDNet::EVENT_USER_LIST,1),100);Receive(p);
    Require(!Ready(p,0) && p.GetPlayerCount()==0,"failed roster read reused the previous complete view");
    RequireIdentity(p,100,30,10);
    p.zcon->failGetUsers=false;
    Inject(p,new ZNDNet::Event(ZNDNet::EVENT_USER_LIST,1),100);Receive(p);
    REQUIRE(Ready(p,0)); RequireIdentity(p,100,30,10);
}
static void IDSendAndKick() {
    Fixture f;auto &p=f.p;uint8_t bytes[]={0x34};
    Require(SendID(p,bytes,1,20,0),"ID-directed send API did not route to current peer");
    REQUIRE(p.zcon->trace->sends.back().to==20);
    REQUIRE(!SendID(p,bytes,1,0,0)); REQUIRE(!SendID(p,bytes,1,999,0));
    REQUIRE(!KickID(p,20,0));
    p.zcon->rows[1].status=ZNDNet::RosterAway;
    Inject(p,new ZNDNet::Event(ZNDNet::EVENT_USER_LIST,1),100);Receive(p);
    REQUIRE(!SendID(p,bytes,1,20,0));
    REQUIRE(p.SetMode(true)==1); REQUIRE(p.Host("session",6));
    Require(KickID(p,20,0),"network host could not revoke current peer by ID");
    REQUIRE(p.zcon->trace->kicks==std::vector<uint64_t>({20}));
    REQUIRE(!KickID(p,10,0)); REQUIRE(!KickID(p,0,0)); REQUIRE(!KickID(p,999,0));
    REQUIRE(p.zcon->trace->kicks.size()==1);
}
static void RetainDuringLoss() {
    Fixture f;auto &p=f.p;p.SetRecoveryEnabled(true);
    Lose(p);const auto event=Receive(p);
    Require(event.msgType==RECVMSG_SELFLOST && Context(event,0)==100,"self loss discarded its old session context");
    RequireIdentity(p,100,30,10); REQUIRE(!Ready(p,0));
    REQUIRE(p.zcon->GetMyID()==0 && p.zcon->GetMySesID()==0);
    CheckBlocked(p);
}
static void InvalidLifecycle(bool contradiction) {
    Fixture f; auto &p=f.p;
    if(contradiction) p.zcon->rows[0].ID=99;
    else p.zcon->rows[0].lead=false;
    Inject(p,new ZNDNet::EventNameID(ZNDNet::EVENT_USER_ADD,ZNDNet::JoinFresh,"TWO",20,2),100);
    Require(Receive(p).msgType==RECVMSG_SYSUNK,
            "invalid or contradictory roster emitted an authoritative lifecycle event");
    REQUIRE(!Ready(p,0)); CheckBlocked(p);
}
static void InvalidReset() {
    Fixture f; auto &p=f.p; p.zcon->rows[0].lead=false;
    Inject(p,new ZNDNet::Event(ZNDNet::EVENT_USER_LIST,1),100);
    Require(Receive(p).msgType==RECVMSG_SYSUNK,
            "invalid roster emitted an authoritative consolidated reset");
    REQUIRE(!Ready(p,0)); RequireIdentity(p,100,30,10);
}
static void InvalidReclaim() {
    Fixture f; auto &p=f.p; p.SetRecoveryEnabled(true); Lose(p); Receive(p);
    DrainTo(p,RECVMSG_SELFRESUMED);
    Require(p.RecoveryPhase()==WindpResume::Phase::AwaitingReclaim,"reclaim positive control did not reach accepted state");
    p.zcon->rows[0].lead=false;
    Inject(p,new ZNDNet::Event(ZNDNet::EVENT_USER_LIST,1),100); Receive(p);
    Require(!p.CompleteRecovery(),"reclaim completed without a valid current membership view");
    REQUIRE(p.RecoveryPhase()==WindpResume::Phase::AwaitingReclaim); CheckBlocked(p);
}
static void UnadmittedRefusalWhileConnected() {
    Fixture f; auto &p=f.p;
    Inject(p,new ZNDNet::Event(ZNDNet::EVENT_CONNERR,ZNDNet::ERR_CONN_FULL),0);
    Require(Receive(p).msgType==RECVMSG_SYSUNK,
            "zero-context connection refusal terminated an already admitted session");
    REQUIRE(p.IsConnected()); REQUIRE(Ready(p,0)); RequireIdentity(p,100,30,10);
}
static void FreshRehost() {
    Fixture f(false); auto &p=f.p; REQUIRE(p.SetMode(true)==1); REQUIRE(p.Host("first",6));
    RequireIdentity(p,100,10,10); uint8_t bytes[]={0x42};
    REQUIRE(p.QueueBroadcast(bytes,1,true)==1);
    p.zcon->Push(new ZNDNet::Event(ZNDNet::EVENT_SESSION_END,0));
    nextHostId=11; nextHostSession=200;
    Require(p.Host("second",6),"deliberate fresh Host rejected the new host/session tuple");
    RequireIdentity(p,200,11,11); REQUIRE(Ready(p,0));
    REQUIRE(p.GetMode()==1 && p.GetMyName()=="FIVE" && p.broadcastBuff_off==0);
    windp_recvMsg event; REQUIRE(!p.Recv(&event));
}
static void OfflineRetainedIdentity() {
    Fixture f; auto &p=f.p; p.zcon->status=ZNDNet::NetUser::STATUS_DISCONNECTED;
    // ENet retains these historical getters before the queued loss is consumed.
    REQUIRE(p.zcon->GetMyID()==30 && p.zcon->GetMySesID()==100);
    Require(!Ready(p,0),"disconnected transport with retained IDs exposed a ready current roster");
    RequireIdentity(p,100,30,10); CheckBlocked(p);
    uint8_t bytes[]={1};
    Inject(p,new ZNDNet::EventData(ZNDNet::EVENT_DATA,0,20,false,30,1,bytes,0),100);
    REQUIRE(Receive(p).msgType==RECVMSG_SYSUNK);
}
static void HostOfflineRetainedIdentity() {
    Fixture f(false); auto &p=f.p; REQUIRE(p.SetMode(true)==1); REQUIRE(p.Host("host",6));
    REQUIRE(Ready(p,0)); RequireIdentity(p,100,10,10);
    // ENet's live host is CONNECTING in its legacy GetStatus API because
    // transportConnected_ is a client flag. Only DISCONNECTED proves it stopped.
    if(transport=="enet") REQUIRE(p.zenet->GetStatus()==ZNDNet::NetUser::STATUS_CONNECTING);
    p.zcon->status=ZNDNet::NetUser::STATUS_DISCONNECTED;
    if(transport=="znd") p.zcon->id=p.zcon->session=0; // ZNDSingle::Stop clears ME.
    Require(!Ready(p,0),"stopped host retained current-roster authority"); CheckBlocked(p);
}
static void HostSessionEnd() {
    Fixture f(false); auto &p=f.p; REQUIRE(p.SetMode(true)==1); REQUIRE(p.Host("host",6));
    Inject(p,new ZNDNet::Event(ZNDNet::EVENT_SESSION_END,0),100);
    REQUIRE(Receive(p).msgType==RECVMSG_SESSIONEND);
    Require(!Ready(p,0) && !Identity(p,0).valid,"terminal host session retained admissible authority");
    CheckBlocked(p);
}
static bool IdentityCase(const std::string &s) {
    if(s=="identity_client")IdentityClient();
    else if(s=="identity_host")IdentityHost();
    else if(s=="identity_data")DataContext();
    else if(s=="identity_lifecycle")LifecycleContext();
    else if(s=="identity_stale_data")StaleSession(false,false);
    else if(s=="identity_stale_terminal")StaleSession(true,false);
    else if(s=="identity_missing_session")StaleSession(false,true);
    else if(s=="identity_host_migration")ConflictingIdentity(0);
    else if(s=="identity_session_change")ConflictingIdentity(1);
    else if(s=="identity_self_change")ConflictingIdentity(2);
    else if(s=="identity_roster_unavailable")RosterUnavailable();
    else if(s=="identity_send_kick")IDSendAndKick();
    else if(s=="identity_loss")RetainDuringLoss();
    else if(s=="identity_invalid_lifecycle")InvalidLifecycle(false);
    else if(s=="identity_rejected_lifecycle")InvalidLifecycle(true);
    else if(s=="identity_invalid_reset")InvalidReset();
    else if(s=="identity_invalid_reclaim")InvalidReclaim();
    else if(s=="identity_stale_refusal")UnadmittedRefusalWhileConnected();
    else if(s=="identity_rehost")FreshRehost();
    else if(s=="identity_offline_retained")OfflineRetainedIdentity();
    else if(s=="identity_host_offline")HostOfflineRetainedIdentity();
    else if(s=="identity_host_end")HostSessionEnd();
    else return false;
    return true;
}
}

int main(int argc,char **argv) {
    using namespace ProviderFixture;
    if(argc==2 && std::string(argv[1])=="--verify-failing-check")
        Require(false,"intentional provider check failure under NDEBUG");
    if(argc!=3) return 2;
    transport=argv[1]; Environment(); const std::string scenario=argv[2];
    if(IdentityCase(scenario)) {}
    else if(scenario=="barrier") Barrier();
    else if(scenario=="fresh") Fresh();
    else if(scenario=="events") EventMapping();
    else if(scenario=="malformed") Malformed();
    else if(scenario=="metadata") MetadataReset();
    else if(scenario=="explicit") Terminal(1);
    else if(scenario=="kick") Terminal(2);
    else if(scenario=="superseded") Terminal(4);
    else if(scenario=="unarmed") Unarmed();
    else if(scenario=="session_end") SessionEnd();
    else if(scenario=="timeout") Timeout();
    else if(scenario=="refusal_retry") Refusal(false);
    else if(scenario=="refusal_final") Refusal(true);
    else if(scenario=="wrong_id") WrongIdentity(false);
    else if(scenario=="wrong_session") WrongIdentity(true);
    else if(scenario=="disable") Disable();
    else if(scenario=="configuration_failure") ConfigureFailure();
    else if(scenario=="repeat_loss") RepeatLoss();
    else if(scenario=="enable_waiting") EnableAgain(false);
    else if(scenario=="enable_connecting") EnableAgain(true);
    else if(scenario=="late_reclaim") LateReclaim();
    else if(scenario=="fresh_malformed") FreshFailure(true);
    else if(scenario=="fresh_no_token") FreshFailure(false);
    else if(scenario=="reset") Reset();
    else if(scenario=="host") Host();
    else return 2;
    std::printf("PASS actual provider methods: %s %s\n",argv[1],argv[2]);
}
