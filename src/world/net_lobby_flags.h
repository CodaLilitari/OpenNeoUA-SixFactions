// Unattended lobby flags (-net-name, -net-host, -net-join, -net-faction,
// -net-ready, -net-start-when): parsing, validation and the step logic that
// walks the multiplayer screens. SDL- and engine-free so it can be unit-tested
// (tests/test_net_lobby_flags.cpp). src/game/yw_netauto.cpp fills a LobbyView
// from the shell every frame and carries out the returned Action through the
// same code paths a player's clicks reach. A widget press stays queued in the
// Driver until the multiplayer screen takes it during its own input handling.
#ifndef WORLD_NET_LOBBY_FLAGS_H_INCLUDED
#define WORLD_NET_LOBBY_FLAGS_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "net_factions.h"
#include "../netlib/connfail.h"

namespace World { namespace NetLobbyFlags {

// ZNDNet keeps at most 20 characters of a callsign; the lobby name field
// refuses spaces, control characters and '*'.
constexpr std::size_t MaxCallsign = 20;

inline bool ValidCallsign(const std::string &name, std::string *why = nullptr)
{
    const auto fail = [&](const char *reason) { if (why) *why = reason; return false; };
    if (name.empty())
        return fail("it is empty");
    if (name.size() > MaxCallsign)
        return fail("it is longer than 20 characters");
    for (const char c : name)
    {
        if (c == ' ')
            return fail("it contains a space");
        if (c < 0x21 || c > 0x7e || c == '*')
            return fail("it contains a character the name field refuses");
    }
    return true;
}

// Owner id from an owner number (1-6) or a faction name; 0 when unknown.
inline int ParseFaction(const std::string &text)
{
    std::string key;
    for (const char c : text)
    {
        if (c == ' ' || c == '-' || c == '_')
            continue;
        key += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    static const struct { const char *name; int owner; } kNames[] = {
        {"resistance", 1}, {"sulgogars", 2}, {"sulgogar", 2}, {"mykonian", 3}, {"mykonians", 3},
        {"mykonier", 3}, {"taerkasten", 4}, {"blacksect", 5}, {"black", 5}, {"ghorkov", 6},
    };
    for (const auto &entry : kNames)
        if (key == entry.name)
            return entry.owner;
    if (key.size() == 1)
        for (const int owner : NetFactions::Owners)
            if (key[0] == static_cast<char>('0' + owner))
                return owner;
    return 0;
}

inline const char *FactionName(int owner)
{
    switch (owner)
    {
    case 1: return "Resistance";
    case 2: return "Sulgogars";
    case 3: return "Mykonian";
    case 4: return "Taerkasten";
    case 5: return "Black Sect";
    case 6: return "Ghorkov";
    default: return "none";
    }
}

// Lobby faction buttons: the result code a click reports and the widget id.
inline std::uint16_t FactionCode(int owner)
{
    switch (owner)
    {
    case 1: return 1204;
    case 6: return 1205;
    case 3: return 1206;
    case 4: return 1207;
    case 5: return 1260;
    case 2: return 1261;
    default: return 0;
    }
}

inline int FactionWidget(int owner)
{
    switch (owner)
    {
    case 1: return 1206;
    case 6: return 1207;
    case 3: return 1208;
    case 4: return 1209;
    case 5: return 1220;
    case 2: return 1230;
    default: return 0;
    }
}

// Other multiplayer widgets and the codes their clicks report.
constexpr int WidgetOk = 1201;              // Next / Join / Start
constexpr std::uint16_t CodeOk = 1200;
constexpr int WidgetReady = 1219;           // Ready checkbox
constexpr std::uint16_t CodeReady = 1208;

inline bool ParseLevelId(const std::string &text, int &level)
{
    if (text.empty() || text.size() > 3)
        return false;
    int value = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
    }
    if (value < 1 || value > 255)
        return false;
    level = value;
    return true;
}

// A host name or dotted IPv4 address; the port is fixed (61234).
inline bool ValidAddress(const std::string &text)
{
    if (text.empty() || text.size() > 253 || text[0] == '-' || text[0] == '.')
        return false;
    for (const char c : text)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return false;
    return true;
}

struct Options
{
    bool any = false;          // at least one -net-* argument
    std::string name;          // -net-name (empty: the profile's callsign)
    int hostLevel = 0;         // -net-host <level id>
    std::string joinAddress;   // -net-join <address>
    int faction = 0;           // -net-faction (owner id; 0: keep the lobby's choice)
    bool ready = false;        // -net-ready
    int startWhen = 0;         // -net-start-when <n>: host starts once n players are all ready
    std::vector<std::string> errors;

    bool Host() const { return hostLevel > 0; }
    bool Join() const { return !joinAddress.empty(); }
    bool Valid() const { return any && errors.empty(); }
};

// Reads every -net-* argument. Unknown, repeated, value-less or invalid flags
// are errors: a scripted run must not continue with a guess.
inline Options Parse(const std::vector<std::string> &args)
{
    Options o;
    std::vector<std::string> seen;
    bool hostGiven = false, joinGiven = false;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string &flag = args[i];
        if (flag.compare(0, 5, "-net-") != 0)
            continue;
        o.any = true;
        bool repeated = false;
        for (const std::string &s : seen)
            repeated = repeated || s == flag;
        if (repeated)
        {
            o.errors.push_back(flag + " is given more than once");
            continue;
        }
        seen.push_back(flag);
        if (flag == "-net-ready")
        {
            o.ready = true;
            continue;
        }
        const bool known = flag == "-net-name" || flag == "-net-host" || flag == "-net-join" ||
                           flag == "-net-faction" || flag == "-net-start-when";
        if (!known)
        {
            o.errors.push_back("unknown flag " + flag);
            continue;
        }
        if (i + 1 >= args.size() || (!args[i + 1].empty() && args[i + 1][0] == '-'))
        {
            o.errors.push_back(flag + " needs a value");
            continue;
        }
        const std::string value = args[++i];
        std::string why;
        if (flag == "-net-name")
        {
            if (ValidCallsign(value, &why))
                o.name = value;
            else
                o.errors.push_back("-net-name '" + value + "' is not a usable callsign: " + why);
        }
        else if (flag == "-net-host")
        {
            hostGiven = true;
            if (!ParseLevelId(value, o.hostLevel))
                o.errors.push_back("-net-host '" + value + "' is not a level id (1-255)");
        }
        else if (flag == "-net-join")
        {
            joinGiven = true;
            if (ValidAddress(value))
                o.joinAddress = value;
            else
                o.errors.push_back("-net-join '" + value + "' is not a host name or IPv4 address (the port is fixed)");
        }
        else if (flag == "-net-faction")
        {
            o.faction = ParseFaction(value);
            if (!o.faction)
                o.errors.push_back("-net-faction '" + value + "' is not an owner number (1-6) or a faction name");
        }
        else
        {
            int n = 0;
            if (value.size() == 1 && value[0] >= '1' && value[0] <= '0' + NetFactions::MaxPlayers)
                n = value[0] - '0';
            if (n)
                o.startWhen = n;
            else
                o.errors.push_back("-net-start-when '" + value + "' is not a player count (1-6)");
        }
    }
    if (!o.any)
        return o;
    if (hostGiven && joinGiven)
        o.errors.push_back("-net-host and -net-join exclude each other");
    else if (!hostGiven && !joinGiven)
        o.errors.push_back("the -net-* flags need -net-host <level> or -net-join <address>");
    if (o.startWhen && joinGiven)
        o.errors.push_back("-net-start-when is for the host (-net-host)");
    return o;
}

enum class Screen { Provider, SessionSelect, Name, Address, Map, Session, Other };

enum class Step : std::uint8_t
{
    Off, Failed, WaitMenu, Provider, Name, Address, Map, Faction, Ready, InSession, WaitStart, InGame
};

inline const char *StepName(Step step)
{
    switch (step)
    {
    case Step::Off:       return "off";
    case Step::Failed:    return "failed";
    case Step::WaitMenu:  return "wait_menu";
    case Step::Provider:  return "provider";
    case Step::Name:      return "name";
    case Step::Address:   return "address";
    case Step::Map:       return "map";
    case Step::Faction:   return "faction";
    case Step::Ready:     return "ready";
    case Step::InSession: return "in_session";
    case Step::WaitStart: return "wait_start";
    case Step::InGame:    return "in_game";
    default:              return "unknown";
    }
}

struct Player
{
    std::string name;
    std::uint32_t faction = 0; // faction bit
    bool ready = false;
    bool me = false;
};

struct Level
{
    int id = 0;
    std::uint32_t starts = 0;  // host stations (RoboCount)
};

// What the shell shows this frame.
struct LobbyView
{
    std::uint32_t nowMs = 0;
    bool shellOpen = false;         // the menu shell is up (not a level or replay)
    bool titleScreen = false;       // main menu
    bool netMenu = false;           // the multiplayer menu is the active screen
    Screen screen = Screen::Other;  // netSelMode
    bool started = false;           // the network match passed the start barrier
    bool isHost = false;
    int levelId = 0;
    std::uint32_t mapFactions = 0;  // faction bits of the chosen level
    std::uint32_t selectedFaction = 0;
    bool readySent = false;         // rdyStart
    bool welcomed = false;          // isWelcmd: every other player's WELCOME arrived
    bool okEnabled = false;         // widget 1201
    bool readyEnabled = false;      // widget 1219
    std::uint32_t enabledFactions = 0; // faction bits whose lobby buttons are enabled
    bool rosterCanStart = false;    // host: CheckNetLobbyRoster().CanStart()
    bool clientConnected = false;   // client transport connected
    int connectFailure = ZNDNet::CONNFAIL_NONE; // reason of the last failed Connect()
    std::string profileCallsign;    // the name field's prefill
    std::vector<Player> players;    // transport roster
    std::vector<Level> levels;      // network levels in map-list order
};

enum class ActionKind
{
    None,           // nothing to do (message, if any, is progress for the log)
    OpenMenu,       // the main menu's Multiplayer button
    ChooseProvider, // host or join on the first screen (host)
    SubmitText,     // put text in the field, then the OK widget (code)
    ChooseLevel,    // select levelId at listIndex in the map list, then OK (code)
    JoinSession,    // connected but not in the session yet: join it
    PressCode,      // a click on widget, reporting code
    StartAlone,     // Start (code) for a host with no other player; see Press::startAlone
    Fail,           // stop; message says why
};

struct Action
{
    ActionKind kind = ActionKind::None;
    bool host = false;
    std::string text;
    int levelId = 0;
    int listIndex = -1;
    std::uint16_t code = 0;         // non-zero: the driver queues this press (see Press)
    int widget = 0;
    std::string message;
};

// A widget press waiting for the multiplayer screen. The screen takes it in
// its own input handling, where a click would land (Driver::TakePress), so
// whatever the press starts runs inside the shell frame like a click. That
// matters for Start: the launch sets the shell's envAction, which the main
// loop reads right after the shell frame and the next frame clears first, so
// a launch requested between frames is lost.
struct Press
{
    std::uint16_t code = 0;         // the result code a click on the widget reports; 0: none
    int widget = 0;
    Screen screen = Screen::Other;  // the only screen that may take it
    bool startAlone = false;        // Start for a lone host: launch without the "start alone?"
                                    // confirmation a click opens (its OK would launch)
};

class Driver
{
public:
    // Enumerators, not static constexpr members: C++14 would need an
    // out-of-line definition the moment one is bound to a reference.
    enum : std::uint32_t
    {
        MenuTimeoutMs = 120000,
        JoinTimeoutMs = 120000,
        JoinRetryMs = 2000,
        WelcomeTimeoutMs = 60000,
        WidgetTimeoutMs = 15000,
        SettleMs = 1500,      // after a faction change, before READY
        StartHoldMs = 2000,   // the start condition must hold this long
        ProgressMs = 15000,
        StartAttempts = 5,    // Start taken this often in a row, condition held, no launch: fail
    };

    // botFaction: the -bot spec's owner, used when -net-faction is absent.
    explicit Driver(const Options &options, int botFaction = 0)
        : o_(options)
    {
        if (!o_.Valid())
        {
            step_ = o_.any ? Step::Failed : Step::Off;
            for (const std::string &e : o_.errors)
                error_ += (error_.empty() ? "" : "; ") + e;
            return;
        }
        want_ = o_.faction ? o_.faction : botFaction;
        explicit_ = o_.faction != 0;
        step_ = Step::WaitMenu;
    }

    Step step() const { return step_; }
    const std::string &error() const { return error_; }
    int wantedFaction() const { return want_; }
    const Press &queued() const { return press_; } // the press not taken yet (code 0: none)

    Action Next(const LobbyView &v)
    {
        if (step_ == Step::Off || step_ == Step::Failed || step_ == Step::InGame)
            return Action();
        if (!began_)
        {
            began_ = true;
            since_ = v.nowMs;
        }
        if (v.started)
        {
            press_ = Press();
            Enter(Step::InGame, v);
            return Note("the match started");
        }
        if (press_.code)
        {
            // Only the screen the press was made on takes it (TakePress). Once
            // that screen or the menu is gone, nothing ever will, and a press
            // left queued would land on whatever screen opens next.
            if (!v.netMenu || v.screen != press_.screen)
                return Fail("the multiplayer screen changed before it took the pressed widget");
            if (!pending_)
            {
                pending_ = true;
                pendingSince_ = v.nowMs;
            }
            if (v.nowMs - pendingSince_ > WidgetTimeoutMs)
                return Fail("the multiplayer screen did not take the pressed widget within 15 s");
            return Action();
        }
        pending_ = false;
        if (!v.shellOpen || Before(v.nowMs, notBefore_))
            return Action();
        Action a;
        switch (step_)
        {
        case Step::WaitMenu:  a = WaitMenu(v); break;
        case Step::Provider:  a = Provider(v); break;
        case Step::Name:      a = Name(v); break;
        case Step::Address:   a = Address(v); break;
        case Step::Map:       a = Map(v); break;
        case Step::Faction:   a = Faction(v); break;
        case Step::Ready:     a = Ready(v); break;
        case Step::WaitStart: a = WaitStart(v); break;
        case Step::InSession: a = InSession(v); break;
        default:              break;
        }
        if (a.code && step_ != Step::Failed)
        {
            press_.code = a.code;
            press_.widget = a.widget;
            press_.screen = v.screen;
            press_.startAlone = a.kind == ActionKind::StartAlone;
        }
        return a;
    }

    // For the multiplayer screen's input handling, once per shell frame in
    // which no widget was clicked: the queued press if screen is the one it
    // was made on, else nothing. Taking it clears it. A driver that failed or
    // finished has nothing queued.
    Press TakePress(Screen screen)
    {
        if (!press_.code || screen != press_.screen)
            return Press();
        const Press press = press_;
        press_ = Press();
        return press;
    }

private:
    static bool Before(std::uint32_t now, std::uint32_t t) { return static_cast<std::int32_t>(now - t) < 0; }
    std::uint32_t Elapsed(const LobbyView &v) const { return v.nowMs - since_; }

    void Enter(Step step, const LobbyView &v)
    {
        step_ = step;
        since_ = v.nowMs;
        sent_ = false;
        holdSince_ = 0;
        holding_ = false;
        lastProgress_ = v.nowMs;
    }

    Action Note(const std::string &message) const
    {
        Action a;
        a.message = message;
        return a;
    }

    Action Fail(const std::string &message)
    {
        step_ = Step::Failed;
        error_ = message;
        press_ = Press(); // the game is interactive again: nothing may press later
        Action a;
        a.kind = ActionKind::Fail;
        a.message = message;
        return a;
    }

    Action Do(ActionKind kind, const std::string &message)
    {
        sent_ = true;
        Action a;
        a.kind = kind;
        a.message = message;
        return a;
    }

    std::string Callsign(const LobbyView &v) const { return o_.name.empty() ? v.profileCallsign : o_.name; }

    std::string FactionList(std::uint32_t bits) const
    {
        std::string out;
        for (const int owner : NetFactions::Owners)
            if (bits & (1u << owner))
                out += (out.empty() ? "" : ", ") + std::to_string(owner) + " " + FactionName(owner);
        return out.empty() ? "none" : out;
    }

    Action LeftLobby(const LobbyView &v)
    {
        return Fail(std::string("the lobby was left before the match started (multiplayer menu ") +
                    (v.netMenu ? "open" : "closed") + ")");
    }

    Action WaitMenu(const LobbyView &v)
    {
        if (v.netMenu)
        {
            Enter(Step::Provider, v);
            return Action();
        }
        if (v.titleScreen)
        {
            Enter(Step::Provider, v); // the next step starts with nothing sent
            Action a;
            a.kind = ActionKind::OpenMenu;
            a.message = "opening the multiplayer menu";
            return a;
        }
        if (Elapsed(v) > MenuTimeoutMs)
            return Fail("the main menu did not appear within 120 s");
        return Action();
    }

    Action Provider(const LobbyView &v)
    {
        if (!v.netMenu)
            return Fail("the multiplayer menu did not open");
        if (sent_)
        {
            if (v.screen == Screen::Name)
            {
                Enter(Step::Name, v);
                return Action();
            }
            return Fail("the host/join choice was not accepted");
        }
        if (v.screen != Screen::Provider)
            return Fail("the multiplayer menu did not start at the host/join choice");
        Action a = Do(ActionKind::ChooseProvider, o_.Host() ? "choosing to host" : "choosing to join");
        a.host = o_.Host();
        return a;
    }

    Action Name(const LobbyView &v)
    {
        if (!v.netMenu)
            return LeftLobby(v);
        const std::string callsign = Callsign(v);
        if (sent_)
        {
            const Screen next = o_.Host() ? Screen::Map : Screen::Address;
            if (v.screen == next)
            {
                Enter(o_.Host() ? Step::Map : Step::Address, v);
                return Note("callsign " + callsign + " accepted");
            }
            return Fail("the callsign " + callsign + " was not accepted");
        }
        if (v.screen != Screen::Name)
            return Fail("expected the callsign screen");
        std::string why;
        if (!ValidCallsign(callsign, &why))
            return Fail("the callsign '" + callsign + "' cannot be used (" + why + "); pass -net-name");
        if (!v.okEnabled)
            return Elapsed(v) > WidgetTimeoutMs ? Fail("the Next button stayed disabled") : Action();
        Action a = Do(ActionKind::SubmitText, "entering the callsign " + callsign);
        a.text = callsign;
        a.code = CodeOk;
        a.widget = WidgetOk;
        return a;
    }

    Action Address(const LobbyView &v)
    {
        if (!v.netMenu)
            return LeftLobby(v);
        if (sent_)
        {
            if (v.screen == Screen::Session)
            {
                Enter(want_ ? Step::Faction : AfterFaction(), v);
                return Note("joined " + o_.joinAddress + " after " + std::to_string(attempts_) + " attempt(s)");
            }
            if (v.screen == Screen::SessionSelect)
                return Fail("the server at " + o_.joinAddress + " lists sessions; joining one is not supported");
            if (v.screen != Screen::Address)
                return Fail("joining left the address screen without entering the lobby");
            const std::string reason = v.clientConnected ? std::string("connected, not in the session")
                                                         : ZNDNet::ConnectFailureName(v.connectFailure);
            if (ZNDNet::ConnectFailureIsFinal(v.connectFailure))
                return Fail("cannot join " + o_.joinAddress + ": " + reason);
            if (Elapsed(v) > JoinTimeoutMs)
                return Fail("could not join " + o_.joinAddress + " within 120 s (" + std::to_string(attempts_) +
                            " attempts, last: " + reason + ")");
            sent_ = false;
            notBefore_ = v.nowMs + JoinRetryMs;
            return Note("join attempt " + std::to_string(attempts_) + " failed (" + reason + "), retrying");
        }
        if (v.screen != Screen::Address)
            return Fail("expected the address screen");
        if (v.clientConnected)
        {
            // Connected, but the session join did not finish within its
            // one-second wait: join the session instead of connecting again.
            Action a = Do(ActionKind::JoinSession, "connected; joining the session");
            return a;
        }
        if (!v.okEnabled)
            return Elapsed(v) > WidgetTimeoutMs ? Fail("the Join button stayed disabled") : Action();
        ++attempts_;
        Action a = Do(ActionKind::SubmitText, "connecting to " + o_.joinAddress + " (attempt " +
                      std::to_string(attempts_) + ")");
        a.text = o_.joinAddress;
        a.code = CodeOk;
        a.widget = WidgetOk;
        return a;
    }

    Action Map(const LobbyView &v)
    {
        if (!v.netMenu)
            return LeftLobby(v);
        if (sent_)
        {
            if (v.screen == Screen::Session && v.isHost)
            {
                Enter(want_ ? Step::Faction : AfterFaction(), v);
                return Note("hosting level " + std::to_string(o_.hostLevel));
            }
            return Fail("hosting level " + std::to_string(o_.hostLevel) + " failed");
        }
        if (v.screen != Screen::Map)
            return Fail("expected the level list");
        // The map list hides levels with fewer starts than players present.
        int index = 0, found = -1;
        std::uint32_t starts = 0;
        std::string available;
        for (const Level &level : v.levels)
        {
            if (v.players.size() > 1 && v.players.size() > level.starts)
                continue;
            available += (available.empty() ? "" : " ") + std::to_string(level.id);
            if (level.id == o_.hostLevel)
            {
                found = index;
                starts = level.starts;
            }
            ++index;
        }
        if (found < 0)
            return Fail("level " + std::to_string(o_.hostLevel) + " is not in the network level list (" +
                        (available.empty() ? std::string("the list is empty") : "available: " + available) + ")");
        if (o_.startWhen > 0 && static_cast<std::uint32_t>(o_.startWhen) > starts)
            return Fail("level " + std::to_string(o_.hostLevel) + " has " + std::to_string(starts) +
                        " starts; -net-start-when " + std::to_string(o_.startWhen) + " cannot be met");
        if (!v.okEnabled)
            return Elapsed(v) > WidgetTimeoutMs ? Fail("the Next button stayed disabled") : Action();
        Action a = Do(ActionKind::ChooseLevel, "choosing level " + std::to_string(o_.hostLevel));
        a.levelId = o_.hostLevel;
        a.listIndex = found;
        a.code = CodeOk;
        a.widget = WidgetOk;
        return a;
    }

    Step AfterFaction() const
    {
        if (o_.Join() && o_.ready)
            return Step::Ready;
        if (o_.Host() && o_.startWhen > 0)
            return Step::WaitStart;
        return Step::InSession;
    }

    Action Faction(const LobbyView &v)
    {
        if (!v.netMenu || v.screen != Screen::Session)
            return LeftLobby(v);
        if (!want_)
        {
            Enter(AfterFaction(), v);
            return Action();
        }
        const std::uint32_t bit = 1u << want_;
        const std::string name = FactionName(want_);
        if (sent_)
        {
            if (v.selectedFaction == bit)
            {
                Enter(AfterFaction(), v);
                notBefore_ = v.nowMs + SettleMs;
                return Note("playing " + name);
            }
            return Fail("the " + name + " selection did not take effect");
        }
        if (!v.isHost && !v.welcomed)
            return Elapsed(v) > WelcomeTimeoutMs
                ? Fail("no WELCOME from every player within 60 s; the faction cannot be chosen safely")
                : Action();
        if (v.selectedFaction == bit)
        {
            Enter(AfterFaction(), v);
            return Note("playing " + name);
        }
        std::string problem;
        if (!(v.mapFactions & bit))
            problem = name + " is not on level " + std::to_string(v.levelId) + " (available: " +
                      FactionList(v.mapFactions) + ")";
        for (const Player &p : v.players)
            if (problem.empty() && !p.me && p.faction == bit)
                problem = name + " is taken by " + p.name;
        if (!problem.empty())
        {
            if (explicit_)
                return Fail("-net-faction: " + problem);
            Enter(AfterFaction(), v);
            return Note("the -bot faction is not available here (" + problem + "); keeping " +
                        FactionList(v.selectedFaction));
        }
        if (!(v.enabledFactions & bit))
            return Elapsed(v) > WidgetTimeoutMs ? Fail("the " + name + " button stayed disabled") : Action();
        Action a = Do(ActionKind::PressCode, "choosing " + name);
        a.code = FactionCode(want_);
        a.widget = FactionWidget(want_);
        return a;
    }

    Action Ready(const LobbyView &v)
    {
        if (!v.netMenu || v.screen != Screen::Session)
            return LeftLobby(v);
        if (v.readySent)
        {
            Enter(Step::WaitStart, v);
            return Note("READY sent");
        }
        if (sent_)
            return Fail("READY did not take effect");
        if (explicit_ && v.selectedFaction != (1u << want_))
            return Fail(std::string("a later WELCOME moved this player from ") + FactionName(want_) +
                        " to " + FactionList(v.selectedFaction) + " before READY");
        if (!v.readyEnabled)
            return Elapsed(v) > WelcomeTimeoutMs
                ? Fail("the Ready box stayed disabled for 60 s (no WELCOME from every player)")
                : Action();
        Action a = Do(ActionKind::PressCode, "sending READY");
        a.code = CodeReady;
        a.widget = WidgetReady;
        return a;
    }

    Action InSession(const LobbyView &v)
    {
        if (!v.netMenu || v.screen != Screen::Session)
            return LeftLobby(v);
        return Action();
    }

    Action WaitStart(const LobbyView &v)
    {
        if (!v.netMenu || v.screen != Screen::Session)
            return LeftLobby(v);
        if (!o_.Host())
            return Action(); // client: the host starts the match
        if (sent_)
        {
            // The screen took Start, but the lobby is still here: the launch
            // re-check refused it (roster or readiness changed in between), or
            // the press started nothing. Waiting again covers the first; a
            // start that fails while the condition keeps holding is the second.
            sent_ = false;
            holding_ = false;
            if (++failedStarts_ >= StartAttempts)
                return Fail("Start was taken " + std::to_string(failedStarts_) +
                            " times in a row while the start condition held, and the match did not start");
            return Note("the start did not take effect; waiting again");
        }
        const std::size_t count = v.players.size();
        std::size_t ready = 0;
        for (const Player &p : v.players)
            ready += p.ready ? 1 : 0;
        const bool met = count >= static_cast<std::size_t>(o_.startWhen) && ready == count &&
                         v.rosterCanStart && v.okEnabled;
        if (!met)
        {
            holding_ = false;
            failedStarts_ = 0;
            if (v.nowMs - lastProgress_ >= ProgressMs)
            {
                lastProgress_ = v.nowMs;
                return Note("waiting for " + std::to_string(o_.startWhen) + " ready players: " +
                            std::to_string(count) + " in the lobby, " + std::to_string(ready) + " ready" +
                            (v.rosterCanStart || ready != count ? "" : ", roster check failed"));
            }
            return Action();
        }
        if (!holding_)
        {
            holding_ = true;
            holdSince_ = v.nowMs;
        }
        if (v.nowMs - holdSince_ < StartHoldMs)
            return Action();
        // Alone, a click on Start only opens the "start alone?" confirmation;
        // the press says to launch instead, as that dialog's OK would.
        Action a = count <= 1 ? Do(ActionKind::StartAlone, "starting the match alone")
                              : Do(ActionKind::PressCode, "starting the match with " + std::to_string(count) +
                                                          " players");
        a.code = CodeOk;
        a.widget = WidgetOk;
        return a;
    }

    Options o_;
    Step step_ = Step::Off;
    std::string error_;
    Press press_;
    int want_ = 0;
    bool explicit_ = false;
    bool began_ = false;
    bool sent_ = false;
    bool holding_ = false;
    bool pending_ = false;
    std::uint32_t pendingSince_ = 0;
    int attempts_ = 0;
    int failedStarts_ = 0; // Start presses in a row that did not start the match
    std::uint32_t since_ = 0;
    std::uint32_t notBefore_ = 0;
    std::uint32_t holdSince_ = 0;
    std::uint32_t lastProgress_ = 0;
};

} }
#endif
