// Engine bridge for World::Fork: translates the live NC_STACK_ypaworld into
// the deterministic fork state and back. Both local what-if forks and
// server-side bot planning go through these functions and the same
// World::Fork::Fork::Step; forks never touch the net driver or renderer.
#ifndef YW_FORK_H_INCLUDED
#define YW_FORK_H_INCLUDED

#include "world/fork.h"

class NC_STACK_ypaworld;

namespace ForkBridge {

// Deep-copies the observable simulation subset (sectors + unit hierarchy)
// out of the live world. Returns false when no level is loaded.
bool CaptureWorldState(NC_STACK_ypaworld *world, World::Fork::WorldState &out);

// Validates an action batch by stepping it in a throwaway fork of the
// current live state (commit-by-replay gate for bot/host planning).
// Registers the agent bridge fork session (fork_create/step/observe/commit/
// discard) bound to the live world. Call before Agent::Init so session.json
// advertises the fork operations.
void RegisterAgentHandler(NC_STACK_ypaworld *world);

bool ValidateActions(NC_STACK_ypaworld *world, std::uint8_t controllerOwner,
                     const World::Fork::ForkActionBatch &actions, std::uint32_t ticks);

} // namespace ForkBridge
#endif
