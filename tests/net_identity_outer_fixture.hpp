// Exact outer/lifecycle fixture; no real engine, socket or renderer is started.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>
#include <strings.h>
#include "game/yw_net_msgs.h"
#include "platform/windp_events.h"
#include "platform/windp_resume.h"
#include "world/net_update_gate.h"
#include "world/net_lobby.h"

// Engine sinks below are not an authorization implementation.
class NC_STACK_ypaworld;
#include "net_identity_outer_types.inc"

namespace Common {struct Point {int32_t x,y;Point(int32_t a,int32_t b):x(a),y(b){}};}

struct Sent {uint32_t id;std::string destination;uint64_t boundSession=0,boundPlayer=0;};
struct Sinks {
    int logs=0,crc=0,clean=0,locks=0,deleted=0,flushes=0;
    std::vector<Sent> sent;
    std::vector<uint8_t> lastBroadcast;
};
static Sinks sinks;
static int dispatch_marker=0;
static void log_netlog(const char*,...){++sinks.logs;}

struct TDPPlayerData {
    uint32_t Index=0;uint64_t ID=0;std::string name;uint32_t flags=0;
    bool IsItMe()const{return flags&1;} bool IsOnline()const{return !(flags&2);}
    bool IsHost()const{return flags&4;}
};
class NC_STACK_windp {
public:
    std::deque<windp_recvMsg> events;
    std::vector<TDPPlayerData> players;
    WindpResume::Phase fixturePhase=WindpResume::Phase::Connected;
    bool Recv(windp_recvMsg*out){
        if(events.empty())return false;
        *out=std::move(events.front());events.pop_front();
        out->data=out->_data.empty()?nullptr:out->_data.data();
        out->size=uint32_t(out->_data.size());return true;
    }
    uint32_t GetPlayerCount()const{return uint32_t(players.size());}
    bool HasValidRoster()const{return true;} // Test input: these fixtures supply a validated roster.
    std::vector<TDPPlayerData> GetPlayersData()const{return players;}
    int32_t GetMyIndex()const{for(auto&p:players)if(p.IsItMe())return p.Index;return -1;}
    bool GetPlayerData(uint32_t slot,TDPPlayerData*out)const{
        for(auto&p:players)if(p.Index==slot){*out=p;return true;}
        return false;
    }
    int32_t GetPlayerIndex(const std::string&n)const{for(auto&p:players)if(!StriCmp(p.name,n))return p.Index;return -1;}
    bool IsPlayer(const std::string&n)const{return GetPlayerIndex(n)>=0;}
    WindpResume::Phase RecoveryPhase()const{return fixturePhase;}
    void LockSession(int*){++sinks.locks;}
    void FlushBroadcastBuffer(){++sinks.flushes;}
};
#include "net_station_fixture.hpp"
class NC_STACK_ypaworld {
public:
    UserData *_GameShell=nullptr;NC_STACK_windp *_netDriver=nullptr;
    NC_STACK_ypabact *_userRobo=nullptr;
    struct {int32_t gTime=0,frameTime=0;} _updateMessage;
    int32_t _timeStamp=0,_netUpdateTimeStamp=0;
    int32_t _netStartTimer=150000;
    bool _netInterpolate=false,_isNetGameStarted=false,_netInfoOverkill=false;
    TLevelInfo _levelInfo;
    std::vector<NC_STACK_ypabact*> _unitsList;
    NC_STACK_ypabact *yw_getHostByOwner(uint8_t owner);
    size_t FixtureStartupSend();
    void DeleteLevel(){++sinks.deleted;_userRobo=nullptr;}
    std::array<World::TPlayerStatus,World::CVFractionsCount> _gameplayStats{};
    std::vector<int> _techUpgrades=std::vector<int>(4);
    struct {std::vector<int> MapRegions=std::vector<int>(8);} _globalMapRegions;
    std::vector<int> fixtureBuildProtos=std::vector<int>(4);
    const std::vector<int>&GetBuildProtos()const{return fixtureBuildProtos;}
    bool IsSector(const Common::Point&p)const{return p.x>=0&&p.y>=0&&p.x<8&&p.y<8;}
    void NetBroadcastMessage(void*p,size_t n,bool){
        sinks.sent.push_back({static_cast<uamessage_base*>(p)->msgID,"*"});
        const auto*b=static_cast<const uint8_t*>(p);sinks.lastBroadcast.assign(b,b+n);
    }
    void NetSendMessage(void*p,size_t,const std::string&dest,bool){
        Sent event{static_cast<uamessage_base*>(p)->msgID,dest};
        // Observe identity at the send boundary, not merely after CREATE returns.
        for(const auto&r:_GameShell->lobbyPlayers)if(r.Name==dest){event.boundSession=r.SessionId;event.boundPlayer=r.PlayerId;break;}
        sinks.sent.push_back(event);
    }
    bool NetSendMessageToPlayer(void*p,size_t,uint64_t player,bool){
        Sent event{static_cast<uamessage_base*>(p)->msgID,"<unbound>"};
        for(const auto&r:_GameShell->lobbyPlayers)if(r.PlayerId==player){
            event.destination=r.Name;event.boundSession=r.SessionId;event.boundPlayer=r.PlayerId;break;
        }
        sinks.sent.push_back(event);
        return true; // Stub successful transport acceptance; no policy is performed.
    }
    void SendCRC(int){++sinks.crc;}
};
static bool SPEED_DOWN_NET=false;
static uamessage_vhclDataI vhcldata_interpolate;
static uamessage_vhclDataE vhcldata_exact;
static uint32_t HNDL_MSG[108]{};
static bool yw_prepareVHCLData(NC_STACK_ypaworld*,uamessage_vhclData*){return false;}
static void yw_cleanPlayer(NC_STACK_ypaworld*,const std::string&,int,int){++sinks.clean;}
