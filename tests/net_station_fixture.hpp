#ifndef NET_STATION_FIXTURE_HPP
#define NET_STATION_FIXTURE_HPP
// Engine object/RTTI stand-ins only. All admission, station traversal, wire
// validation and mutations under test are extracted from production yw_net.cpp.
enum { BACT_TYPES_ROBO = 3, BACT_TYPES_GUN = 9 };
class NC_STACK_ypabact {
public:
    virtual ~NC_STACK_ypabact() = default;
    uint8_t _owner=0;
    uint32_t _gid=0;
    int _bact_type=0;
    int32_t _lastFrmStamp=0;
    std::vector<NC_STACK_ypabact*> _kidList;
    bool fixtureViewer=false;
    void setBACT_viewer(bool value) { fixtureViewer=value; }
};
class NC_STACK_ypagun : public NC_STACK_ypabact {
public:
    bool fixtureRoboGun=true;
    NC_STACK_ypagun() { _bact_type=BACT_TYPES_GUN; }
    bool IsRoboGun() { return fixtureRoboGun; }
};
struct TLevelInfo { enum { STATE_ABORTED=99 }; int State=0; };
#endif
