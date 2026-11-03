// Unattended lobby flags (src/world/net_lobby_flags.h): parsing, validation
// and the screen-by-screen driver against a simulated multiplayer shell.
// Explicit checks (no assert), so the test also fails in NDEBUG builds.
#include "../src/world/net_lobby_flags.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace World::NetLobbyFlags;

static void Require(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static bool Contains(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

static Options P(std::vector<std::string> args)
{
    args.insert(args.begin(), "OpenNeoUA");
    return Parse(args);
}

static bool HasError(const Options &o, const std::string &part)
{
    for (const std::string &e : o.errors)
        if (Contains(e, part))
            return true;
    return false;
}

static void TestValidation()
{
    std::string why;
    Require(ValidCallsign("HOST1"), "plain callsign refused");
    Require(ValidCallsign(std::string(20, 'a')), "20-character callsign refused");
    Require(!ValidCallsign(std::string(21, 'a'), &why) && Contains(why, "20"), "21-character callsign accepted");
    Require(!ValidCallsign("BOT Ghorkov", &why) && Contains(why, "space"), "callsign with a space accepted");
    Require(!ValidCallsign("", &why) && Contains(why, "empty"), "empty callsign accepted");
    Require(!ValidCallsign("a*b"), "callsign with '*' accepted");
    Require(!ValidCallsign("tab\tname"), "callsign with a control character accepted");
    Require(!ValidCallsign("caf\xc3\xa9"), "non-ASCII callsign accepted");

    Require(ParseFaction("Ghorkov") == 6 && ParseFaction("ghorkov") == 6, "Ghorkov not parsed");
    Require(ParseFaction("black-sect") == 5 && ParseFaction("Black Sect") == 5 && ParseFaction("BLACK_SECT") == 5,
            "Black Sect spellings not parsed");
    Require(ParseFaction("Mykonier") == 3 && ParseFaction("mykonian") == 3, "Mykonian spellings not parsed");
    Require(ParseFaction("sulgogars") == 2 && ParseFaction("taerkasten") == 4 && ParseFaction("Resistance") == 1,
            "faction names not parsed");
    for (int owner = 1; owner <= 6; ++owner)
        Require(ParseFaction(std::string(1, static_cast<char>('0' + owner))) == owner, "owner number not parsed");
    for (const char *bad : {"0", "7", "8", "12", "", "elves", "ghor"})
        Require(ParseFaction(bad) == 0, std::string("invalid faction accepted: ") + bad);
    for (int owner = 1; owner <= 6; ++owner)
    {
        Require(FactionCode(owner) != 0 && FactionWidget(owner) != 0, "faction without a lobby button");
        for (int other = owner + 1; other <= 6; ++other)
            Require(FactionCode(owner) != FactionCode(other), "two factions share a code");
    }
    // The buttons as yw_shell_ui_screens.cpp builds them.
    Require(FactionCode(1) == 1204 && FactionWidget(1) == 1206, "Resistance button changed");
    Require(FactionCode(6) == 1205 && FactionWidget(6) == 1207, "Ghorkov button changed");
    Require(FactionCode(5) == 1260 && FactionWidget(5) == 1220, "Black Sect button changed");
    Require(FactionCode(2) == 1261 && FactionWidget(2) == 1230, "Sulgogar button changed");

    int level = 0;
    Require(ParseLevelId("200", level) && level == 200, "level 200 refused");
    Require(ParseLevelId("1", level) && level == 1 && ParseLevelId("255", level) && level == 255, "level bounds refused");
    for (const char *bad : {"0", "256", "-1", "+5", "abc", "", "1000", "2 0"})
        Require(!ParseLevelId(bad, level), std::string("invalid level accepted: ") + bad);

    Require(ValidAddress("127.0.0.1") && ValidAddress("host-a.lan") && ValidAddress("10.0.0.2"), "address refused");
    for (const char *bad : {"", "127.0.0.1:61234", "-x", ".x", "a b", "host/1"})
        Require(!ValidAddress(bad), std::string("invalid address accepted: ") + bad);
}

static void TestParse()
{
    Options none = P({"-env", "x", "-bot", "Ghorkov:Veteran"});
    Require(!none.any && none.errors.empty() && !none.Valid(), "no -net flag still enabled the automation");

    Options host = P({"-net-host", "200", "-net-name", "HOST1", "-net-faction", "resistance", "-net-start-when", "3"});
    Require(host.Valid() && host.Host() && !host.Join(), "valid host flags refused");
    Require(host.hostLevel == 200 && host.name == "HOST1" && host.faction == 1 && host.startWhen == 3,
            "host flags parsed wrongly");

    Options client = P({"-bot", "Ghorkov:Veteran", "-net-join", "127.0.0.1", "-net-name", "BOT1", "-net-ready"});
    Require(client.Valid() && client.Join() && client.ready && client.joinAddress == "127.0.0.1" &&
            client.faction == 0, "valid client flags refused");

    Require(HasError(P({"-net-join", "127.0.0.1", "-net-strat-when", "2"}), "unknown flag -net-strat-when"),
            "misspelt flag accepted");
    Require(HasError(P({"-net-join", "127.0.0.1", "-net-rejoin"}), "unknown flag -net-rejoin"),
            "-net-rejoin accepted before the rejoin work exists");
    Require(HasError(P({"-net-host", "200", "-net-name", "A", "-net-name", "B"}), "more than once"),
            "repeated flag accepted");
    Require(HasError(P({"-net-host"}), "needs a value"), "missing value accepted");
    Require(HasError(P({"-net-name", "-net-host", "200"}), "-net-name needs a value"), "flag taken as a value");
    Require(HasError(P({"-net-host", "200", "-net-name", "BOT Ghorkov"}), "space"), "callsign with a space accepted");
    Require(HasError(P({"-net-host", "200", "-net-name", std::string(21, 'x')}), "20"), "long callsign accepted");
    Require(HasError(P({"-net-host", "abc"}), "level id"), "bad level accepted");
    Require(HasError(P({"-net-join", "127.0.0.1:61234"}), "port is fixed"), "address with a port accepted");
    Require(HasError(P({"-net-join", "127.0.0.1", "-net-faction", "elves"}), "-net-faction 'elves'"),
            "bad faction accepted");
    Require(HasError(P({"-net-host", "200", "-net-start-when", "7"}), "player count"), "7 players accepted");
    Require(HasError(P({"-net-host", "200", "-net-start-when", "0"}), "player count"), "0 players accepted");
    Require(HasError(P({"-net-host", "200", "-net-join", "127.0.0.1"}), "exclude each other"), "host and join accepted");
    Require(HasError(P({"-net-name", "A", "-net-ready"}), "need -net-host"), "flags without host/join accepted");
    Require(HasError(P({"-net-join", "127.0.0.1", "-net-start-when", "2"}), "for the host"),
            "client start condition accepted");
    Options many = P({"-net-host", "x", "-net-faction", "9", "-net-bogus"});
    Require(many.errors.size() == 3, "not every error was reported");

    Driver off(none);
    Require(off.step() == Step::Off, "driver without flags is not off");
    Driver failed(many);
    Require(failed.step() == Step::Failed && Contains(failed.error(), "-net-bogus") &&
            Contains(failed.error(), "-net-faction"), "invalid flags did not fail with every reason");
    LobbyView title;
    title.shellOpen = title.titleScreen = true;
    Require(failed.Next(title).kind == ActionKind::None && failed.step() == Step::Failed, "failed driver acted");
}

// A small model of the multiplayer shell. As in the engine, the screen takes
// the driver's queued press in its own frame, where a click would land, and
// handles it then; direct calls (menu, host/join choice) take effect at once.
struct Shell
{
    LobbyView v;
    bool hostMode = false;
    std::string text;
    int level = 0;
    int listIndex = -1;
    std::vector<int> failures;      // connect failures before a join succeeds
    std::vector<Player> remote;     // other players present when joining
    std::uint32_t levelFactions = 0;
    std::vector<std::string> trace; // actions taken
    std::vector<std::string> notes; // messages
    int launchFrames = -1;          // frames until the start barrier passes
    bool deferNextLaunch = false;
    bool launchLost = false;        // Start is handled but no launch follows (as when the engine
                                    // asked for it between frames, where the next frame clears it)
    bool confirmDialog = false;     // the "start alone?" confirmation is open
    bool stuck = false;             // the screen never takes a pressed widget

    Shell()
    {
        v.shellOpen = true;
        v.titleScreen = true;
        v.profileCallsign = "SDU7";
        v.levels = {{132, 2}, {200, 6}, {201, 6}, {150, 4}};
        levelFactions = (1u << 1) | (1u << 6) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 2);
    }

    void Widgets()
    {
        v.okEnabled = v.screen != Screen::Session || (v.isHost && v.rosterCanStart);
        v.readyEnabled = v.screen == Screen::Session && !v.isHost && v.welcomed;
        v.enabledFactions = (v.screen == Screen::Session && (v.isHost || !v.readySent)) ? v.mapFactions : 0;
    }

    void Roster()
    {
        std::uint32_t taken = 0;
        bool clash = false;
        for (const Player &p : v.players)
        {
            clash = clash || (taken & p.faction) != 0 || !(v.mapFactions & p.faction);
            taken |= p.faction;
        }
        bool all = true;
        for (const Player &p : v.players)
            all = all && p.ready;
        v.rosterCanStart = v.isHost && !clash && all;
    }

    Player &Me()
    {
        for (Player &p : v.players)
            if (p.me)
                return p;
        static Player none;
        return none;
    }

    void Frame(Driver &d)
    {
        if (launchFrames > 0 && --launchFrames == 0)
        {
            v.started = true;
            v.shellOpen = false;
        }
        // The multiplayer screen's input handling: nobody clicks here, so it
        // takes the press the flags queued, if one was made on this screen.
        const Press press = v.shellOpen && v.netMenu && !stuck ? d.TakePress(v.screen) : Press();
        if (press.code)
        {
            const std::uint16_t c = press.code;
            if (v.screen == Screen::Name && c == CodeOk)
            {
                v.profileCallsign = text;
                v.screen = hostMode ? Screen::Map : Screen::Address;
            }
            else if (v.screen == Screen::Address && c == CodeOk)
            {
                if (!failures.empty())
                {
                    v.connectFailure = failures.front();
                    failures.erase(failures.begin());
                }
                else
                {
                    v.connectFailure = ZNDNet::CONNFAIL_NONE;
                    v.screen = Screen::Session;
                    v.levelId = 200;
                    v.mapFactions = levelFactions;
                    v.players = remote;
                    Player me;
                    me.name = v.profileCallsign;
                    me.me = true;
                    std::uint32_t taken = 0;
                    for (const Player &p : remote)
                        taken |= p.faction;
                    me.faction = World::NetFactions::FirstAvailable(levelFactions & ~taken);
                    v.selectedFaction = me.faction;
                    v.players.push_back(me);
                }
            }
            else if (v.screen == Screen::Map && c == CodeOk)
            {
                v.screen = Screen::Session;
                v.isHost = true;
                v.levelId = level;
                v.mapFactions = levelFactions;
                v.selectedFaction = World::NetFactions::FirstAvailable(levelFactions);
                v.readySent = true;
                Player me;
                me.name = v.profileCallsign;
                me.me = true;
                me.ready = true;
                me.faction = v.selectedFaction;
                v.players = {me};
            }
            else if (v.screen == Screen::Session)
            {
                for (int owner = 1; owner <= 6; ++owner)
                    if (c == FactionCode(owner))
                    {
                        v.selectedFaction = 1u << owner;
                        Me().faction = v.selectedFaction;
                    }
                if (c == CodeReady)
                {
                    v.readySent = true;
                    Me().ready = true;
                }
                if (c == CodeOk && v.isHost)
                {
                    // Alone, a click on Start only opens the confirmation.
                    if (v.players.size() <= 1 && !press.startAlone)
                    {
                        confirmDialog = true;
                        trace.push_back("confirm-dialog");
                    }
                    else
                        Launch();
                }
            }
        }
        Roster();
        Widgets();
    }

    // Only a press handled inside Frame() launches, as in the engine.
    void Launch()
    {
        if (deferNextLaunch || !v.rosterCanStart)
        {
            deferNextLaunch = false;
            return;
        }
        if (launchLost)
            return;
        v.shellOpen = false; // the level loads, then the start barrier holds the frame
        launchFrames = 3;
    }

    void Apply(const Action &a)
    {
        if (!a.message.empty())
            notes.push_back(a.message);
        switch (a.kind)
        {
        case ActionKind::None: return;
        case ActionKind::OpenMenu:
            v.titleScreen = false;
            v.netMenu = true;
            v.screen = Screen::Provider;
            trace.push_back("menu");
            break;
        case ActionKind::ChooseProvider:
            hostMode = a.host;
            v.screen = Screen::Name;
            trace.push_back(a.host ? "provider:host" : "provider:join");
            break;
        case ActionKind::SubmitText:
            text = a.text;            // the field's text; the OK press is queued in the driver
            trace.push_back("text:" + a.text);
            break;
        case ActionKind::ChooseLevel:
            level = a.levelId;
            listIndex = a.listIndex;
            trace.push_back("level:" + std::to_string(a.levelId) + "@" + std::to_string(a.listIndex));
            break;
        case ActionKind::JoinSession:
            trace.push_back("join-session");
            break;
        case ActionKind::PressCode:
            trace.push_back("code:" + std::to_string(a.code));
            break;
        case ActionKind::StartAlone:
            trace.push_back("start-alone"); // queued in the driver like any press
            break;
        case ActionKind::Fail:
            trace.push_back("fail");
            break;
        }
    }

    // Runs frames of frameMs until the driver stops at a terminal step or a
    // predicate holds; returns the frames used.
    template <typename Until>
    int Run(Driver &d, int frames, std::uint32_t frameMs, Until until)
    {
        for (int i = 0; i < frames; ++i)
        {
            Frame(d);
            v.nowMs += frameMs;
            Apply(d.Next(v));
            if (until() || d.step() == Step::Failed || d.step() == Step::InGame)
                return i + 1;
        }
        return frames;
    }
    int Run(Driver &d, int frames, std::uint32_t frameMs = 50)
    {
        return Run(d, frames, frameMs, [] { return false; });
    }

    std::string Trace() const
    {
        std::string out;
        for (const std::string &t : trace)
            out += (out.empty() ? "" : " ") + t;
        return out;
    }
};

static Player Remote(const char *name, int owner, bool ready)
{
    Player p;
    p.name = name;
    p.faction = owner ? 1u << owner : 0;
    p.ready = ready;
    return p;
}

static void TestHostFlow()
{
    Driver d(P({"-net-host", "200", "-net-name", "HOST1", "-net-faction", "ghorkov", "-net-start-when", "3"}));
    Shell s;
    s.levelFactions = (1u << 1) | (1u << 6) | (1u << 3);
    s.Run(d, 200, 50, [&] { return d.step() == Step::WaitStart; });
    Require(d.step() == Step::WaitStart, "host did not reach the start wait: " + s.Trace() + " / " + d.error());
    // Level 132 (2 starts) is still listed with one player, so 200 is entry 1.
    Require(s.Trace() == "menu provider:host text:HOST1 level:200@1 code:1205", "host actions: " + s.Trace());
    Require(s.v.selectedFaction == (1u << 6), "host did not end up on Ghorkov");

    // Two clients join; one is ready, the other not: no start.
    s.v.players.push_back(Remote("C1", 1, true));
    s.v.players.push_back(Remote("BOT1", 3, false));
    s.Run(d, 200);
    Require(d.step() == Step::WaitStart && s.trace.back() == "code:1205", "started before every player was ready");
    // Ready flips on and off within the hold time: still no start.
    s.v.players.back().ready = true;
    s.Run(d, 20);
    s.v.players.back().ready = false;
    s.Run(d, 100);
    Require(s.trace.back() == "code:1205", "the start did not wait for the condition to hold");
    // A faction clash keeps the roster check failing.
    s.v.players.back().ready = true;
    s.v.players.back().faction = 1u << 1;
    s.Run(d, 400); // longer than one progress period
    Require(s.trace.back() == "code:1205", "started with a faction clash");
    bool reported = false;
    for (const std::string &n : s.notes)
        reported = reported || Contains(n, "roster check failed");
    Require(reported, "the failing roster check was never reported");
    s.v.players.back().faction = 1u << 3;
    // The launch re-check can still refuse: the driver waits again.
    s.deferNextLaunch = true;
    s.Run(d, 100, 50, [&] { return s.trace.back() == "code:1200"; });
    Require(s.trace.back() == "code:1200", "host never pressed Start");
    Require(d.queued().code == CodeOk && d.queued().widget == WidgetOk && !d.queued().startAlone,
            "Start with three players is not a plain press of the Start button");
    s.Run(d, 5);
    reported = false;
    for (const std::string &n : s.notes)
        reported = reported || Contains(n, "did not take effect; waiting again");
    Require(reported && d.step() == Step::WaitStart, "a deferred launch was not noticed");
    s.Run(d, 200);
    Require(d.step() == Step::InGame, "match did not start: " + s.Trace());
    int starts = 0;
    for (const std::string &t : s.trace)
        starts += t == "code:1200" ? 1 : 0;
    Require(starts == 2, "Start pressed " + std::to_string(starts) + " times instead of twice");
}

static void TestClientFlow()
{
    Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1", "-net-faction", "4", "-net-ready"}));
    Shell s;
    s.failures = {ZNDNet::CONNFAIL_NO_ANSWER, ZNDNet::CONNFAIL_NAME};
    s.remote = {Remote("HOST1", 1, true)};
    s.Run(d, 400, 50, [&] { return d.step() == Step::Faction; });
    Require(d.step() == Step::Faction, "client did not join: " + s.Trace() + " / " + d.error());
    Require(s.Trace() == "menu provider:join text:C1 text:127.0.0.1 text:127.0.0.1 text:127.0.0.1",
            "client actions: " + s.Trace());
    // No faction change before every WELCOME arrived.
    s.Run(d, 100);
    Require(s.trace.back() == "text:127.0.0.1" && d.step() == Step::Faction, "faction chosen before the WELCOMEs");
    s.v.welcomed = true;
    s.Run(d, 100, 50, [&] { return d.step() == Step::Ready; });
    Require(s.trace.back() == "code:1207" && s.v.selectedFaction == (1u << 4), "Taerkasten not chosen: " + s.Trace());
    // READY waits for the settle time after the faction change.
    const std::uint32_t chosen = s.v.nowMs;
    s.Run(d, 200, 50, [&] { return s.trace.back() == "code:1208"; });
    Require(s.trace.back() == "code:1208", "READY never sent: " + s.Trace());
    Require(s.v.nowMs - chosen >= Driver::SettleMs, "READY sent right after the faction change");
    s.Run(d, 5);
    Require(d.step() == Step::WaitStart && s.v.readySent, "client not waiting for the start");
    s.v.shellOpen = false;
    s.Run(d, 5);
    s.v.started = true;
    s.Run(d, 2);
    Require(d.step() == Step::InGame, "client did not notice the start");
    Require(s.Trace() == "menu provider:join text:C1 text:127.0.0.1 text:127.0.0.1 text:127.0.0.1 code:1207 code:1208",
            "client actions: " + s.Trace());
}

static void TestJoinFailures()
{
    {
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1"}));
        Shell s;
        s.failures = {ZNDNet::CONNFAIL_VERSION};
        s.Run(d, 200);
        Require(d.step() == Step::Failed && Contains(d.error(), "version"), "version refusal retried: " + d.error());
    }
    {
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1"}));
        Shell s;
        s.failures.assign(1000, ZNDNet::CONNFAIL_NO_ANSWER);
        s.Run(d, 4000, 100);
        Require(d.step() == Step::Failed && Contains(d.error(), "within 120 s") && Contains(d.error(), "no_answer"),
                "silent host not given up after the deadline: " + d.error());
        int attempts = 0;
        for (const std::string &t : s.trace)
            attempts += t == "text:127.0.0.1" ? 1 : 0;
        Require(attempts >= 40 && attempts <= 61, "retry pacing off: " + std::to_string(attempts) + " attempts");
    }
    {
        // Connected but the session join did not finish: join, do not reconnect.
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1"}));
        Shell s;
        s.Run(d, 100, 50, [&] { return d.step() == Step::Address; });
        s.v.clientConnected = true;
        s.Run(d, 3);
        Require(s.trace.back() == "join-session", "a connected client was connected again: " + s.Trace());
    }
}

static void TestFactionProblems()
{
    {
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1", "-net-faction", "black-sect", "-net-ready"}));
        Shell s;
        s.levelFactions = (1u << 1) | (1u << 6);
        s.remote = {Remote("HOST1", 1, true)};
        s.v.welcomed = true;
        s.Run(d, 300);
        Require(d.step() == Step::Failed && Contains(d.error(), "Black Sect is not on level 200") &&
                Contains(d.error(), "1 Resistance, 6 Ghorkov"), "unavailable faction accepted: " + d.error());
        Require(s.trace.back() == "fail", "a faction or READY was sent after the failure");
    }
    {
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1", "-net-faction", "resistance"}));
        Shell s;
        s.remote = {Remote("HOST1", 1, true)};
        s.v.welcomed = true;
        s.Run(d, 300);
        Require(d.step() == Step::Failed && Contains(d.error(), "taken by HOST1"), "taken faction accepted: " + d.error());
    }
    {
        // The -bot faction is a preference: unavailable, the lobby's choice stays.
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "BOT1", "-net-ready"}), 5);
        Require(d.wantedFaction() == 5, "-bot faction not used as the default");
        Shell s;
        s.levelFactions = (1u << 1) | (1u << 6);
        s.remote = {Remote("HOST1", 1, true)};
        s.v.welcomed = true;
        s.Run(d, 300, 50, [&] { return d.step() == Step::WaitStart; });
        Require(d.step() == Step::WaitStart && s.v.selectedFaction == (1u << 6) && s.v.readySent,
                "bot without its faction did not keep the lobby's choice: " + s.Trace() + " / " + d.error());
    }
    {
        // A later WELCOME clash moved the player before READY: fail, do not ready.
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1", "-net-faction", "4", "-net-ready"}));
        Shell s;
        s.remote = {Remote("HOST1", 1, true)};
        s.v.welcomed = true;
        s.Run(d, 300, 50, [&] { return d.step() == Step::Ready; });
        s.v.selectedFaction = 1u << 3;
        s.Run(d, 100);
        Require(d.step() == Step::Failed && Contains(d.error(), "moved this player from Taerkasten"),
                "faction clash before READY not caught: " + d.error());
        Require(!s.v.readySent, "READY sent after the clash");
    }
    {
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1", "-net-faction", "4"}));
        Shell s;
        s.remote = {Remote("HOST1", 1, true)};
        s.Run(d, 2000, 50);
        Require(d.step() == Step::Failed && Contains(d.error(), "WELCOME"), "missing WELCOMEs not reported");
    }
}

static void TestHostProblems()
{
    {
        Driver d(P({"-net-host", "77", "-net-name", "HOST1"}));
        Shell s;
        s.Run(d, 200);
        Require(d.step() == Step::Failed && Contains(d.error(), "level 77") && Contains(d.error(), "132 200 201 150"),
                "unknown level not reported with the list: " + d.error());
    }
    {
        Driver d(P({"-net-host", "150", "-net-name", "HOST1", "-net-start-when", "5"}));
        Shell s;
        s.Run(d, 200);
        Require(d.step() == Step::Failed && Contains(d.error(), "has 4 starts"), "too many players accepted: " + d.error());
    }
    {
        // The profile callsign is the fallback and must be usable.
        Driver d(P({"-net-host", "200"}));
        Shell s;
        s.v.profileCallsign = "My Name";
        s.Run(d, 200);
        Require(d.step() == Step::Failed && Contains(d.error(), "pass -net-name"), "unusable profile callsign used");
    }
    {
        // Alone, Start is pressed like any widget and launches in the shell's
        // own frame; a plain press would only open the confirmation.
        Driver d(P({"-net-host", "200", "-net-name", "SOLO", "-net-start-when", "1"}));
        Shell s;
        s.Run(d, 300, 50, [&] { return d.queued().code != 0 && d.step() == Step::WaitStart; });
        Require(d.queued().code == CodeOk && d.queued().widget == WidgetOk && d.queued().startAlone &&
                d.queued().screen == Screen::Session, "a lone host's Start is not a marked press: " + s.Trace());
        Require(!s.v.started && s.v.shellOpen, "the match started before the screen took Start");
        s.Run(d, 300);
        Require(d.step() == Step::InGame && s.trace.back() == "start-alone" && !s.confirmDialog,
                "single-player start: " + s.Trace() + " / " + d.error());
    }
    {
        // A Start that is taken but never launches (as when the launch is
        // asked for between frames) must not be retried until a harness
        // timeout: the driver gives up after StartAttempts presses.
        Driver d(P({"-net-host", "200", "-net-name", "SOLO", "-net-start-when", "1"}));
        Shell s;
        s.launchLost = true;
        s.Run(d, 2000);
        int starts = 0;
        for (const std::string &t : s.trace)
            starts += t == "start-alone" ? 1 : 0;
        Require(d.step() == Step::Failed && Contains(d.error(), "the match did not start") &&
                starts == Driver::StartAttempts, "lost launches retried " + std::to_string(starts) +
                " times: " + d.error());
        Require(d.queued().code == 0, "a press is left after giving up on the start");
    }
    {
        // Deferred launches while the lobby keeps changing do not count up:
        // only a start that fails with the condition holding does.
        Driver d(P({"-net-host", "200", "-net-name", "HOST1", "-net-start-when", "2"}));
        Shell s;
        s.Run(d, 100, 50, [&] { return d.step() == Step::WaitStart; });
        s.v.players.push_back(Remote("C1", 3, true));
        for (int round = 0; round < Driver::StartAttempts + 2; ++round)
        {
            s.deferNextLaunch = true;
            s.Run(d, 200, 50, [&] { return d.queued().code != 0; });
            s.Run(d, 3);
            s.v.players.back().ready = false; // the client unticks Ready, then ticks it again
            s.Run(d, 3);
            s.v.players.back().ready = true;
        }
        Require(d.step() == Step::WaitStart, "deferred launches in a changing lobby made the host give up: " +
                d.error());
        s.Run(d, 200);
        Require(d.step() == Step::InGame, "the match did not start after the lobby settled: " + s.Trace());
    }
    {
        Driver d(P({"-net-host", "200", "-net-name", "HOST1", "-net-start-when", "2"}));
        Shell s;
        s.Run(d, 100, 50, [&] { return d.step() == Step::WaitStart; });
        // Escape in the lobby: netcleanup returns to the provider screen.
        s.v.screen = Screen::Provider;
        s.v.netMenu = false;
        s.v.titleScreen = true;
        s.Run(d, 5);
        Require(d.step() == Step::Failed && Contains(d.error(), "lobby was left"), "leaving the lobby not reported");
    }
    {
        Driver d(P({"-net-host", "200", "-net-name", "HOST1"}));
        Shell s;
        s.stuck = true;
        s.Run(d, 1000, 50);
        Require(d.step() == Step::Failed && Contains(d.error(), "did not take the pressed widget") &&
                s.trace.back() == "fail", "a widget press that never lands was waited for forever: " + d.error());
        // The game is interactive again: the press must not land later.
        Require(d.queued().code == 0 && d.TakePress(s.v.screen).code == 0,
                "a failed driver still holds its press");
    }
    {
        Driver d(P({"-net-host", "200", "-net-name", "HOST1"}));
        Shell s;
        s.v.titleScreen = false; // some other menu screen, never the main menu
        s.Run(d, 3000, 50);
        Require(d.step() == Step::Failed && Contains(d.error(), "main menu"), "missing main menu not reported");
    }
}

// The queued press: only its own screen takes it, once, and nothing is left
// queued after the driver stopped.
static void TestPressBookkeeping()
{
    {
        // Start (1200) is queued, then the lobby is left (Escape) before the
        // screen took it: the press must not land on the next screen.
        Driver d(P({"-net-host", "200", "-net-name", "HOST1", "-net-start-when", "2"}));
        Shell s;
        s.Run(d, 100, 50, [&] { return d.step() == Step::WaitStart; });
        s.v.players.push_back(Remote("C1", 3, true));
        s.Run(d, 200, 50, [&] { return d.queued().code != 0; });
        Require(d.queued().code == CodeOk && d.queued().widget == WidgetOk && d.queued().screen == Screen::Session,
                "Start was not queued for the session screen: " + s.Trace());
        s.v.netMenu = false;
        s.v.titleScreen = true;
        s.v.screen = Screen::Provider;
        s.Apply(d.Next(s.v));
        Require(d.step() == Step::Failed && Contains(d.error(), "changed before it took"),
                "a press orphaned by leaving the lobby was not reported: " + d.error());
        s.v.netMenu = true; // the player opens the multiplayer menu again
        Require(d.queued().code == 0 && d.TakePress(Screen::Provider).code == 0 &&
                d.TakePress(Screen::Session).code == 0, "the orphaned press is still queued");
    }
    {
        // A press made on one screen is never handed to another one.
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1", "-net-faction", "4"}));
        Shell s;
        s.remote = {Remote("HOST1", 1, true)};
        s.v.welcomed = true;
        s.Run(d, 300, 50, [&] { return d.queued().code != 0 && d.step() == Step::Faction; });
        Require(d.queued().code == FactionCode(4), "the faction press was not queued: " + s.Trace());
        Require(d.TakePress(Screen::Map).code == 0 && d.queued().code == FactionCode(4),
                "a press was handed to another screen");
        const Press taken = d.TakePress(Screen::Session);
        Require(taken.code == FactionCode(4) && taken.widget == FactionWidget(4), "the press was not handed out");
        Require(d.queued().code == 0 && d.TakePress(Screen::Session).code == 0, "a press was handed out twice");
    }
    {
        // The match started: nothing more is pressed.
        Driver d(P({"-net-join", "127.0.0.1", "-net-name", "C1", "-net-ready"}));
        Shell s;
        s.remote = {Remote("HOST1", 1, true)};
        s.v.welcomed = true;
        s.Run(d, 300, 50, [&] { return d.step() == Step::WaitStart; });
        s.v.started = true;
        s.Run(d, 2);
        Require(d.step() == Step::InGame && d.queued().code == 0, "a press is queued after the start");
    }
}

int main()
{
    TestValidation();
    TestParse();
    TestHostFlow();
    TestClientFlow();
    TestJoinFailures();
    TestFactionProblems();
    TestHostProblems();
    TestPressBookkeeping();
    std::cout << "PASS: -net-* parsing, validation and the host/client lobby drivers\n";
    return 0;
}
