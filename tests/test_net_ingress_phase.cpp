#include "net_ingress_prefix.inc"
#include <iostream>
#include <functional>

struct Snapshot {
    std::array<std::array<uint32_t,3>,World::CVFractionsCount> values;
    explicit Snapshot(const UserData &u) {
        for(size_t i=0;i<values.size();++i)
            values[i]={{u.netPlayers[i].LastMessageTime,u.netPlayers[i].TimeStamp,u.netPlayers[i].MessageCount}};
    }
    bool operator==(const Snapshot &s) const { return values==s.values; }
};
struct Fixture {
    UserData users;
    NC_STACK_ypabact station;
    NC_STACK_ypaworld world;
    Fixture(bool loaded) {
        world._GameShell=&users; world._userRobo=loaded?&station:nullptr;
        world._updateMessage.gTime=5000; world._timeStamp=1000;
        for(auto &p:users.netPlayers){p.LastMessageTime=3;p.TimeStamp=10;p.MessageCount=7;}
        users.netSession.SessionId=10;
        users.netSession.LocalPlayerId=303;
        users.netSession.HostPlayerId=303;
        Seed(0,303,"host",loaded?5:0);
        Seed(2,101,"actor",loaded?2:0);
        Seed(5,202,"victim",loaded?3:0);
        users.netLaunchPlayers=users.lobbyPlayers;
        users.netLaunchFrozen=loaded;
    }
    void Seed(int slot,uint64_t id,const char *name,uint8_t owner) {
        auto &row=users.lobbyPlayers[slot];
        row.SessionId=10;row.PlayerId=id;row.Name=name;row.Owner=owner;
        row.Update.Requested(950);
        if(owner) {
            auto &player=users.netPlayers[owner];
            player.SessionId=10;player.PlayerId=id;player.PlayerSlot=slot;
            player.Name=name;player.Owner=owner;
        }
    }
};
static int passed=0,failed=0;
static bool gatesUnchanged(const std::array<UserData::TNetPlayerLobbyData,World::CVMaxNetPlayers> &before,
                           const std::array<UserData::TNetPlayerLobbyData,World::CVMaxNetPlayers> &after) {
    for(size_t slot=0;slot<before.size();++slot)
        for(uint32_t time:{950u,1000u,2949u,2950u,5950u}) {
            const auto &a=before[slot].Update;
            const auto &b=after[slot].Update;
            if(a.HoldsEvents(time)!=b.HoldsEvents(time) || a.MayRequest(time)!=b.MayRequest(time)
                || a.MayAnswer(time)!=b.MayAnswer(time)) return false;
        }
    return true;
}
static void emit(const std::string &name,bool ok,size_t consumed,size_t expected,int owner,int accountingOwner,
                 const Snapshot &before,const Snapshot &after,bool gateUnchanged) {
    const size_t observedOwner=accountingOwner>=0 && accountingOwner<(int)World::CVFractionsCount ? accountingOwner : 0;
    std::cout<<"{\"kind\":\"case\",\"name\":\""<<name<<"\",\"passed\":"<<(ok?"true":"false")
        <<",\"consumed\":"<<consumed<<",\"expected_consumed\":"<<expected<<",\"dispatch_marker\":"<<dispatch_marker
        <<",\"peer_gate_unchanged\":"<<(gateUnchanged?"true":"false")
        <<",\"payload_owner\":"<<owner<<",\"observed_accounting_owner\":"<<observedOwner<<",\"before\":["<<before.values[observedOwner][0]<<","<<before.values[observedOwner][1]<<","<<before.values[observedOwner][2]
        <<"],\"after\":["<<after.values[observedOwner][0]<<","<<after.values[observedOwner][1]<<","<<after.values[observedOwner][2]<<"]}"<<std::endl;
    if(ok)++passed;else ++failed;
}
template<class M> static void check(const std::string &name,uint32_t id,bool loaded,uint32_t cast,
        int accounting,int expectedDispatch,bool malformed=false,
        const std::function<void(M&)> &configure={},int bytesAdjust=0,uint8_t owner=2,
        const std::function<void(Fixture&,windp_recvMsg&)> &configureIncoming={},uint8_t accountingOwner=2) {
    Fixture fixture(loaded); M message; NetMsgClear(message);
    message.msgID=id;message.owner=owner;message.tstamp=100;message.msgCnt=7;
    if(configure)configure(message);
    Snapshot before(fixture.users),expected(before);
    if(accounting>=1)expected.values[accountingOwner][0]=5000;
    if(accounting==2){expected.values[accountingOwner][1]=100;expected.values[accountingOwner][2]=8;}
    windp_recvMsg incoming;incoming.data=&message;incoming.cast=cast;incoming.senderName="actor";
    incoming.sessionId=10;incoming.playerId=101;incoming.playerSlot=2;incoming.size=sizeof(message)+bytesAdjust;
    if(configureIncoming)configureIncoming(fixture,incoming);
    const auto gates=fixture.users.lobbyPlayers;
    dispatch_marker=0;uint64_t error=0;
    const size_t consumed=yw_handleNormMsg(&fixture.world,&incoming,incoming.size,&error);
    const Snapshot after(fixture.users);
    const bool unchanged=gatesUnchanged(gates,fixture.users.lobbyPlayers);
    const size_t expectedSize=malformed?0:sizeof(message);
    emit(name,after==expected && dispatch_marker==expectedDispatch && consumed==expectedSize && unchanged,
         consumed,expectedSize,owner,accountingOwner,before,after,unchanged);
}

// Explicit minimal batch caller: advances by handler's consumed size; no transport emulation.
static void mixedBatch() {
    Fixture fixture(false);
    uamessage_setState rejected;NetMsgClear(rejected);rejected.msgID=UAMSG_SETSTATE;
    rejected.owner=2;rejected.tstamp=100;rejected.msgCnt=7;
    uamessage_message chat;NetMsgClear(chat);chat.msgID=UAMSG_MESSAGE;
    chat.owner=0;chat.tstamp=200;chat.msgCnt=7;
    std::vector<uint8_t> buffer(sizeof(rejected)+sizeof(chat));
    std::memcpy(buffer.data(),&rejected,sizeof(rejected));
    std::memcpy(buffer.data()+sizeof(rejected),&chat,sizeof(chat));
    Snapshot before(fixture.users),expected(before);
    const auto gates=fixture.users.lobbyPlayers;
    size_t offset=0;dispatch_marker=0;uint64_t error=0;
    while(offset<buffer.size()) {
        windp_recvMsg incoming;incoming.data=buffer.data()+offset;incoming.cast=2;incoming.senderName="actor";
        incoming.sessionId=10;incoming.playerId=101;incoming.playerSlot=2;
        const size_t consumed=yw_handleNormMsg(&fixture.world,&incoming,buffer.size()-offset,&error);
        if(!consumed)break;
        offset+=consumed;
    }
    Snapshot after(fixture.users);
    const bool unchanged=gatesUnchanged(gates,fixture.users.lobbyPlayers);
    emit("wrong_phase_then_any_minimal_batch",after==expected && offset==buffer.size() && dispatch_marker==1 && unchanged,
         offset,buffer.size(),2,2,before,after,unchanged);
}

int main(int argc,char **argv) {
    // Runtime check aggregation must stay effective under the target's NDEBUG.
    if(argc==2 && std::string(argv[1])=="--verify-failing-check") {
        Fixture fixture(false); Snapshot before(fixture.users);
        emit("intentional_failing_check_under_ndebug",false,0,0,0,0,before,before,true);
        return failed==1 ? 1 : 0;
    }
    // The historical 24-case suite/RED receipts predate identity admission and
    // remain immutable. This suite extracts the new production identity prefix.
    // Lobby positives now use owner0 and unseated actor0: no match accounting.
    // Loaded owner0 chat is rejected; match chat uses the sender's bound owner.
    // Live EventData::cast is bool: both provider values0/1 must isolate liveness.
    check<uamessage_sectorEnergy>("wrong_phase_level_cast1",UAMSG_SECTORENERGY,false,1,0,0);
    check<uamessage_sectorEnergy>("wrong_phase_level_cast0",UAMSG_SECTORENERGY,false,0,0,0);
    // cast2 is legacy/direct-handler branch coverage, not current provider input.
    check<uamessage_setState>("wrong_phase_level_cast2",UAMSG_SETSTATE,false,2,0,0);
    check<uamessage_welcome>("wrong_phase_lobby_cast2",UAMSG_WELCOME,true,2,0,0,false,[](uamessage_welcome&m){m.fraction=1<<1;},0,0);
    check<uamessage_crc>("wrong_phase_crc_cast2",UAMSG_CRC,true,2,0,0,false,{},0,0);
    check<uamessage_requpdate>("wrong_phase_request_cast2",UAMSG_REQUPDATE,false,2,0,0);
    check<uamessage_vhclEnergy>("wrong_phase_cross_owner_energy",UAMSG_VHCLENERGY,false,2,0,0);
    check<uamessage_endPlasma>("wrong_phase_cross_owner_plasma",UAMSG_ENDPLASMA,false,2,0,0);
    check<uamessage_setState>("accepted_level_cast2_with_trailing_bytes",UAMSG_SETSTATE,true,2,2,1,false,{},9);
    check<uamessage_setState>("accepted_level_cast1",UAMSG_SETSTATE,true,1,1,1);
    check<uamessage_welcome>("accepted_lobby_welcome_owner0_no_accounting",UAMSG_WELCOME,false,2,0,1,false,[](uamessage_welcome&m){m.fraction=1<<1;},0,0);
    check<uamessage_message>("accepted_any_lobby_owner0_no_accounting",UAMSG_MESSAGE,false,2,0,1,false,{},0,0);
    check<uamessage_message>("accepted_any_level",UAMSG_MESSAGE,true,2,2,1);
    check<uamessage_message>("rejected_stale_owner0_match_chat",UAMSG_MESSAGE,true,2,0,0,false,{},0,0);
    check<uamessage_vhclEnergy>("accepted_cross_owner_energy_actor2_victim3",UAMSG_VHCLENERGY,true,2,1,1,false,{},0,3);
    check<uamessage_endPlasma>("accepted_cross_owner_plasma_actor2_victim3",UAMSG_ENDPLASMA,true,2,1,1,false,{},0,3);
    check<uamessage_base>("malformed_short_base",UAMSG_REQUPDATE,false,2,0,0,true,{},-1);
    check<uamessage_setState>("malformed_truncated_fixed",UAMSG_SETSTATE,true,2,0,0,true,{},-(int)(sizeof(uamessage_setState)-sizeof(uamessage_base)));
    check<uamessage_base>("malformed_owner",UAMSG_REQUPDATE,true,2,0,0,true,{},0,8);
    check<uamessage_base>("malformed_unknown_id",0xDEADBEEF,true,2,0,0,true);
    check<uamessage_welcome>("malformed_faction",UAMSG_WELCOME,false,2,0,0,true);
    check<uamessage_startBuild>("malformed_build_sector",UAMSG_STARTBUILD,true,2,0,0,true,[](uamessage_startBuild&m){m.sec_x=9;});
    check<uamessage_update>("malformed_variable_update_count",UAMSG_UPDATE,true,2,0,0,true);
    mixedBatch();

    // Identity negatives are well-framed and in-phase. They must be consumed
    // without touching actor/victim accounting or reaching the dispatch marker.
    for(uint32_t cast:{0u,1u}) {
        const std::string suffix="_cast"+std::to_string(cast);
        check<uamessage_setState>("unknown_sender"+suffix,UAMSG_SETSTATE,true,cast,0,0,false,{},0,2,
            [](Fixture&,windp_recvMsg&m){m.playerId=999;});
        check<uamessage_setState>("wrong_session"+suffix,UAMSG_SETSTATE,true,cast,0,0,false,{},0,2,
            [](Fixture&,windp_recvMsg&m){m.sessionId=11;});
        check<uamessage_setState>("wrong_slot"+suffix,UAMSG_SETSTATE,true,cast,0,0,false,{},0,2,
            [](Fixture&,windp_recvMsg&m){m.playerSlot=5;});
    }
    check<uamessage_setState>("accepted_level_cast0",UAMSG_SETSTATE,true,0,1,1);
    check<uamessage_message>("rejected_owner2_lobby_chat",UAMSG_MESSAGE,false,1,0,0);
    check<uamessage_welcome>("rejected_lobby_after_local_launch",UAMSG_WELCOME,false,1,0,0,false,
        [](uamessage_welcome&m){m.fraction=1<<1;},0,0,
        [](Fixture&f,windp_recvMsg&){f.users.netLaunchFrozen=true;});
    check<uamessage_load>("accepted_host_load_owner0",UAMSG_LOAD,false,1,0,1,false,{},0,0,
        [](Fixture&,windp_recvMsg&m){m.playerId=303;m.playerSlot=0;m.senderName="host";});
    check<uamessage_load>("rejected_nonhost_load_owner0",UAMSG_LOAD,false,1,0,0,false,{},0,0);
    check<uamessage_kick>("accepted_host_kick_actor5_victim3",UAMSG_KICK,true,1,1,1,false,{},0,3,
        [](Fixture&,windp_recvMsg&m){m.playerId=303;m.playerSlot=0;m.senderName="host";},5);
    check<uamessage_kick>("rejected_nonhost_kick_victim3",UAMSG_KICK,true,1,0,0,false,{},0,3);
    check<uamessage_kick>("rejected_host_kick_host_owner5",UAMSG_KICK,true,1,0,0,false,{},0,5,
        [](Fixture&,windp_recvMsg&m){m.playerId=303;m.playerSlot=0;m.senderName="host";},5);
    std::cout<<"{\"kind\":\"summary\",\"total\":"<<passed+failed<<",\"passed\":"<<passed<<",\"failed\":"<<failed<<"}"<<std::endl;
    return failed?1:0;
}
