#include "net_identity_outer.inc"
#include <iostream>
#include <sstream>

static int passed=0,failed=0;
static void check(const std::string&name,bool ok){
    std::cout<<"{\"kind\":\"case\",\"name\":\""<<name<<"\",\"passed\":"<<(ok?"true":"false")<<"}\n";
    ok?++passed:++failed;
}
struct Fixture {
    UserData users;NC_STACK_windp provider;NC_STACK_ypaworld world;
    NC_STACK_ypabact local,remote;
    Fixture(bool loaded){
        sinks={};dispatch_marker=0;
        world._GameShell=&users;world._netDriver=&provider;users.p_YW=&world;
        world._userRobo=loaded?&local:nullptr;world._timeStamp=250000;
        world._netUpdateTimeStamp=250000;world._updateMessage.gTime=250000;
        world._updateMessage.frameTime=0;users.DTime=0;
        users.deadCheckTime=250000;users.netPlayerOwner=loaded?1:0;
        users.netSession.SessionId=70;users.netSession.LocalPlayerId=22;users.netSession.HostPlayerId=11;
        users.HasInited=!loaded;users.netPlayerName="local";
        provider.players={{0,11,"host",4},{1,22,"local",1},{2,33,"peer",0}};
        bind(0,11,"host",loaded?6:0);bind(1,22,"local",loaded?1:0);bind(2,33,"peer",loaded?2:0);
        users.netLaunchPlayers=users.lobbyPlayers;users.netLaunchFrozen=loaded;
        remote._owner=2;remote._gid=42;remote._lastFrmStamp=0;remote._bact_type=BACT_TYPES_ROBO;
        world._unitsList.push_back(&remote);
        for(auto&p:users.netPlayers){p.LastMessageTime=3;p.TimeStamp=10;p.MessageCount=7;}
    }
    void bind(int slot,uint64_t id,const std::string&name,uint8_t owner){
        auto&r=users.lobbyPlayers[slot];r.SessionId=70;r.PlayerId=id;r.Name=name;r.Owner=owner;
        r.NetFraction=uint8_t(1u<<slot);r.Ready=true;r.Welcomed=true;r.DataChecksum=123;
        if(owner){auto&p=users.netPlayers[owner];p.SessionId=70;p.PlayerId=id;p.PlayerSlot=slot;p.Name=name;p.Owner=owner;}
    }
    void enqueue(uint32_t type,uint64_t session,uint64_t id,int slot,const std::string&name){
        windp_recvMsg m;m.msgType=type;m.sessionId=session;m.playerId=id;m.playerSlot=slot;m.senderName=name;
        provider.events.push_back(std::move(m));
    }
    template<class M>void packet(const M&m,uint64_t session,uint64_t id,const std::string&name,uint32_t cast=1){
        windp_recvMsg e;e.msgType=RECVMSG_NORMAL;e.sessionId=session;e.playerId=id;e.senderName=name;e.cast=cast;
        // Current provider contract resolves known NORMAL IDs to stable slots;
        // unknown input is deliberately injected past that provider guard here.
        for(const auto&p:provider.players)if(p.ID==id){e.playerSlot=p.Index;break;}
        e._data.resize(sizeof(m));std::memcpy(e._data.data(),&m,sizeof(m));provider.events.push_back(std::move(e));
    }
    void run(){yw_HandleNetMsg(&world);}
};

// Snapshot observable production state, excluding only isolated log count.
static std::string state(const Fixture&f){
    std::ostringstream o;
    const auto&u=f.users;
    o<<u.netrecv_count<<','<<u.problemTimer<<','<<u.deadCheckTime<<','<<f.world._netInfoOverkill<<','
     <<u.netProblem<<','<<u.netProblemCountDown<<','<<u.isHost<<','<<u.rdyStart<<','<<u.blocked<<','
     <<f.world._levelInfo.State<<','<<dispatch_marker<<','<<sinks.crc<<','<<sinks.clean<<','<<sinks.locks<<','
     <<u.netSession.SessionId<<','<<u.netSession.LocalPlayerId<<','<<u.netSession.HostPlayerId<<','<<u.netLaunchFrozen;
    for(const auto&p:u.netPlayers)o<<'|'<<p.SessionId<<','<<p.PlayerId<<','<<p.PlayerSlot<<','<<p.Name<<','
        <<unsigned(p.Owner)<<','<<unsigned(p.Status)<<','<<unsigned(p.DestroyFlags)<<','
        <<p.LastMessageTime<<','<<p.TimeStamp<<','<<p.MessageCount;
    for(const auto&r:u.lobbyPlayers){
        o<<'|'<<r.SessionId<<','<<r.PlayerId<<','<<r.Name<<','<<unsigned(r.Owner)<<','
          <<unsigned(r.NetFraction)<<','<<r.Ready<<','<<r.Welcomed<<','<<r.DataChecksum;
        for(uint32_t t:{0u,249999u,250000u,251999u,252000u,255000u})
            o<<','<<r.Update.HoldsEvents(t)<<r.Update.MayRequest(t)<<r.Update.MayAnswer(t);
    }
    for(const auto&r:u.netLaunchPlayers)o<<'|'<<r.SessionId<<','<<r.PlayerId<<','<<r.Name<<','<<unsigned(r.Owner)<<','<<r.Ready;
    for(const auto&s:sinks.sent)o<<'|'<<s.id<<':'<<s.destination<<':'<<s.boundSession<<':'<<s.boundPlayer;
    return o.str();
}

static void rejectedPacket(const std::string&name,uint64_t session,uint64_t sender,uint32_t cast,
                           bool flood=false,bool corrupt=false){
    Fixture f(true);if(corrupt)f.users.deadCheckTime=0;
    uamessage_score m;NetMsgClear(m);m.msgID=UAMSG_SCORE;m.owner=6;m.tstamp=100;m.msgCnt=7;
    const auto before=state(f);
    for(int i=0;i<(flood?31:1);++i)f.packet(m,session,sender,"host",cast);
    f.run();check(name,state(f)==before);
    std::cout<<"{\"kind\":\"packet_observation\",\"name\":\""<<name<<"\",\"dispatch\":"<<dispatch_marker
        <<",\"received_bytes\":"<<f.users.netrecv_count<<",\"overload\":"<<(f.world._netInfoOverkill?"true":"false")
        <<",\"problem_timer\":"<<f.users.problemTimer<<",\"dead_check_time\":"<<f.users.deadCheckTime
        <<",\"outgoing\":"<<sinks.sent.size()<<",\"repair_hold\":"<<(f.users.lobbyPlayers[2].Update.HoldsEvents(250000)?"true":"false")<<"}\n";
}

static void eventRejected(const std::string&name,uint32_t kind,uint64_t session,uint64_t id,bool loaded=false){
    Fixture f(loaded);f.users.lobbyPlayers[2].Update.Requested(249950);
    const auto before=state(f);
    f.enqueue(kind,session,id,2,"peer");f.run();check(name,state(f)==before);
}

// The downstream NORMAL switch is only a marker. These tests establish that
// valid packets cannot even enter it after an actual terminal lifecycle branch.
static void terminalCases(){
    {
        Fixture f(false);uamessage_welcome m;NetMsgClear(m);m.msgID=UAMSG_WELCOME;m.fraction=4;
        f.packet(m,70,33,"peer");f.run();
        check("control_terminal_suite_lobby_welcome",dispatch_marker==1&&f.users.netrecv_count==sizeof(m));
    }
    {
        Fixture f(false);uamessage_load m;NetMsgClear(m);m.msgID=UAMSG_LOAD;m.level=1;
        f.packet(m,70,11,"host");f.run();
        check("control_terminal_suite_lobby_load",dispatch_marker==1&&f.users.netrecv_count==sizeof(m));
    }
    for(bool terminal:{false,true}){
        Fixture f(true);
        if(terminal)f.enqueue(RECVMSG_SESSIONEND,70,11,0,"host");
        uamessage_syncgame m;NetMsgClear(m);m.msgID=UAMSG_SYNCGM;m.owner=2;m.hostID=0x02000100;
        f.packet(m,70,33,"peer");f.run();
        check(terminal?"terminal_sessionend_then_loaded_syncgm":"control_terminal_suite_loaded_syncgm",
            dispatch_marker==(terminal?0:1)&&f.users.netrecv_count==(terminal?0:sizeof(m)));
    }
    for(bool terminal:{false,true}){
        Fixture f(true);
        if(terminal)f.enqueue(RECVMSG_RECOVERYFAILED,70,11,0,"host");
        uamessage_update m;NetMsgClear(m);m.msgID=UAMSG_UPDATE;m.owner=2;m.num=2;
        m.sz=sizeof(m)-1022*sizeof(vhclUpdData);m.data[0].type=1; // one record, then terminator
        f.packet(m,70,33,"peer");f.provider.events.back()._data.resize(m.sz);f.run();
        check(terminal?"terminal_recoveryfailed_then_loaded_update":"control_terminal_suite_loaded_update",
            dispatch_marker==(terminal?0:1)&&f.users.netrecv_count==(terminal?0:m.sz));
    }
    struct Trial {const char*name;uint32_t event;uint64_t id;int slot;bool loaded,started,load;};
    const Trial trials[]={
        {"terminal_sessionend_then_lobby_welcome",RECVMSG_SESSIONEND,11,0,false,false,false},
        {"terminal_recoveryfailed_then_lobby_load",RECVMSG_RECOVERYFAILED,11,0,false,false,true},
        {"terminal_hostgone_then_lobby_welcome",RECVMSG_PLAYERGONE,11,0,false,false,false},
        {"terminal_hostaway_then_lobby_welcome",RECVMSG_PLAYERAWAY,11,0,false,false,false},
        {"terminal_selfgone_then_loaded_score",RECVMSG_PLAYERGONE,22,1,true,false,false},
        {"terminal_loading_away_then_loaded_score",RECVMSG_PLAYERAWAY,33,2,true,false,false},
        {"terminal_started_sessionend_then_score",RECVMSG_SESSIONEND,11,0,true,true,false},
        {"terminal_started_recoveryfailed_then_score",RECVMSG_RECOVERYFAILED,11,0,true,true,false}
    };
    for(const auto&t:trials){
        Fixture f(t.loaded);f.world._isNetGameStarted=t.started;
        f.users.rdyStart=true;f.users.envAction.action=EnvAction::ACTION_NETPLAY;
        f.enqueue(t.event,70,t.id,t.slot,t.slot==0?"host":t.slot==1?"local":"peer");
        if(t.loaded){
            uamessage_score m;NetMsgClear(m);m.msgID=UAMSG_SCORE;m.owner=6;f.packet(m,70,11,"host");
        }else if(t.load){
            uamessage_load m;NetMsgClear(m);m.msgID=UAMSG_LOAD;m.level=1;f.packet(m,70,11,"host");
        }else{
            uamessage_welcome m;NetMsgClear(m);m.msgID=UAMSG_WELCOME;m.fraction=4;f.packet(m,70,33,"peer");
        }
        f.run();
        check(t.name,f.world._levelInfo.State==TLevelInfo::STATE_ABORTED&&dispatch_marker==0&&
            f.users.netrecv_count==0&&f.users.netPlayers[6].LastMessageTime==3&&
            f.users.netSessionEnded&&f.users.noSent&&!f.users.rdyStart&&
            f.users.envAction.action==EnvAction::ACTION_NONE&&sinks.sent.empty());
        std::cout<<"{\"kind\":\"terminal_observation\",\"name\":\""<<t.name<<"\",\"dispatch\":"<<dispatch_marker
            <<",\"received_bytes\":"<<f.users.netrecv_count<<",\"ended\":"<<(f.users.netSessionEnded?"true":"false")<<"}\n";
    }
    {
        Fixture f(true);f.world._isNetGameStarted=true;
        f.enqueue(RECVMSG_SESSIONEND,70,11,0,"host");f.run();
        f.users.isHost=true;f.users.sendScoreCountDown=-1;
        f.world._timeStamp+=5000;const auto before=state(f);
        const auto previousUpdate=f.world._netUpdateTimeStamp;
        f.run();
        check("terminal_later_empty_tick_no_periodic_work",state(f)==before&&
            f.world._netUpdateTimeStamp==previousUpdate&&f.users.sendScoreCountDown==-1);
    }
}

int main(int argc,char**argv){
    if(argc==2 && std::string(argv[1])=="--verify-failing-check"){
        check("intentional_fail_under_ndebug",false);return 1;
    }
    if(argc==2 && std::string(argv[1])=="--terminal-only"){
        terminalCases();
        std::cout<<"{\"kind\":\"summary\",\"passed\":"<<passed<<",\"failed\":"<<failed<<"}\n";
        return failed?1:0;
    }
    {
        Fixture f(true);f.users.deadCheckTime=0;const auto before=state(f);f.run();
        check("control_empty_input_no_corruption_or_periodic_work",state(f)==before);
    }
    {
        Fixture f(true);uamessage_score m;NetMsgClear(m);m.msgID=UAMSG_SCORE;m.owner=6;
        f.packet(m,70,11,"host");f.run();
        check("control_known_host_score_receiver_start_barrier",dispatch_marker==1&&f.users.netPlayers[6].LastMessageTime==250000);
    }
    rejectedPacket("reject_unknown_id_cast1",70,999,1);
    rejectedPacket("reject_zero_id_cast0",70,0,0);
    rejectedPacket("reject_wrong_session_known_host",71,11,1);
    rejectedPacket("reject_zero_session_known_host",0,11,0);
    rejectedPacket("reject_wrong_host_spoofed_host_name",70,33,1);
    rejectedPacket("reject_unknown_id_overload_threshold",70,999,1,true,false);
    rejectedPacket("reject_unknown_id_due_corruption_and_repair",70,999,1,false,true);
    rejectedPacket("reject_wrong_host_due_corruption_and_repair",70,33,1,false,true);
    {
        Fixture f(false);f.enqueue(RECVMSG_PLAYERAWAY,70,33,2,"peer");f.run();
        check("control_current_away_clears_only_ready",!f.users.lobbyPlayers[2].Ready&&f.users.lobbyPlayers[2].PlayerId==33&&sinks.sent.empty());
    }
    eventRejected("reject_stale_id_away_same_slot_name",RECVMSG_PLAYERAWAY,70,333);
    eventRejected("reject_wrong_session_away",RECVMSG_PLAYERAWAY,71,33);
    eventRejected("reject_stale_id_gone_same_slot_name",RECVMSG_PLAYERGONE,70,333);
    eventRejected("reject_wrong_session_gone_loaded",RECVMSG_PLAYERGONE,71,33,true);
    eventRejected("reject_stale_id_back_same_slot_name",RECVMSG_PLAYERBACK,70,333);
    eventRejected("reject_wrong_session_create",RECVMSG_CREATEPLAYER,71,33);
    {
        Fixture f(false);f.users.lobbyPlayers[2].Ready=false;f.users.lobbyPlayers[2].Update.Requested(249950);
        f.enqueue(RECVMSG_PLAYERBACK,70,33,2,"peer");f.run();
        check("control_current_back_preserves_identity_and_claim",f.users.lobbyPlayers[2].PlayerId==33&&f.users.lobbyPlayers[2].NetFraction==4);
    }
    {
        Fixture f(false);f.users.lobbyPlayers[4]={};f.provider.players.push_back({4,44,"new",0});
        f.enqueue(RECVMSG_CREATEPLAYER,70,44,4,"new");f.run();
        bool welcomeBound=false;
        for(const auto&s:sinks.sent)if(s.id==UAMSG_WELCOME&&s.destination=="new")welcomeBound=s.boundSession==70&&s.boundPlayer==44;
        check("create_binds_identity_before_welcome",f.users.lobbyPlayers[4].SessionId==70&&f.users.lobbyPlayers[4].PlayerId==44&&f.users.lobbyPlayers[4].Name=="new"&&welcomeBound);
    }
    {
        Fixture f(false);f.users.lobbyPlayers[2].Update.Requested(249950);
        f.enqueue(RECVMSG_PLAYERGONE,70,33,2,"peer");f.run();
        check("control_terminal_gone_clears_complete_row",f.users.lobbyPlayers[2].Name.empty()&&!f.users.lobbyPlayers[2].PlayerId&&!f.users.lobbyPlayers[2].Ready&&f.users.lobbyPlayers[2].Update.MayRequest(250000));
        f.provider.players[2].ID=44;f.enqueue(RECVMSG_CREATEPLAYER,70,44,2,"peer");f.run();
        check("reuse_new_id_gets_clean_row",f.users.lobbyPlayers[2].PlayerId==44&&!f.users.lobbyPlayers[2].Owner&&!f.users.lobbyPlayers[2].Ready&&!f.users.lobbyPlayers[2].NetFraction);
        f.users.lobbyPlayers[2].Ready=true;const auto before=state(f);
        f.enqueue(RECVMSG_PLAYERAWAY,70,33,2,"peer");f.enqueue(RECVMSG_PLAYERGONE,70,33,2,"peer");f.run();
        check("reuse_delayed_old_away_gone_cannot_mutate_replacement",state(f)==before);
    }
    {
        Fixture f(true);const auto before=state(f);f.enqueue(RECVMSG_CREATEPLAYER,70,44,4,"outsider");f.run();
        check("control_loaded_outsider_no_lobby_welcome",state(f)==before);
    }
    {
        Fixture f(true);const auto before=state(f);f.enqueue(RECVMSG_HOST,70,33,2,"peer");f.run();
        check("reject_legacy_host_event_migration",state(f)==before);
    }
    {
        Fixture f(true);const auto before=state(f);f.enqueue(RECVMSG_SESSIONEND,71,11,0,"host");f.run();
        check("reject_old_session_terminal_event",state(f)==before);
    }
    std::cout<<"{\"kind\":\"summary\",\"passed\":"<<passed<<",\"failed\":"<<failed<<"}\n";
    return failed?1:0;
}
