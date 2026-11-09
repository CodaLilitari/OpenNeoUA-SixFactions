// Boundary fixture, not a recovery implementation. Sends are always recorded:
// the fake never supplies the provider's gameplay gate for it.
#ifndef PROVIDER_TEST_FAKE_TRANSPORT_H
#define PROVIDER_TEST_FAKE_TRANSPORT_H
namespace ZNDNet {
struct NetUser { enum { STATUS_DISCONNECTED=0, STATUS_CONNECTING=1, STATUS_CONNECTED=2 }; };
struct UserInfo {
    uint64_t ID=0; std::string name; bool lead=false; uint8_t slot=0xff;
    uint8_t status=RosterOnline;
};
using UserInfoVect = std::vector<UserInfo>;
struct SessionInfo { uint64_t ID=0; std::string name; };
using SessionInfoVect = std::vector<SessionInfo>;
constexpr int PKT_FLAG_GARANT=1;
}
// Event context is immutable in production. Fake enqueue constructs the same
// concrete event with the requested context instead of mutating that field.
inline ZNDNet::Event *WithContext(ZNDNet::Event *event,uint64_t session) {
    ZNDNet::Event *copy=nullptr;
    if(event->type==ZNDNet::EVENT_DATA) {
        auto *data=static_cast<ZNDNet::EventData*>(event);
        copy=new ZNDNet::EventData(data->type,data->value,data->from,data->cast,data->to,
                                  data->size,data->data,data->channel,session);
    } else if(event->type==ZNDNet::EVENT_CONNECTED || event->type==ZNDNet::EVENT_SESSION_JOIN ||
              event->type==ZNDNet::EVENT_USER_ADD || event->type==ZNDNet::EVENT_USER_LEAVE) {
        auto *named=static_cast<ZNDNet::EventNameID*>(event);
        copy=new ZNDNet::EventNameID(named->type,named->value,named->name,named->id,named->slot,session);
    } else copy=new ZNDNet::Event(event->type,event->value,session);
    delete event; return copy;
}
namespace ProviderFixture {
using Token=World::NetProtocol::ResumeToken;
enum class StartMode { Accept, Silent, Refuse };
struct StartSpec {
    StartMode mode=StartMode::Accept;
    uint64_t id=30, session=100;
    uint32_t refusal=ZNDNet::ERR_CONN_TIMEOUT;
    bool malformedRoster=false, noToken=false;
};
static StartSpec nextStart;
static uint64_t nextHostId=10, nextHostSession=100;
struct StartCall { IPaddress address; std::string name; Token token{}; bool resumeOnly=false; };
struct SendCall { bool broadcast=false; uint64_t to=0; std::vector<uint8_t> bytes; int flags=0; };
struct Trace {
    std::string kind;
    std::vector<StartCall> starts;
    std::vector<SendCall> sends;
    unsigned stops=0, destroyed=0, disconnects=0, tokenClears=0, closed=0;
    std::vector<bool> resumeOnlySettings;
    std::vector<uint32_t> graceSettings;
    std::vector<bool> showSettings;
    std::vector<uint64_t> kicks;
};
static std::vector<std::shared_ptr<Trace>> traces;
static bool configureFails=false;
inline std::size_t SendCalls() {
    std::size_t n=0; for(const auto &t:traces) n+=t->sends.size(); return n;
}
inline ZNDNet::UserInfoVect Rows(uint64_t own=30) {
    return {{10,"HOST",true,0,ZNDNet::RosterOnline},
            {20,"TWO",false,2,ZNDNet::RosterOnline},
            {own,"FIVE",false,5,ZNDNet::RosterOnline}};
}
}
namespace ZNDNet {
class ZNDNet {
public:
    std::shared_ptr<ProviderFixture::Trace> trace;
    int status=NetUser::STATUS_DISCONNECTED;
    uint64_t id=0, session=0;
    std::string name;
    UserInfoVect rows;
    std::deque<Event*> queue;
    std::vector<ProviderFixture::SendCall> outgoing;
    ProviderFixture::Token token{};
    bool resumeOnly=false, running=false;
    bool failGetUsers=false;
    uint32_t graceMs=60000;
    explicit ZNDNet(const std::string &kind):trace(std::make_shared<ProviderFixture::Trace>()) {
        trace->kind=kind; ProviderFixture::traces.push_back(trace);
    }
    virtual ~ZNDNet() { ++trace->destroyed; Events_Clear(); }
    void Push(Event *event) { queue.push_back(WithContext(event,session)); }
    void Events_Clear() { for(auto *event:queue) delete event; queue.clear(); }
    Event *Events_Pop() {
        if(queue.empty()) return nullptr;
        auto *event=queue.front(); queue.pop_front(); return event;
    }
    Event *Events_PopSystem() {
        for(auto i=queue.begin();i!=queue.end();++i) if((*i)->type!=EVENT_DATA) {
            auto *event=*i; queue.erase(i); return event;
        }
        return nullptr;
    }
    virtual void Stop() {
        ++trace->stops; running=false; status=NetUser::STATUS_DISCONNECTED;
        id=session=0; rows.clear(); outgoing.clear(); Events_Clear();
        // Like both adapters, stop retains the bearer token; provider chooses
        // whether this is terminal and explicitly clears it in that case.
    }
    void Accept(const ProviderFixture::StartSpec &spec) {
        id=spec.id; session=spec.session; status=NetUser::STATUS_CONNECTED;
        rows=ProviderFixture::Rows(id);
        rows.back().name=name;
        if(spec.malformedRoster) rows.back().slot=2;
        if(spec.noToken) token=ProviderFixture::Token{};
        else if(!World::NetProtocol::TokenValid(token)) token.b[0]=0x5a;
        // Both transports publish acceptance identity/session before self-resume.
        Push(new Event(EVENT_USER_LIST,1));
        Push(new EventNameID(EVENT_CONNECTED,0,name,id));
        Push(new EventNameID(EVENT_SESSION_JOIN,0,"fixture-session",session));
        if(resumeOnly) Push(new Event(EVENT_SELF_RESUMED,0));
    }
    void Start(const std::string &requestedName, const IPaddress &address) {
        trace->starts.push_back({address,requestedName,token,resumeOnly});
        name=requestedName; running=true; status=NetUser::STATUS_CONNECTING;
        Events_Clear(); rows.clear(); id=session=0;
        const auto spec=ProviderFixture::nextStart;
        if(spec.mode==ProviderFixture::StartMode::Accept) Accept(spec);
        else if(spec.mode==ProviderFixture::StartMode::Refuse) {
            status=NetUser::STATUS_DISCONNECTED; Push(new Event(EVENT_CONNERR,spec.refusal));
        }
    }
    void Start(const std::string &requestedName, uint16_t, const std::string &, int) {
        Stop(); // Both real host adapters stop the previous session first.
        name=requestedName; running=true;
        status=trace->kind=="enet" ? NetUser::STATUS_CONNECTING : NetUser::STATUS_CONNECTED;
        id=ProviderFixture::nextHostId; session=ProviderFixture::nextHostSession;
        rows=ProviderFixture::Rows(); rows[0].ID=id; rows[0].name=name;
    }
    int GetStatus() const { return status; }
    uint64_t GetMyID() const { return id; }
    uint64_t GetMySesID() const { return session; }
    bool GetUsers(UserInfoVect &destination) {
        if(failGetUsers) return false;
        // ZNDClient leaves destination untouched while offline; ENet replaces it.
        if(trace->kind=="znd" && status!=NetUser::STATUS_CONNECTED) return false;
        destination=rows; return !rows.empty();
    }
    bool GetSessions(SessionInfoVect &destination) { destination.clear(); return true; }
    bool GetResumeToken(ProviderFixture::Token &destination) const {
        destination=token; return World::NetProtocol::TokenValid(token);
    }
    bool SetResumeToken(const ProviderFixture::Token &value) {
        if(ProviderFixture::configureFails) return false;
        token=value; return World::NetProtocol::TokenValid(token);
    }
    void ClearResumeToken() { ++trace->tokenClears; token=ProviderFixture::Token{}; }
    bool SetResumeOnly(bool value) {
        trace->resumeOnlySettings.push_back(value); resumeOnly=value;
        return !ProviderFixture::configureFails;
    }
    uint32_t GetResumeGraceMs() const { return graceMs; }
    bool SetResumeGraceMs(uint32_t value) {
        trace->graceSettings.push_back(value); graceMs=value;
        return !ProviderFixture::configureFails;
    }
    void SendDisconnect() { ++trace->disconnects; }
    void KickUser(uint64_t id) { trace->kicks.push_back(id); }
    void CloseSession(uint32_t) { ++trace->closed; }
    void ShowSession(bool value) { trace->showSettings.push_back(value); }
    void SendData(uint64_t to, void *data, size_t size, int flags) {
        Record(false,to,data,size,flags);
    }
    void BroadcastData(void *data, size_t size, int flags) {
        Record(true,0,data,size,flags);
    }
    void Record(bool broadcast,uint64_t to,void *data,size_t size,int flags) {
        auto *first=static_cast<uint8_t*>(data);
        ProviderFixture::SendCall call{broadcast,to,{first,first+size},flags};
        trace->sends.push_back(call); outgoing.push_back(call);
    }
};
class ZNDClient: public ZNDNet { public: explicit ZNDClient(const char*):ZNDNet("znd"){} };
class ZNDSingle: public ZNDNet { public: explicit ZNDSingle(const char*):ZNDNet("znd-host"){} };
class ZNDEnet: public ZNDNet { public: explicit ZNDEnet(const char*):ZNDNet("enet"){} };
}
#endif
