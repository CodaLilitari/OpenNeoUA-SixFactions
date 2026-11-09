// Real production client ingress with an authenticated host endpoint. Deliberate
// malformed or reordered control frames exercise the actual socket/adapter path.
#include <SDL2/SDL.h>
#include "../src/netlib/zndNet.h"
#include "../src/netlib/enet_znd.h"
#include "../src/world/net_protocol.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <functional>
using namespace ZNDNet;
static void Check(bool ok,const char*m){if(!ok)throw std::runtime_error(m);}
class NativeHost:public ZNDSingle{
public:using ZNDSingle::ZNDSingle;
 uint16_t Port(){return SDLNet_Read16(&SDLNet_UDP_GetPeerAddress(sock,-1)->port);}
 RosterState State(){SDL_LockMutex(eSyncMutex);auto state=roster;SDL_UnlockMutex(eSyncMutex);return state;}
 void Binding(uint64_t id,uint64_t&a,uint32_t&g){SDL_LockMutex(eSyncMutex);auto*u=users.FindByID(id);Check(u!=nullptr,"binding missing");a=u->attempt;g=u->bindingGeneration;SDL_UnlockMutex(eSyncMutex);}
 void Inject(uint64_t id,const std::vector<uint8_t>&bytes){SDL_LockMutex(eSyncMutex);auto*u=users.FindByID(id);Check(u!=nullptr,"inject target missing");Send_PushData(MkSendingData(u,RefDataStatic::create(const_cast<uint8_t*>(bytes.data()),bytes.size()),PKT_FLAG_GARANT,0));SDL_UnlockMutex(eSyncMutex);}
};
struct Fixture{
 bool enet;std::unique_ptr<NativeHost> znd;std::unique_ptr<EnetTransport> native;std::unique_ptr<ZNDNet::ZNDNet> client;RosterState initial;uint64_t attempt=0,id=0;uint32_t generation=0,peer=0;
 explicit Fixture(bool useEnet):enet(useEnet){const std::string svc=World::NetProtocol::ServiceId;IPaddress addr{};
  if(enet){native.reset(new EnetTransport(svc));Check(native->StartHost("host",0,3),"ENet host failed");SDLNet_ResolveHost(&addr,"127.0.0.1",native->BoundPort());auto*p=new ZNDEnet(svc);client.reset(p);p->Start("one",addr);
   Check(Wait([&]{return peer!=0;},3000),"ENet handshake missing");id=uint64_t(peer)+1;Check(native->PeerBinding(peer,attempt,generation),"ENet binding missing");initial.session=native->SessionId();initial.name="control";initial.rows={{1,0,RosterOnline,"host"},{id,1,RosterOnline,"one"}};initial.Changed(initial.rows.back(),Transition::Fresh);Send(EncodeRoster(initial,attempt,generation));
  }else{znd.reset(new NativeHost(svc));znd->Start("host",0,"control",4);SDLNet_ResolveHost(&addr,"127.0.0.1",znd->Port());auto*p=new ZNDClient(svc);client.reset(p);p->Start("one",addr);Check(Wait([&]{return client->GetMyID()!=0;},3000),"ZND handshake missing");id=client->GetMyID();initial=znd->State();znd->Binding(id,attempt,generation);}
  Check(Wait([&]{UserInfoVect rows;return client->GetUsers(rows)&&rows.size()==2&&Status()==NetUser::STATUS_CONNECTED;},3000),"initial roster missing");Drain();
 }
 ~Fixture(){client->Stop();if(znd)znd->Stop();if(native)native->Stop();}
 uint8_t Status(){return enet?static_cast<ZNDEnet&>(*client).GetStatus():static_cast<ZNDClient&>(*client).GetStatus();}
 void Drive(){if(native){TransportEvent e;if(native->Poll(e,0)&&e.type==TransportEvent::Type::Connected)peer=e.peer;}SDL_Delay(1);}
 bool Wait(const std::function<bool()>&predicate,unsigned ms){auto end=TransportNow()+ms;do{Drive();if(predicate())return true;}while(TransportNow()<end);return false;}
 void Pause(unsigned ms){auto end=TransportNow()+ms;while(TransportNow()<end)Drive();}
 void Send(const std::vector<uint8_t>&bytes){if(native)Check(native->Send(peer,bytes.data(),bytes.size(),WireChannel::Reliable),"ENet control send");else znd->Inject(id,bytes);}
 void Drain(){while(auto*e=client->Events_Pop())delete e;}
 bool SameRows(){UserInfoVect rows;if(!client->GetUsers(rows)||rows.size()!=2)return false;return rows[0].ID==initial.rows[0].id&&rows[0].slot==0&&rows[0].status==RosterOnline&&rows[1].ID==id&&rows[1].slot==1&&rows[1].name=="one"&&rows[1].status==RosterOnline;}
};
static void MissingSelf(bool enet){Fixture f(enet);auto state=f.initial;state.rows.resize(1);state.Changed(state.rows[0],Transition::Resumed);f.Send(EncodeRoster(state,f.attempt,f.generation));f.Pause(150);Check(f.SameRows(),"roster without the accepted local identity was applied");}
static void Malformed(bool enet){Fixture f(enet);auto state=f.initial;++state.revision;state.history.clear();auto valid=EncodeRoster(state,f.attempt,f.generation);std::vector<std::vector<uint8_t>> invalid;
 auto x=valid;x.push_back(0x7f);invalid.push_back(x);x=valid;x[1]^=1;invalid.push_back(x);x=valid;x[9]^=1;invalid.push_back(x);x=valid;x[13]^=1;invalid.push_back(x);
 const size_t count=30+state.name.size();x=valid;x[count]=7;invalid.push_back(x);
 size_t first=count+1;size_t second=first+11+state.rows[0].name.size();x=valid;x[second+8]=0;invalid.push_back(x);x=valid;x[second+9]=7;invalid.push_back(x);
 x=valid;x.resize(x.size()-1);invalid.push_back(x);
 for(const auto&bytes:invalid){f.Send(bytes);f.Pause(60);Check(f.SameRows(),"malformed or mismatched control changed roster");Check(f.client->Events_Pop()==nullptr,"malformed control emitted lifecycle event");}
 if(!enet){ControlWriter w;w.U(USR_MSG_SES_USERJOIN,1);w.U(999,8);w.Str("legacy");f.Send(w.b);f.Pause(60);Check(f.SameRows(),"legacy roster delta bypassed revision validation");}
}
static void History(bool enet){std::cerr<<"history begin\n";Fixture f(enet);std::cerr<<"history connected\n";auto next=f.initial;RosterRow ghost{999,2,RosterOnline,"ghost"};next.rows.push_back(ghost);next.Changed(ghost,Transition::Fresh);auto intermediate=next;next.rows.pop_back();ghost.status=RosterAway;next.Changed(ghost,Transition::Left);
 std::cerr<<"history sending small\n";f.Send(EncodeRoster(next,f.attempt,f.generation));f.Send(EncodeRoster(intermediate,f.attempt,f.generation));f.Send(EncodeRoster(next,f.attempt,f.generation));unsigned fresh=0,left=0;
 Check(f.Wait([&]{while(auto*e=f.client->Events_Pop()){std::unique_ptr<Event>hold(e);if(e->type==EVENT_USER_ADD||e->type==EVENT_USER_LEAVE){auto*n=static_cast<EventNameID*>(e);Check(n->id==999&&n->slot==2,"history lost identity/event-time slot");if(e->type==EVENT_USER_ADD&&e->value==JoinFresh)++fresh;if(e->type==EVENT_USER_LEAVE&&e->value==LeaveLeft)++left;}}return fresh==1&&left==1;},1500),"retained history did not emit both transitions");f.Pause(100);while(auto*e=f.client->Events_Pop()){std::unique_ptr<Event>hold(e);Check(e->type!=EVENT_USER_ADD&&e->type!=EVENT_USER_LEAVE,"duplicate/reordered control emitted duplicate transition");}Check(f.SameRows(),"reordered roster regressed final state");
 std::cerr<<"history small done\n";for(unsigned i=0;i<65;++i)next.Changed(ghost,i%2?Transition::Left:Transition::Fresh);std::cerr<<"history sending large\n";f.Send(EncodeRoster(next,f.attempt,f.generation));unsigned lists=0;Check(f.Wait([&]{while(auto*e=f.client->Events_Pop()){std::unique_ptr<Event>hold(e);Check(e->type!=EVENT_USER_ADD&&e->type!=EVENT_USER_LEAVE,"history overflow invented missing transitions");if(e->type==EVENT_USER_LIST&&e->value==1)++lists;}return lists==1;},2000),"history overflow did not signal consolidated repair");Check(f.SameRows(),"history overflow changed final rows");std::cerr<<"history done\n";}
int main(int argc,char**argv){SDL_SetMainReady();if(SDL_Init(SDL_INIT_TIMER)||SDLNet_Init())return 2;int rc=0;try{std::string mode=argc>1?argv[1]:"znd_missing";bool enet=mode.find("enet")==0;if(mode.find("missing")!=std::string::npos)MissingSelf(enet);else if(mode.find("malformed")!=std::string::npos)Malformed(enet);else History(enet);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';rc=1;}SDLNet_Quit();SDL_Quit();return rc;}
