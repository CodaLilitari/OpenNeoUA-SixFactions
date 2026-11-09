// Only omitted engine state/sinks. Identity declarations are generated from yw.h.
EnvAction envAction{};
bool noSent=false;
NC_STACK_ypaworld *p_YW=nullptr;
bool isHost=false, remoteMode=false, HasInited=true, rdyStart=false, blocked=false;
enum { NETSCREEN_INSESSION=4 };
int netSelMode=NETSCREEN_INSESSION, netLevelID=1, netProblem=0;
std::string netPlayerName="local",netProblemName;
uint8_t netPlayerOwner=1, SelectedFraction=1, FreeFraction=63;
int update_time_norm=1000,sendScoreCountDown=1000,DTime=0,problemTimer=0,netProblemCountDown=0;
uint32_t deadCheckTime=0;
uint32_t netrecv_count=0,netrecv_time=0,netsend_count=0,netrecv_speed=0,netsend_speed=0;
uint32_t netrecv_bpsMin=0,netrecv_bpsMax=0,netrecv_bpsAvr=0;
uint32_t netsend_bpsMin=0,netsend_bpsMax=0,netsend_bpsAvr=0;
uint32_t net_gcnt=0,net_packet_size=0,net_packet_min=0,net_packet_max=0,net_packet_cnt=0,net_packet_avr=0;
