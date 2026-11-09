#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <strings.h>
#include "game/yw_net_msgs.h"
#include "platform/windp_events.h"
#include "world/net_update_gate.h"
#include "net_ingress_types.inc"

// Only point coordinates/construction are required by the world fixture's IsSector.
// This stand-in avoids SDL from common/common.h; no admission logic is replaced.
namespace Common { struct Point { int32_t x,y; Point(int32_t a,int32_t b):x(a),y(b) {} }; }

// Minimal world fixture. Production validation and ingress are not reimplemented.
class NC_STACK_ypabact {};
class NC_STACK_ypaworld {
public:
    UserData *_GameShell = nullptr;
    NC_STACK_ypabact *_userRobo = nullptr;
    struct { int gTime = 0; } _updateMessage;
    int32_t _timeStamp = 0;
    std::vector<int> _techUpgrades = std::vector<int>(4);
    struct { std::vector<int> MapRegions = std::vector<int>(8); } _globalMapRegions;
    std::vector<int> fixtureBuildProtos = std::vector<int>(4);
    const std::vector<int> &GetBuildProtos() const { return fixtureBuildProtos; }
    bool IsSector(const Common::Point &p) const { return p.x>=0 && p.y>=0 && p.x<8 && p.y<8; }
};
static int dispatch_marker = 0;
static int captured_logs = 0;
static void log_netlog(const char *, ...) { ++captured_logs; }
