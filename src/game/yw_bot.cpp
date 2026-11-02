// Multiplayer bot runtime (see yw_bot.h and docs/bots-and-forks.md).
#include "yw_bot.h"

#include <cstring>

#include "game/yw.h"
#include "game/yw_net.h"
#include "game/yparobo.h"
#include "game/yw_fork.h"
#include "system/system.h"

namespace BotRuntime {
namespace {

World::NetBot::BotSpec g_spec;
World::NetBot::BotLifecycle g_lifecycle;
World::Fork::ForkRng g_rng(0xB07B07ull);
std::int32_t g_lastThinkMs = -1;
std::int32_t g_lastTauntMs = -1;
bool g_profileApplied = false;
std::uint64_t g_forkSeq = 1;

NC_STACK_yparobo *LocalRobo(NC_STACK_ypaworld *world)
{
    if (!world || !world->_userRobo)
        return nullptr;
    return dynamic_cast<NC_STACK_yparobo *>(world->_userRobo);
}

void ApplyProfile(NC_STACK_yparobo *robo)
{
    const World::NetBot::BotProfile &p = World::NetBot::ProfileForOwner(g_spec.owner);
    robo->setROBO_epConquer(p.wConquer);
    robo->setROBO_epDefense(p.wDefense);
    robo->setROBO_epRadar(p.wRadar);
    robo->setROBO_epPower(p.wPower);
    robo->setROBO_epSafety(p.wSafety);
    robo->setROBO_epReconnoitre(p.wRecon);
    robo->setROBO_epRobo(p.wRoboAttack);
    g_profileApplied = true;
}

void SendChat(NC_STACK_ypaworld *world, const char *line)
{
    if (!World::NetBot::ValidBotChatLine(line))
        return;
    uamessage_message msg;
    msg.msgID = UAMSG_MESSAGE;
    msg.owner = static_cast<std::uint8_t>(world->GetPlayerOwner());
    std::strncpy(msg.message, line, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    world->NetBroadcastMessage(&msg, sizeof(msg), true);
}

} // namespace

bool InitFromCmdLine()
{
    const int idx = System::FindCmdLineArg("-bot");
    if (idx < 0)
        return false;
    std::vector<std::string> &cmdl = System::GetCmdLineArray();
    if (idx + 1 >= static_cast<int>(cmdl.size()))
        return false;
    g_spec = World::NetBot::ParseBotSpec(cmdl[idx + 1]);
    if (!g_spec.valid)
        return false;
    g_lifecycle.spec = g_spec;
    g_lifecycle.phase = World::NetBot::BotPhase::Lobby;
    return true;
}

bool Enabled() { return g_spec.valid; }
World::NetBot::BotSpec Spec() { return g_spec; }
World::NetBot::BotPhase Phase() { return g_lifecycle.phase; }

void Tick(NC_STACK_ypaworld *world)
{
    if (!Enabled() || !world || !world->_isNetGame || !world->_isNetGameStarted)
        return;
    if (g_lifecycle.phase == World::NetBot::BotPhase::Lobby)
        g_lifecycle.phase = World::NetBot::BotPhase::InGame;
    NC_STACK_yparobo *robo = LocalRobo(world);
    if (!robo)
        return;
    // No human drives this station: route it to the native station AI
    // (AI_checkWorld/AI_doWish) instead of the player path.
    robo->SetBotControlled(true);
    if (!g_profileApplied)
        ApplyProfile(robo);

    const World::NetBot::DifficultyParams dp = World::NetBot::ParamsFor(g_spec.difficulty);
    if (g_lastThinkMs >= 0 && world->_timeStamp - g_lastThinkMs < dp.reactionMs)
        return;
    g_lastThinkMs = world->_timeStamp;

    World::Fork::WorldState state;
    if (!ForkBridge::CaptureWorldState(world, state))
        return;
    const World::NetBot::BotOrder order =
        World::NetBot::Decide(state, g_spec, g_rng, World::Fork::ForkId{g_forkSeq++});
    // Steer the native station AI (AI_layer3 wishes) toward the fork-chosen
    // order. Direct state writes are never used; the robo setters are the
    // same wish interface the shell uses for a human station.
    const World::NetBot::BotProfile &p = World::NetBot::ProfileForOwner(g_spec.owner);
    switch (order.kind)
    {
    case World::NetBot::OrderKind::Attack:
        robo->setROBO_epRobo(100);
        robo->setROBO_epConquer(p.wConquer);
        break;
    case World::NetBot::OrderKind::Build:
        robo->setROBO_epPower(100);
        robo->setROBO_epRadar(p.wRadar);
        break;
    case World::NetBot::OrderKind::Move:
        robo->setROBO_epConquer(100);
        robo->setROBO_epRobo(p.wRoboAttack);
        break;
    default:
        break;
    }

    if (World::NetBot::TauntDue(g_lastTauntMs, world->_timeStamp))
    {
        g_lastTauntMs = world->_timeStamp;
        SendChat(world, World::NetBot::TauntFor(g_spec.owner,
                                                static_cast<std::uint32_t>(g_forkSeq)));
    }
}

bool RequestLeave(NC_STACK_ypaworld *world)
{
    if (!Enabled() || !world)
        return false;
    if (!g_lifecycle.OnLeaveRequested())
        return false;
    uamessage_exit msg;
    msg.msgID = UAMSG_EXIT;
    msg.norm = 1;
    msg.owner = static_cast<std::uint8_t>(world->GetPlayerOwner());
    const bool sent = world->NetBroadcastMessage(&msg, sizeof(msg), true);
    g_lifecycle.OnDestroyed();
    return sent;
}

void Shutdown()
{
    g_spec = World::NetBot::BotSpec();
    g_profileApplied = false;
    g_lastThinkMs = -1;
    g_lastTauntMs = -1;
}

} // namespace BotRuntime
