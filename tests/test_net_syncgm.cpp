#include "net_syncgm.inc"
#include <functional>
#include <iostream>
#include <sstream>

static int passed=0, failed=0;
static void check(const std::string &name, bool okay) {
    std::cout << "{\"kind\":\"case\",\"name\":\"" << name
              << "\",\"passed\":" << (okay?"true":"false") << "}\n";
    okay?++passed:++failed;
}

// A valid frozen six-faction mapping can use sparse transport slots. Host ID11
// is slot0/owner6; this local player ID22 is slot2/owner1; sender ID33 is slot5.
struct Fixture {
    UserData users;
    NC_STACK_windp provider;
    NC_STACK_ypaworld world;
    NC_STACK_ypabact local,remote,other;
    std::array<NC_STACK_ypagun,9> guns;
    uint8_t owner;
    Fixture(size_t count=2,uint8_t remoteOwner=5):owner(remoteOwner) {
        sinks={};dispatch_marker=0;
        world._GameShell=&users;world._netDriver=&provider;users.p_YW=&world;
        world._userRobo=&local;world._timeStamp=1000;world._netUpdateTimeStamp=1000;
        world._updateMessage.gTime=5000;world._updateMessage.frameTime=0;
        users.deadCheckTime=1000;users.netPlayerOwner=1;users.HasInited=false;
        users.netSession.SessionId=70;users.netSession.LocalPlayerId=22;users.netSession.HostPlayerId=11;
        provider.players={{0,11,"host",4},{2,22,"local",1},{5,33,"peer",0}};
        bind(0,11,"host",6);bind(2,22,"local",1);bind(5,33,"peer",owner);
        users.netLaunchPlayers=users.lobbyPlayers;users.netLaunchFrozen=true;
        local._owner=1;local._bact_type=BACT_TYPES_ROBO;local._gid=0x01000010;
        remote._owner=owner;remote._bact_type=BACT_TYPES_ROBO;remote._gid=(uint32_t(owner)<<24)|0x20;
        other._owner=6;other._bact_type=BACT_TYPES_ROBO;other._gid=0x06000030;
        world._unitsList={&local,&remote,&other};
        for(size_t i=0;i<count;++i) {
            guns[i]._owner=owner;guns[i]._gid=(uint32_t(owner)<<24)|uint32_t(0x30+i);
            remote._kidList.push_back(&guns[i]);
        }
        for(auto&p:users.netPlayers){p.LastMessageTime=3;p.TimeStamp=10;p.MessageCount=7;}
    }
    void bind(int slot,uint64_t id,const char *name,uint8_t boundOwner) {
        auto&r=users.lobbyPlayers[slot];r.SessionId=70;r.PlayerId=id;r.Name=name;
        r.Owner=boundOwner;r.NetFraction=uint8_t(1u<<boundOwner);r.Ready=true;
        auto&p=users.netPlayers[boundOwner];p.SessionId=70;p.PlayerId=id;p.PlayerSlot=slot;
        p.Name=name;p.Owner=boundOwner;
    }
    uamessage_syncgame message()const {
        uamessage_syncgame m;NetMsgClear(m);m.msgID=UAMSG_SYNCGM;m.owner=owner;
        m.tstamp=100;m.msgCnt=7;m.hostID=(uint32_t(owner)<<24)|0x100;
        for(size_t i=0;i<remote._kidList.size()&&i<8;++i)m.gun[i]=(uint32_t(owner)<<24)|uint32_t(0x110+i);
        return m;
    }
    windp_recvMsg packet(const uamessage_syncgame&m,uint32_t cast=1)const {
        windp_recvMsg e;e.msgType=RECVMSG_NORMAL;e.sessionId=70;e.playerId=33;e.playerSlot=5;
        e.senderName="host"; // Deliberately misleading name: only immutable IDs matter.
        e.cast=cast;e._data.resize(sizeof(m));std::memcpy(e._data.data(),&m,sizeof(m));return e;
    }
    void run(windp_recvMsg e){provider.events.push_back(std::move(e));yw_HandleNetMsg(&world);}
    void run(const uamessage_syncgame&m){run(packet(m));}
};

static std::string state(const Fixture&f) {
    std::ostringstream s;
    s<<f.users.netrecv_count<<','<<f.users.problemTimer<<','<<f.users.deadCheckTime<<','
     <<f.world._netInfoOverkill<<','<<f.world._levelInfo.State<<','<<f.users.netSessionEnded<<','
     <<dispatch_marker<<','<<sinks.sent.size()<<','<<sinks.clean<<','<<sinks.deleted;
    for(const auto&p:f.users.netPlayers)s<<'|'<<p.Ready<<','<<unsigned(p.DestroyFlags)<<','
        <<p.LastMessageTime<<','<<p.TimeStamp<<','<<p.MessageCount;
    s<<'|'<<f.local._gid<<','<<f.remote._gid<<','<<f.other._gid;
    for(const auto&g:f.guns)s<<','<<g._gid;
    for(const auto&r:f.users.lobbyPlayers)s<<'|'<<r.Update.HoldsEvents(1000)<<r.Update.MayRequest(1000);
    return s.str();
}

static void reject(const char*name,const std::function<void(Fixture&,uamessage_syncgame&,windp_recvMsg&)>&change) {
    Fixture f;auto m=f.message();auto e=f.packet(m);change(f,m,e);
    std::memcpy(e._data.data(),&m,sizeof(m));
    const auto before=state(f);f.run(std::move(e));check(name,state(f)==before);
}

static void positiveCases() {
    for(uint8_t owner:{uint8_t(2),uint8_t(5)})for(size_t count=0;count<=8;++count) {
        Fixture f(count,owner);const auto m=f.message();f.run(m);
        bool okay=f.remote._gid==m.hostID&&f.users.netPlayers[owner].Ready&&
            f.users.netPlayers[owner].LastMessageTime==5000&&f.users.netrecv_count==sizeof(m)&&
            !f.users.netPlayers[6].Ready&&f.other._gid==0x06000030;
        for(size_t i=0;i<count;++i)okay=okay&&f.guns[i]._gid==m.gun[i];
        check("valid_sparse_owner"+std::to_string(owner)+"_guns"+std::to_string(count),okay);
        const auto ids=f.remote._gid;f.run(m);
        check("duplicate_same_mapping_owner"+std::to_string(owner)+"_guns"+std::to_string(count),
            f.remote._gid==ids&&f.users.netPlayers[owner].Ready&&f.users.netrecv_count==2*sizeof(m));
    }
    Fixture host(0);host.users.netSession.LocalPlayerId=11;host.provider.fixturePhase=WindpResume::Phase::Idle;
    const auto m=host.message();host.run(m);
    check("valid_host_provider_idle_phase",host.remote._gid==m.hostID&&host.users.netPlayers[5].Ready);
    Fixture nonGun(0);NC_STACK_ypabact child;child._owner=5;child._gid=0x05000123;
    nonGun.remote._kidList.push_back(&child);auto noGun=nonGun.message();noGun.gun[0]=0;nonGun.run(noGun);
    check("ordinary_child_not_a_station_gun",nonGun.users.netPlayers[5].Ready&&child._gid==0x05000123);
}

static void negativeCases() {
    reject("forged_other_owner",[](Fixture&,uamessage_syncgame&m,windp_recvMsg&){m.owner=6;m.hostID=0x06000100;m.gun[0]=m.gun[1]=0;});
    reject("unfrozen_startup",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.users.netLaunchFrozen=false;});
    reject("wrong_frozen_owner",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.users.netLaunchPlayers[5].Owner=2;});
    reject("wrong_frozen_faction",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.users.netLaunchPlayers[5].NetFraction=1u<<2;});
    reject("reused_slot_new_identity",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&e){f.users.lobbyPlayers[5].PlayerId=44;f.users.netPlayers[5].PlayerId=44;e.playerId=44;});
    reject("old_session",[](Fixture&,uamessage_syncgame&,windp_recvMsg&e){e.sessionId=71;});
    reject("wrong_slot",[](Fixture&,uamessage_syncgame&,windp_recvMsg&e){e.playerSlot=2;});
    reject("terminal_session",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.users.netSessionEnded=true;});
    reject("aborted_level",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.world._levelInfo.State=TLevelInfo::STATE_ABORTED;});
    reject("already_started",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.world._isNetGameStarted=true;});
    reject("expired_start_barrier",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.world._netStartTimer=0;});
    reject("repair_pending",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.users.lobbyPlayers[5].Update.Requested(950);});
    reject("reclaim_pending",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.provider.fixturePhase=WindpResume::Phase::AwaitingReclaim;});
    reject("retry_pending",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.provider.fixturePhase=WindpResume::Phase::WaitingRetry;});
    reject("destroyed_player",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.users.netPlayers[5].DestroyFlags=1;});
    reject("missing_station",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.world._unitsList.erase(f.world._unitsList.begin()+1);});
    reject("duplicate_station",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.other._owner=5;});
    reject("wrong_gun_owner",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.guns[1]._owner=6;});
    reject("duplicate_gun_object",[](Fixture&f,uamessage_syncgame&,windp_recvMsg&){f.remote._kidList[1]=f.remote._kidList[0];});
    reject("zero_station_id",[](Fixture&,uamessage_syncgame&m,windp_recvMsg&){m.hostID=0;});
    reject("zero_gun_id",[](Fixture&,uamessage_syncgame&m,windp_recvMsg&){m.gun[1]=0;});
    reject("wrong_station_id_owner",[](Fixture&,uamessage_syncgame&m,windp_recvMsg&){m.hostID=0x06000100;});
    reject("wrong_gun_id_owner",[](Fixture&,uamessage_syncgame&m,windp_recvMsg&){m.gun[1]=0x06000111;});
    reject("gun_equals_station_id",[](Fixture&,uamessage_syncgame&m,windp_recvMsg&){m.gun[1]=m.hostID;});
    reject("duplicate_gun_id",[](Fixture&,uamessage_syncgame&m,windp_recvMsg&){m.gun[1]=m.gun[0];});
    reject("unmatched_extra_gun_id",[](Fixture&,uamessage_syncgame&m,windp_recvMsg&){m.gun[2]=0x05000112;});
    reject("conflicting_duplicate_mapping",[](Fixture&f,uamessage_syncgame&m,windp_recvMsg&){f.run(m);m.hostID=0x05000200;});
    for(uint32_t cast:{0u,2u})reject(cast?"forged_owner_legacy_cast2":"forged_owner_live_cast0",
        [cast](Fixture&,uamessage_syncgame&m,windp_recvMsg&e){e.cast=cast;m.owner=6;m.hostID=0x06000100;m.gun[0]=m.gun[1]=0;});

    // Do not invoke the baseline's known out-of-array read on a ninth gun.
    // Actual complete-packet preflight must reject it before dispatch/accounting.
    Fixture tooMany(9);const auto before=state(tooMany);auto p=tooMany.packet(tooMany.message());
    check("nine_guns_rejected_in_actual_packet_preflight",yw_netAdmittedPacketBytes(&tooMany.world,p)==0&&state(tooMany)==before);

    Fixture malformed;auto good=malformed.packet(malformed.message());good._data.pop_back();
    const auto malformedBefore=state(malformed);malformed.run(std::move(good));
    check("truncated_syncgm_no_effects",state(malformed)==malformedBefore);

    Fixture mixed;auto packet=mixed.packet(mixed.message());
    uamessage_base bad;NetMsgClear(bad);bad.msgID=0xDEADBEEF;
    const auto*pbad=reinterpret_cast<const uint8_t*>(&bad);packet._data.insert(packet._data.end(),pbad,pbad+sizeof(bad));
    const auto mixedBefore=state(mixed);mixed.run(std::move(packet));
    check("valid_syncgm_then_malformed_batch_is_atomic",state(mixed)==mixedBefore);

    Fixture conflicting;auto first=conflicting.message(),second=first;second.hostID=0x05000300;
    auto conflictPacket=conflicting.packet(first);const auto*bytes=reinterpret_cast<const uint8_t*>(&second);
    conflictPacket._data.insert(conflictPacket._data.end(),bytes,bytes+sizeof(second));
    const auto conflictBefore=state(conflicting);conflicting.run(std::move(conflictPacket));
    check("conflicting_syncgm_batch_no_partial_apply",state(conflicting)==conflictBefore);
}

static void senderCases() {
    for(size_t count=0;count<=9;++count) {
        Fixture f(count);f.users.netPlayerOwner=5;f.world._userRobo=&f.remote;
        const size_t result=f.world.FixtureStartupSend();
        if(count==9) {
            check("sender_nine_guns_fails_load_without_ready_or_send",result==0&&sinks.deleted==1&&
                sinks.sent.empty()&&!f.users.netPlayers[5].Ready&&!f.remote.fixtureViewer);
        } else {
            bool okay=result==1&&sinks.deleted==0&&sinks.sent.size()==1&&sinks.flushes==1&&
                sinks.lastBroadcast.size()==sizeof(uamessage_syncgame)&&f.users.netPlayers[5].Ready;
            uamessage_syncgame sent;NetMsgClear(sent);
            if(sinks.lastBroadcast.size()==sizeof(sent))std::memcpy(&sent,sinks.lastBroadcast.data(),sizeof(sent));
            okay=okay&&sent.hostID==f.remote._gid;
            for(size_t i=0;i<8;++i)okay=okay&&sent.gun[i]==(i<count?f.guns[i]._gid:0);
            check("sender_exact_guns"+std::to_string(count),okay);
        }
    }
}

int main(int argc,char**argv) {
    if(argc==2&&std::string(argv[1])=="--verify-failing-check") {
        check("intentional_ndebug_failure",false);return failed?1:0;
    }
    positiveCases();negativeCases();senderCases();
    std::cout<<"{\"kind\":\"summary\",\"total\":"<<passed+failed<<",\"passed\":"<<passed<<",\"failed\":"<<failed<<"}\n";
    return failed?1:0;
}
