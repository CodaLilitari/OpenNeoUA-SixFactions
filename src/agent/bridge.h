#ifndef OPENNEOUA_AGENT_BRIDGE_H
#define OPENNEOUA_AGENT_BRIDGE_H
#include <cstdint>
union SDL_Event;
namespace Agent {
struct Snapshot {
    const char *screen = "unknown";
    int level = -1, owner = 0;
    bool localUnit = false, localHost = false;
    uint32_t unitId = 0;
    int prototype = 0, unitOwner = 0, energy = 0, hostEnergy = 0;
    double x = 0, y = 0, z = 0;
};
// Disabled unless OPENNEOUA_AGENT_DIR names an existing absolute directory.
void Init();
void BeforeEvents();
void BeforeSwap();
void AfterFrame(const Snapshot &snapshot);
void Shutdown();
int EventFilter(void *, SDL_Event *event);
}
#endif
