// Multiplayer bot runtime: one bot = this engine process playing one faction
// through the normal netcode (same ServiceId/UAMSG lifecycle as a human).
// Enabled with:  OpenNeoUA -bot Ghorkov:Veteran   (see docs/bots-and-forks.md)
// The brain (src/world/net_bot.h) only consumes fog-filtered fork state and
// steers the station through the same robo wish setters a player uses.
#ifndef YW_BOT_H_INCLUDED
#define YW_BOT_H_INCLUDED

#include "world/net_bot.h"

class NC_STACK_ypaworld;

namespace BotRuntime {

bool InitFromCmdLine();          // parses -bot <Faction:Difficulty>
bool Enabled();
World::NetBot::BotSpec Spec();
World::NetBot::BotPhase Phase();
void Tick(NC_STACK_ypaworld *world); // call once per gameplay frame
bool RequestLeave(NC_STACK_ypaworld *world); // quit like a human: abort + UAMSG_EXIT
void Shutdown();

} // namespace BotRuntime
#endif
