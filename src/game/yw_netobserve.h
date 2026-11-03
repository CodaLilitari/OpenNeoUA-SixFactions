// Fills the agent observation's lobby and network state (Agent::NetState,
// reported under "net" by observe/status; see docs/agent-control.md).
// Read-only: it never changes game, lobby or transport state.
#ifndef YW_NETOBSERVE_H_INCLUDED
#define YW_NETOBSERVE_H_INCLUDED

#include "agent/net_observation.h"

class NC_STACK_ypaworld;

namespace NetObserve {

void Fill(NC_STACK_ypaworld *world, Agent::NetState &out);

} // namespace NetObserve
#endif
