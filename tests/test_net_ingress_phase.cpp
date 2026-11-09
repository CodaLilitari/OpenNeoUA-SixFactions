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
        users.lobbyPlayers[0].Name="actor"; users.lobbyPlayers[0].Owner=1;
        users.lobbyPlayers[0].Update.Requested(950);
    }
};
static int passed=0,failed=0;
static void emit(const std::string &name,bool ok,size_t consumed,size_t expected,int owner,
                 const Snapshot &before,const Snapshot &after,bool gateUnchanged) {
    const size_t observedOwner=owner>=0 && owner<(int)World::CVFractionsCount ? owner : 0;
    std::cout<<"{\"kind\":\"case\",\"name\":\""<<name<<"\",\"passed\":"<<(ok?"true":"false")
        <<",\"consumed\":"<<consumed<<",\"expected_consumed\":"<<expected<<",\"dispatch_marker\":"<<dispatch_marker
        <<",\"peer_gate_unchanged\":"<<(gateUnchanged?"true":"false")
        <<",\"owner\":"<<owner<<",\"observed_accounting_owner\":"<<observedOwner<<",\"before\":["<<before.values[observedOwner][0]<<","<<before.values[observedOwner][1]<<","<<before.values[observedOwner][2]
        <<"],\"after\":["<<after.values[observedOwner][0]<<","<<after.values[observedOwner][1]<<","<<after.values[observedOwner][2]<<"]}"<<std::endl;
    if(ok)++passed;else ++failed;
}
template<class M> static void check(const std::string &name,uint32_t id,bool loaded,uint32_t cast,
        int accounting,int expectedDispatch,bool malformed=false,
        const std::function<void(M&)> &configure={},int bytesAdjust=0,uint8_t owner=2) {
    Fixture fixture(loaded); M message; NetMsgClear(message);
    message.msgID=id;message.owner=owner;message.tstamp=100;message.msgCnt=7;
    if(configure)configure(message);
    Snapshot before(fixture.users),expected(before);
    if(accounting>=1)expected.values[owner][0]=5000;
    if(accounting==2){expected.values[owner][1]=100;expected.values[owner][2]=8;}
    const auto gate=fixture.users.lobbyPlayers[0].Update;
    windp_recvMsg incoming;incoming.data=&message;incoming.cast=cast;incoming.senderName="actor";
    incoming.playerId=101;incoming.playerSlot=0;incoming.size=sizeof(message)+bytesAdjust;
    dispatch_marker=0;std::string error;
    const size_t consumed=yw_handleNormMsg(&fixture.world,&incoming,incoming.size,&error);
    const Snapshot after(fixture.users);
    const auto &afterGate=fixture.users.lobbyPlayers[0].Update;
    bool unchanged=true;
    for(uint32_t time:{950u,1000u,2949u,2950u,5950u})
        unchanged=unchanged && gate.HoldsEvents(time)==afterGate.HoldsEvents(time)
            && gate.MayRequest(time)==afterGate.MayRequest(time) && gate.MayAnswer(time)==afterGate.MayAnswer(time);
    const size_t expectedSize=malformed?0:sizeof(message);
    emit(name,after==expected && dispatch_marker==expectedDispatch && consumed==expectedSize && unchanged,
         consumed,expectedSize,owner,before,after,unchanged);
}

// Explicit minimal batch caller: advances by handler's consumed size; no transport emulation.
static void mixedBatch() {
    Fixture fixture(false);
    uamessage_setState rejected;NetMsgClear(rejected);rejected.msgID=UAMSG_SETSTATE;
    rejected.owner=2;rejected.tstamp=100;rejected.msgCnt=7;
    uamessage_message chat;NetMsgClear(chat);chat.msgID=UAMSG_MESSAGE;
    chat.owner=1;chat.tstamp=200;chat.msgCnt=7;
    std::vector<uint8_t> buffer(sizeof(rejected)+sizeof(chat));
    std::memcpy(buffer.data(),&rejected,sizeof(rejected));
    std::memcpy(buffer.data()+sizeof(rejected),&chat,sizeof(chat));
    Snapshot before(fixture.users),expected(before);
    expected.values[1]={{5000,200,8}};
    size_t offset=0;dispatch_marker=0;std::string error;
    while(offset<buffer.size()) {
        windp_recvMsg incoming;incoming.data=buffer.data()+offset;incoming.cast=2;incoming.senderName="actor";
        const size_t consumed=yw_handleNormMsg(&fixture.world,&incoming,buffer.size()-offset,&error);
        if(!consumed)break;
        offset+=consumed;
    }
    Snapshot after(fixture.users);
    emit("wrong_phase_then_any_minimal_batch",after==expected && offset==buffer.size() && dispatch_marker==1,
         offset,buffer.size(),2,before,after,true);
}

int main(int argc,char **argv) {
    // Runtime check aggregation must stay effective under the target's NDEBUG.
    if(argc==2 && std::string(argv[1])=="--verify-failing-check") {
        Fixture fixture(false); Snapshot before(fixture.users);
        emit("intentional_failing_check_under_ndebug",false,0,0,0,before,before,true);
        return failed==1 ? 1 : 0;
    }
    // Live EventData::cast is bool: both provider values0/1 must isolate liveness.
    check<uamessage_sectorEnergy>("wrong_phase_level_cast1",UAMSG_SECTORENERGY,false,1,0,0);
    check<uamessage_sectorEnergy>("wrong_phase_level_cast0",UAMSG_SECTORENERGY,false,0,0,0);
    // cast2 is legacy/direct-handler branch coverage, not current provider input.
    check<uamessage_setState>("wrong_phase_level_cast2",UAMSG_SETSTATE,false,2,0,0);
    check<uamessage_welcome>("wrong_phase_lobby_cast2",UAMSG_WELCOME,true,2,0,0,false,[](uamessage_welcome&m){m.fraction=1<<1;});
    check<uamessage_crc>("wrong_phase_crc_cast2",UAMSG_CRC,true,2,0,0);
    check<uamessage_requpdate>("wrong_phase_request_cast2",UAMSG_REQUPDATE,false,2,0,0);
    check<uamessage_vhclEnergy>("wrong_phase_cross_owner_energy",UAMSG_VHCLENERGY,false,2,0,0);
    check<uamessage_endPlasma>("wrong_phase_cross_owner_plasma",UAMSG_ENDPLASMA,false,2,0,0);
    check<uamessage_setState>("accepted_level_cast2_with_trailing_bytes",UAMSG_SETSTATE,true,2,2,1,false,{},9);
    check<uamessage_setState>("accepted_level_cast1",UAMSG_SETSTATE,true,1,1,1);
    check<uamessage_welcome>("accepted_lobby_welcome",UAMSG_WELCOME,false,2,2,1,false,[](uamessage_welcome&m){m.fraction=1<<1;});
    check<uamessage_message>("accepted_any_lobby",UAMSG_MESSAGE,false,2,2,1);
    check<uamessage_message>("accepted_any_level",UAMSG_MESSAGE,true,2,2,1);
    check<uamessage_message>("accepted_owner0_chat",UAMSG_MESSAGE,true,2,0,1,false,{},0,0);
    check<uamessage_vhclEnergy>("accepted_cross_owner_energy_exception",UAMSG_VHCLENERGY,true,2,1,1);
    check<uamessage_endPlasma>("accepted_cross_owner_plasma_exception",UAMSG_ENDPLASMA,true,2,1,1);
    check<uamessage_base>("malformed_short_base",UAMSG_REQUPDATE,false,2,0,0,true,{},-1);
    check<uamessage_setState>("malformed_truncated_fixed",UAMSG_SETSTATE,true,2,0,0,true,{},-(int)(sizeof(uamessage_setState)-sizeof(uamessage_base)));
    check<uamessage_base>("malformed_owner",UAMSG_REQUPDATE,true,2,0,0,true,{},0,8);
    check<uamessage_base>("malformed_unknown_id",0xDEADBEEF,true,2,0,0,true);
    check<uamessage_welcome>("malformed_faction",UAMSG_WELCOME,false,2,0,0,true);
    check<uamessage_startBuild>("malformed_build_sector",UAMSG_STARTBUILD,true,2,0,0,true,[](uamessage_startBuild&m){m.sec_x=9;});
    check<uamessage_update>("malformed_variable_update_count",UAMSG_UPDATE,true,2,0,0,true);
    mixedBatch();
    std::cout<<"{\"kind\":\"summary\",\"total\":"<<passed+failed<<",\"passed\":"<<passed<<",\"failed\":"<<failed<<"}"<<std::endl;
    return failed?1:0;
}
