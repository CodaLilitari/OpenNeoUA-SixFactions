// Unattended lobby flags, engine side (see yw_netauto.h). Each frame the
// shell state goes into a LobbyView; the driver in world/net_lobby_flags.h
// decides, and the action runs here through the existing lobby code.
#include "game/yw_netauto.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include <SDL2/SDL.h>

#include "common/log.h"
#include "game/yw.h"
#include "game/yw_bot.h"
#include "gui/button.h"
#include "platform/windp.h"
#include "system/system.h"
#include "world/net_lobby_flags.h"

namespace NetAuto {
namespace {
namespace Flags = World::NetLobbyFlags;

std::unique_ptr<Flags::Driver> g_driver; // holds a widget press until the screen takes it
bool g_startAlone = false; // the press taken in this shell frame is a lone host's Start

void Log(const std::string &line)
{
    ypa_log_out("netauto: %s\n", line.c_str());
}

void LogError(const std::string &line)
{
    ypa_log_out("netauto: ERROR: %s\n", line.c_str());
    std::fprintf(stderr, "OpenNeoUA netauto: ERROR: %s\n", line.c_str());
    std::fflush(stderr);
}

bool WidgetEnabled(NC_STACK_button *buttons, int id)
{
    if (!buttons)
        return false;
    const int index = buttons->GetIndexByID(id);
    return index >= 0 && static_cast<size_t>(index) < buttons->field_d8.size() &&
           !(buttons->field_d8[index].flags & NC_STACK_button::FLAG_DISABLED);
}

Flags::Screen ScreenOf(int mode)
{
    switch (mode)
    {
    case UserData::NETSCREEN_MODE_SELECT:    return Flags::Screen::Provider;
    case UserData::NETSCREEN_SESSION_SELECT: return Flags::Screen::SessionSelect;
    case UserData::NETSCREEN_ENTER_NAME:     return Flags::Screen::Name;
    case UserData::NETSCREEN_ENTER_IP:       return Flags::Screen::Address;
    case UserData::NETSCREEN_CHOOSE_MAP:     return Flags::Screen::Map;
    case UserData::NETSCREEN_INSESSION:      return Flags::Screen::Session;
    default:                                 return Flags::Screen::Other;
    }
}

void BuildView(NC_STACK_ypaworld *world, Flags::LobbyView &v)
{
    UserData &usr = *world->_GameShell;
    NC_STACK_windp *driver = world->_netDriver;
    const auto &regions = world->_globalMapRegions.MapRegions;

    v.nowMs = SDL_GetTicks();
    v.shellOpen = usr.HasInited;
    v.titleScreen = usr.EnvMode == ENVMODE_TITLE;
    v.netMenu = usr.EnvMode == ENVMODE_NETPLAY;
    v.screen = ScreenOf(usr.netSelMode);
    v.started = world->_isNetGameStarted;
    v.isHost = usr.isHost;
    v.levelId = usr.netLevelID;
    if (usr.netLevelID > 0 && static_cast<size_t>(usr.netLevelID) < regions.size())
        v.mapFactions = regions[usr.netLevelID].FractionsBits & World::NetFactions::Mask;
    v.selectedFaction = usr.SelectedFraction;
    v.readySent = usr.rdyStart;
    v.welcomed = usr.isWelcmd;
    v.profileCallsign = usr.netPlayerName;

    // Widget states as the last shell frame left them: a press is only made
    // where a player could click.
    NC_STACK_button *buttons = usr.network_button;
    v.okEnabled = WidgetEnabled(buttons, Flags::WidgetOk);
    v.readyEnabled = WidgetEnabled(buttons, Flags::WidgetReady);
    for (const int owner : World::NetFactions::Owners)
        if (WidgetEnabled(buttons, Flags::FactionWidget(owner)))
            v.enabledFactions |= 1u << owner;

    if (driver)
        v.connectFailure = driver->_connectFailure;
    if (driver && driver->zcon)
    {
        v.rosterCanStart = usr.isHost && usr.CheckNetLobbyRoster().CanStart();
        v.clientConnected = driver->GetMode() == 2 && driver->IsConnected();
        for (const TDPPlayerData &p : driver->GetPlayersData())
        {
            Flags::Player player;
            player.name = p.name;
            player.me = p.IsItMe();
            if (p.Index < usr.lobbyPlayers.size())
            {
                const UserData::TNetPlayerLobbyData &lobby = usr.lobbyPlayers[p.Index];
                // As CheckNetLobbyRoster sees them: the host is always ready.
                player.faction = player.me ? usr.SelectedFraction : lobby.NetFraction;
                player.ready = player.me ? (usr.isHost || usr.rdyStart) : lobby.Ready;
            }
            v.players.push_back(player);
        }
    }
    for (const UserData::TMapDescription &desc : usr.mapDescriptions)
    {
        Flags::Level level;
        level.id = desc.id;
        if (desc.id >= 0 && static_cast<size_t>(desc.id) < regions.size())
            level.starts = regions[desc.id].RoboCount;
        v.levels.push_back(level);
    }
}

void Execute(NC_STACK_ypaworld *world, const Flags::Action &a)
{
    UserData &usr = *world->_GameShell;
    switch (a.kind)
    {
    case Flags::ActionKind::OpenMenu:
        usr.GameShellUiOpenNetwork(); // the main menu's Multiplayer button
        break;
    case Flags::ActionKind::ChooseProvider:
        usr.netSel = a.host ? 0 : 1;  // provider list: 0 host, 1 join; then OK
        usr.yw_NetOKProvider();
        break;
    case Flags::ActionKind::SubmitText:
        usr.netName = a.text;         // the name or address field; the driver then presses OK (1200)
        usr.netNameCurPos = static_cast<int16_t>(usr.netName.size());
        break;
    case Flags::ActionKind::ChooseLevel:
        for (const UserData::TMapDescription &desc : usr.mapDescriptions)
            if (desc.id == a.levelId)
            {
                usr.netLevelID = static_cast<int16_t>(desc.id);
                usr.netLevelName = desc.pstring;
                break;
            }
        usr.netSel = static_cast<int16_t>(a.listIndex); // list entry; OK (1200) -> AfterMapChoose
        break;
    case Flags::ActionKind::JoinSession:
        usr.JoinLobbyLessGame();      // what ConnectToServer runs after a connect
        break;
    case Flags::ActionKind::PressCode:  // faction button, Ready (1208) or Start (1200)
    case Flags::ActionKind::StartAlone: // Start (1200) that launches without the confirmation
        break;                          // queued in the driver: the screen takes it in its frame
    default:
        break;
    }
}

std::string Plan(const Flags::Options &o, int faction)
{
    std::string plan = o.Host() ? "host level " + std::to_string(o.hostLevel) : "join " + o.joinAddress;
    plan += " as " + (o.name.empty() ? std::string("the profile callsign") : o.name);
    plan += ", faction " + (faction ? std::string(Flags::FactionName(faction)) : std::string("as the lobby assigns"));
    if (faction && !o.faction)
        plan += " (from -bot)";
    if (o.Join())
        plan += o.ready ? ", send READY" : ", no READY";
    if (o.Host())
        plan += o.startWhen ? ", start when " + std::to_string(o.startWhen) + " players are ready" : ", no start";
    const char *transport = std::getenv("OPENNEOUA_TRANSPORT");
    plan += std::string(", transport ") + (transport && std::string(transport) == "enet" ? "enet" : "zndnet");
    return plan;
}

} // namespace

bool InitFromCmdLine()
{
    g_driver.reset();
    const Flags::Options options = Flags::Parse(System::GetCmdLineArray());
    if (!options.any)
        return false;
    const int botFaction = BotRuntime::Enabled() ? BotRuntime::Spec().owner : 0;
    g_driver.reset(new Flags::Driver(options, botFaction));
    if (g_driver->step() == Flags::Step::Failed)
    {
        for (const std::string &error : options.errors)
            LogError(error);
        LogError("the -net-* flags are not used; the game stays interactive");
        return false;
    }
    Log(Plan(options, g_driver->wantedFaction()));
    return true;
}

void Tick(NC_STACK_ypaworld *world)
{
    g_startAlone = false; // only for the shell frame that took the press, which is over
    if (!g_driver || !world || !world->_GameShell)
        return;
    const Flags::Step before = g_driver->step();
    if (before == Flags::Step::Off || before == Flags::Step::Failed || before == Flags::Step::InGame)
        return;
    Flags::LobbyView view;
    BuildView(world, view);
    const Flags::Action action = g_driver->Next(view);
    if (action.kind == Flags::ActionKind::Fail)
        LogError(action.message);
    else if (!action.message.empty())
        Log(action.message);
    Execute(world, action);
    if (g_driver->step() != before && g_driver->step() != Flags::Step::Failed)
        Log(std::string("step ") + Flags::StepName(g_driver->step()));
}

void Shutdown()
{
    g_driver.reset();
    g_startAlone = false;
}

std::uint16_t TakeNetworkCode(NC_STACK_button *networkButtons, int netSelMode)
{
    g_startAlone = false;
    if (!g_driver)
        return 0;
    const Flags::Press press = g_driver->TakePress(ScreenOf(netSelMode));
    if (!press.code)
        return 0;
    g_startAlone = press.startAlone;
    if (press.widget == Flags::WidgetReady && networkButtons)
    {
        // The checkbox shows what a click would have left it showing.
        NC_STACK_button::button_66arg state;
        state.butID = Flags::WidgetReady;
        state.field_4 = 1;
        networkButtons->SetState(&state);
    }
    return press.code;
}

bool TakeStartAlone()
{
    const bool startAlone = g_startAlone;
    g_startAlone = false;
    return startAlone;
}

bool Active()
{
    return g_driver != nullptr;
}

const char *StepName()
{
    return g_driver ? Flags::StepName(g_driver->step()) : "off";
}

std::string Error()
{
    return g_driver ? g_driver->error() : std::string();
}

} // namespace NetAuto
