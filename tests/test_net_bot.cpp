#include "../src/world/net_bot.h"
#include "require.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <set>
#include <string>

using namespace World::NetBot;
using World::Fork::ForkRng;
using World::Fork::UnitState;
using World::Fork::WorldState;

static WorldState MakeState(bool foeNear)
{
    WorldState s;
    s.mapW = 4;
    s.mapH = 3;
    s.sectors.resize(12);
    for (int i = 0; i < 12; ++i)
    {
        s.sectors[i].id = i;
        s.sectors[i].energyPower = 200;
    }
    s.sectors[0].owner = 1;
    s.sectors[11].owner = 6;
    UnitState own;
    own.gid = 0x10000;
    own.protoId = 10;
    own.owner = 1;
    own.energy = 100;
    own.x = 100; own.y = 100;
    UnitState foe;
    foe.gid = 0x10001;
    foe.protoId = 11;
    foe.owner = 6;
    foe.energy = 80;
    foe.x = foeNear ? 250.0f : 4000.0f;
    foe.y = foeNear ? 100.0f : 3000.0f;
    s.units = {own, foe};
    s.nextGid = 0x10002;
    return s;
}

// A six-owner match at the fork cap on a 54 x 32 map: each owner holds a
// band of sectors and keeps its units there, the bands touching. With `ring`,
// Resistance's units instead stand on a circle 2200 units around everyone
// else, just beyond Veteran sensor range (2160): the costliest layout for the
// fog checks, since every enemy is near own units and none is in range.
static WorldState CapWorld(bool ring)
{
    WorldState s;
    s.mapW = 54;
    s.mapH = 32;
    s.sectors.resize(54 * 32);
    for (int i = 0; i < 54 * 32; ++i)
    {
        World::Fork::SectorState &sec = s.sectors[i];
        const int x = i % 54, y = i / 54;
        sec.id = i;
        sec.energyPower = 200;
        sec.border = x == 0 || y == 0 || x == 53 || y == 31;
        sec.owner = sec.border ? 0 : static_cast<std::uint8_t>(1 + (x * 6) / 54);
        sec.viewMask = ring ? 0u : 1u << sec.owner;
    }
    ForkRng rng(3);
    const float len = World::CVSectorLength;
    for (std::uint32_t i = 0; i < World::Fork::MaxWorldUnits; ++i)
    {
        UnitState u;
        u.gid = 0x10000 + i;
        u.protoId = 10;
        u.owner = static_cast<std::uint8_t>(1 + i % 6);
        u.energy = static_cast<std::int32_t>(100 + rng.Below(400));
        if (!ring)
        {
            const float band = (u.owner - 1) * (52.0f / 6.0f) + 1.0f;
            u.x = (band + rng.Below(1000) / 1000.0f * (52.0f / 6.0f)) * len;
            u.y = (1.0f + rng.Below(30000) / 1000.0f) * len;
        }
        else if (u.owner == 1)
        {
            const float a = static_cast<float>(i) * 0.0123f;
            u.x = 20.5f * len + 2200.0f * std::cos(a);
            u.y = 16.5f * len + 2200.0f * std::sin(a);
        }
        else
        {
            u.x = 20.5f * len + static_cast<float>(rng.Below(20));
            u.y = 16.5f * len + static_cast<float>(rng.Below(20));
        }
        s.units.push_back(u);
    }
    s.nextGid = 0x10000 + World::Fork::MaxWorldUnits;
    return s;
}

static std::vector<int> WeightKey(const BotProfile &p)
{
    return {p.wConquer, p.wDefense, p.wRadar, p.wPower, p.wSafety, p.wRecon,
            p.wRoboAttack, p.squadSize, p.canConstruct ? 1 : 0};
}

int main()
{
    // Six distinct faction personalities.
    std::size_t count = 0;
    const BotProfile *all = AllProfiles(count);
    REQUIRE(count == 6);
    std::set<std::uint8_t> owners;
    for (std::size_t i = 0; i < count; ++i)
    {
        owners.insert(all[i].owner);
        for (std::size_t j = i + 1; j < count; ++j)
            Require(WeightKey(all[i]) != WeightKey(all[j]), "faction personalities are distinct");
    }
    REQUIRE(owners.size() == 6);
    Require(!ProfileForOwner(2).canConstruct, "Sulgogars never get construction wishes");
    REQUIRE(std::strcmp(ProfileForOwner(5).factionName, "Black Sect") == 0);

    // Difficulty scaling stays inside legal observation limits.
    const DifficultyParams cadet = ParamsFor(Difficulty::Cadet);
    const DifficultyParams vet = ParamsFor(Difficulty::Veteran);
    REQUIRE(cadet.reactionMs > vet.reactionMs);
    REQUIRE(cadet.lookaheadDepth == 0 && vet.lookaheadDepth == 2);
    REQUIRE(vet.sensorRange <= 2160.0f);
    REQUIRE(cadet.noisePct > vet.noisePct);

    // Station AI gate: only a bot-flagged station may take the native AI
    // path while marked as the player robo; humans and AI stations keep
    // their existing paths.
    Require(!StationUsesNativeAI(true, false), "human player station keeps player path");
    Require(StationUsesNativeAI(true, true), "bot station uses native AI path");
    Require(StationUsesNativeAI(false, false), "AI station uses native AI path");
    Require(StationUsesNativeAI(false, true), "bot flag does not break AI station");

    // Spec parsing.
    const BotSpec gh = ParseBotSpec("Ghorkov:Veteran");
    Require(gh.valid && gh.owner == 6 && gh.difficulty == Difficulty::Veteran, "name spec parses");
    const BotSpec bs = ParseBotSpec("black-sect:cadet");
    Require(bs.valid && bs.owner == 5 && bs.difficulty == Difficulty::Cadet, "dash spec parses");
    const BotSpec num = ParseBotSpec("2:0");
    Require(num.valid && num.owner == 2 && num.difficulty == Difficulty::Cadet, "numeric spec parses");
    Require(!ParseBotSpec("Ghorkov").valid, "missing difficulty rejected");
    Require(!ParseBotSpec("Nobody:Normal").valid, "unknown faction rejected");
    Require(!ParseBotSpec("Ghorkov:Ultra").valid, "unknown difficulty rejected");
    // Callsigns: the name screen refuses spaces and '*', ZNDNet cuts names to
    // 20 characters, and a session needs unique names.
    REQUIRE(BotDisplayName(gh) == "Bot-Ghorkov-Vet");
    {
        std::set<std::string> callsigns;
        for (int owner : World::NetFactions::Owners)
            for (Difficulty d : {Difficulty::Cadet, Difficulty::Normal, Difficulty::Veteran})
            {
                BotSpec spec;
                spec.owner = static_cast<std::uint8_t>(owner);
                spec.difficulty = d;
                spec.valid = true;
                const std::string name = BotDisplayName(spec);
                REQUIRE(!name.empty() && name.size() <= MaxCallsignLength);
                for (char c : name)
                    REQUIRE(c > ' ' && c < 127 && c != '*');
                callsigns.insert(name);
            }
        REQUIRE(callsigns.size() == 18);
    }

    // Roster fill: humans keep slots, bots fill the rest and can start.
    const World::NetFactions::RosterEntry humans[2] = {{0, 2, true}, {1, 64, true}};
    const BotSpec bots[2] = {ParseBotSpec("Mykonian:Normal"), ParseBotSpec("Sulgogars:Cadet")};
    World::NetFactions::RosterEntry roster[6];
    std::size_t rosterCount = 0;
    Require(BuildRosterWithBots(126, humans, 2, bots, 2, roster, rosterCount), "bot roster builds");
    REQUIRE(rosterCount == 4);
    REQUIRE(World::NetFactions::CheckRoster(126, roster, rosterCount).CanStart());
    const BotSpec tooMany[5] = {gh, gh, gh, gh, gh};
    Require(!BuildRosterWithBots(126, humans, 2, tooMany, 5, roster, rosterCount),
            "seventh player rejected");
    const BotSpec bad;
    Require(!BuildRosterWithBots(126, humans, 2, &bad, 1, roster, rosterCount),
            "invalid bot spec rejected");

    // Legal observations: a hidden enemy produces no attack order.
    ForkRng rng(1234);
    const BotSpec spec = ParseBotSpec("Resistance:Veteran");
    std::vector<BotOrder> hidden = CandidateOrders(MakeState(false), spec, rng);
    for (const BotOrder &o : hidden)
        Require(o.kind != OrderKind::Attack, "bot never attacks what it cannot see");
    std::vector<BotOrder> visible = CandidateOrders(MakeState(true), spec, rng);
    bool sawAttack = false;
    for (const BotOrder &o : visible)
        if (o.kind == OrderKind::Attack && o.targetGid == 0x10001)
            sawAttack = true;
    Require(sawAttack, "visible enemy is attacked");

    // Decide is deterministic and never mutates the input state.
    const WorldState state = MakeState(true);
    const std::uint64_t hashBefore = World::Fork::HashState(state);
    ForkRng r1(99), r2(99);
    const BotOrder d1 = Decide(state, spec, r1, World::Fork::ForkId{1});
    const BotOrder d2 = Decide(state, spec, r2, World::Fork::ForkId{1});
    REQUIRE(d1.kind == d2.kind && d1.gid == d2.gid && d1.targetGid == d2.targetGid);
    REQUIRE(World::Fork::HashState(state) == hashBefore);
    Require(d1.kind != OrderKind::None, "veteran decide returns an order");

    // Lookahead reports what it could not do instead of silently playing the
    // greedy choice: a large match forks (the old 1024-unit fork cap made
    // every fork fail above it), a world above the cap is reported.
    {
        WorldState big = MakeState(true);
        while (big.units.size() < 1500)
        {
            UnitState u = big.units[big.units.size() % 2];
            u.gid = big.nextGid++;
            u.x += static_cast<float>(big.units.size() % 37) * 30.0f;
            big.units.push_back(u);
        }
        DecideReport report;
        ForkRng rng(7);
        const BotOrder order = Decide(big, spec, rng, World::Fork::ForkId{1}, &report);
        Require(order.kind != OrderKind::None, "a large match still decides");
        Require(report.forksCreated > 0 && report.forkFailures == 0 && !report.greedyFallback,
                "a 1500-unit match gets its lookahead");
        while (big.units.size() <= World::Fork::MaxWorldUnits)
        {
            UnitState u = big.units[0];
            u.gid = big.nextGid++;
            big.units.push_back(u);
        }
        DecideReport over;
        ForkRng rng2(7);
        const BotOrder fallback = Decide(big, spec, rng2, World::Fork::ForkId{1}, &over);
        Require(fallback.kind != OrderKind::None, "above the cap a greedy order remains");
        Require(over.forkFailures > 0 && over.greedyFallback && over.forksCreated == 0,
                "a lookahead that cannot fork is reported");
        DecideReport cadet;
        ForkRng rng3(7);
        Decide(MakeState(true), ParseBotSpec("Resistance:Cadet"), rng3, World::Fork::ForkId{1}, &cadet);
        Require(cadet.forksCreated == 0 && cadet.forkFailures == 0 && !cadet.greedyFallback,
                "a greedy difficulty is not a fallback");
    }

    // The cost of a think at the cap. Decide runs on the main loop, up to
    // every 250 ms for a Veteran. Its nine fog checks used to scan the world
    // once per enemy unit (9 x 3413 x 4096 unit visits per think here); they
    // now test only own units near each enemy. Budget: a Veteran think in
    // this match makes at most one range test per unit of the world.
    {
        const WorldState spread = CapWorld(false);
        DecideReport report;
        ForkRng rng(11);
        const BotOrder order = Decide(spread, spec, rng, World::Fork::ForkId{1}, &report);
        Require(order.kind != OrderKind::None, "a think at the cap decides");
        Require(report.forksCreated == 8 && report.forkFailures == 0 && !report.greedyFallback &&
                !report.lookaheadCut, "a think at the cap gets its whole lookahead");
        Require(report.rangeTests > 0 && report.rangeTests <= World::Fork::MaxWorldUnits,
                "a think at the cap stays within its fog-check budget");
        // Both kinds of fog check go through the counted view.
        std::uint64_t candidateTests = 0, evaluateTests = 0;
        ForkRng crng(11);
        CandidateOrders(spread, spec, crng, &candidateTests);
        EvaluateFor(spread, spec.owner, ParamsFor(spec.difficulty).sensorRange, &evaluateTests);
        Require(candidateTests > 0 && candidateTests <= spread.units.size() &&
                evaluateTests > 0 && evaluateTests <= spread.units.size(),
                "each fog check tests only own units near the enemies");

        // Own units strung just beyond sensor range around every enemy: each
        // fog check of Resistance tests about 683 own units per enemy. The
        // think stops forking once MaxLookaheadRangeTests is spent, says so,
        // and still plays the best order scored before the cut.
        const WorldState ring = CapWorld(true);
        DecideReport cut;
        ForkRng rng3(11);
        const BotOrder cutOrder = Decide(ring, spec, rng3, World::Fork::ForkId{1}, &cut);
        Require(cutOrder.kind != OrderKind::None, "a cut think still decides");
        Require(cut.lookaheadCut && cut.rangeTests > MaxLookaheadRangeTests,
                "a lookahead past its budget is cut and reported");
        Require(cut.forksCreated > 0 && cut.forksCreated < 8 && !cut.greedyFallback,
                "the candidates scored before the cut count");
        // At most the budget plus the two fog checks of the candidate that
        // crossed it, each at most enemies x own units.
        const std::uint64_t perCheck = std::uint64_t(World::Fork::MaxWorldUnits / 2) *
                                       (World::Fork::MaxWorldUnits / 2);
        Require(cut.rangeTests <= MaxLookaheadRangeTests + 2 * perCheck, "the cut bounds the think");
        DecideReport again;
        ForkRng rng4(11);
        const BotOrder repeat = Decide(ring, spec, rng4, World::Fork::ForkId{1}, &again);
        Require(repeat.kind == cutOrder.kind && repeat.gid == cutOrder.gid &&
                repeat.targetGid == cutOrder.targetGid && again.rangeTests == cut.rangeTests,
                "a cut think is deterministic");
    }

    // A Veteran that no enemy can answer still looks ahead at every
    // candidate. It used to judge only the first one: with no unit of
    // another faction, or none whose faction had an order, it scored the
    // first candidate and skipped the rest. Here the best-scored candidate
    // builds (Resistance scores that 30, the moves 12.5 and 15, apart even
    // under Cadet noise), but the unit stands 30 units from the centre of the
    // empty sector it is in, so moving there takes the sector within the
    // step: a better position than a build that only spends sector energy.
    {
        WorldState s;
        s.mapW = 4;
        s.mapH = 3;
        s.sectors.resize(12);
        for (int i = 0; i < 12; ++i)
            s.sectors[i].id = i;
        s.sectors[0].owner = 1;
        s.sectors[0].energyPower = 200;
        s.sectors[0].viewMask = 1u << 1;
        s.sectors[1].energyPower = 100;
        s.sectors[1].viewMask = 1u << 1;
        UnitState own;
        own.gid = 0x10000;
        own.owner = 1;
        own.energy = 100;
        own.x = 1.5f * World::CVSectorLength - 30.0f;
        own.y = 0.5f * World::CVSectorLength;
        s.units = {own};
        s.nextGid = 0x10001;
        const float centreX = 1.5f * World::CVSectorLength, centreY = 0.5f * World::CVSectorLength;
        ForkRng r0(1), r1(1), r2(1);
        const BotOrder cadet = Decide(s, ParseBotSpec("Resistance:Cadet"), r0, World::Fork::ForkId{1});
        Require(cadet.kind == OrderKind::Build, "the best-scored candidate builds");
        const BotOrder normal = Decide(s, ParseBotSpec("Resistance:Normal"), r1, World::Fork::ForkId{1});
        const BotOrder veteran = Decide(s, spec, r2, World::Fork::ForkId{1});
        Require(normal.kind == OrderKind::Move && normal.x == centreX && normal.y == centreY,
                "Normal takes the empty sector");
        Require(veteran.kind == OrderKind::Move && veteran.x == centreX && veteran.y == centreY,
                "a Veteran no enemy can answer takes it too");
    }

    // Sulgogar bot never emits build orders.
    ForkRng r3(5);
    const BotSpec sulg = ParseBotSpec("Sulgogars:Veteran");
    for (const BotOrder &o : CandidateOrders(state, sulg, r3))
        Require(o.kind != OrderKind::Build, "Sulgogar candidate list has no builds");

    // Lifecycle: join -> ready -> game -> leave; takeover needs same owner.
    BotLifecycle life;
    life.spec = ParseBotSpec("Ghorkov:Normal");
    Require(!life.OnGameLoaded(), "cannot load before ready");
    Require(life.OnRosterAccepted() && life.OnReadySent() && life.OnGameLoaded(), "join flow");
    REQUIRE(life.phase == BotPhase::InGame);
    BotLifecycle standby;
    standby.spec = ParseBotSpec("Ghorkov:Normal");
    Require(!standby.OnTakeoverOffer(3), "takeover with wrong owner rejected");
    Require(standby.OnTakeoverOffer(6), "takeover with matching owner accepted");
    BotLifecycle adopted;
    adopted.spec = ParseBotSpec("Ghorkov:Normal");
    Require(adopted.OnTakeoverOffer(6) && adopted.OnGameLoaded(), "takeover resumes in game");
    Require(adopted.OnHumanReclaim() && adopted.OnReclaimComplete(), "human reclaim flow");
    REQUIRE(adopted.phase == BotPhase::Gone);
    // A leave waits for the transport leave: Leaving until OnDestroyed.
    Require(life.OnLeaveRequested() && life.phase == BotPhase::Leaving, "leave requested");
    Require(!life.OnLeaveRequested() && life.phase == BotPhase::Leaving, "leave is requested once");
    Require(life.OnDestroyed(), "leave flow");
    REQUIRE(life.phase == BotPhase::Gone);

    // Each match starts from the lobby: a bot left InGame by its last match
    // refused every takeover offer.
    BotLifecycle reused;
    reused.spec = ParseBotSpec("Ghorkov:Normal");
    Require(reused.OnRosterAccepted() && reused.OnReadySent() && reused.OnGameLoaded(), "first match");
    Require(!reused.OnTakeoverOffer(6), "a bot in a match takes nothing over");
    reused.ResetForNextMatch();
    Require(reused.phase == BotPhase::Lobby && !reused.humanWasDisconnected, "reset to the lobby");
    Require(reused.OnRosterAccepted() && reused.OnReadySent() && reused.OnGameLoaded(), "second match");
    reused.ResetForNextMatch();
    Require(reused.OnTakeoverOffer(6), "takeover offer accepted after a match");
    reused.ResetForNextMatch();
    Require(reused.phase == BotPhase::Lobby && !reused.humanWasDisconnected, "takeover state cleared");

    // Chat rules mirror the UAMSG_MESSAGE receiver, which plays speech sample
    // 9<n> instead of showing the text when int(strtol(text, 0, 0)) > 0.
    Require(ValidBotChatLine("Holding the line."), "plain taunt valid");
    Require(!ValidBotChatLine("9 lives left"), "leading digit rejected");
    Require(!ValidBotChatLine(" 9 lives left"), "leading space before a number rejected");
    Require(!ValidBotChatLine("+5 incoming"), "signed number rejected");
    Require(!ValidBotChatLine("0x1F"), "hexadecimal number rejected");
    Require(ValidBotChatLine("Zero hour."), "words are not numbers");
    for (const char *line : {"9 lives left", " 9 lives left", "+5 incoming", "   12", "0x1F",
                             "010 left", "-4294967295 down", "4294967297", "99999999999 more",
                             "-3 down", "0 left", "-0", "+", " ", "x9", "Zero hour."})
    {
        const bool receiverPlaysSpeech = static_cast<int>(std::strtol(line, nullptr, 0)) > 0;
        if (receiverPlaysSpeech)
            Require(!ValidBotChatLine(line), "a line the receiver plays as speech is rejected");
    }
    Require(!ValidBotChatLine(""), "empty line rejected");
    Require(!ValidBotChatLine(std::string(64, 'a')), "64-char line rejected");
    Require(ValidBotChatLine(std::string(63, 'a')), "63-char line valid");
    for (int owner : World::NetFactions::Owners)
        Require(ValidBotChatLine(TauntFor(static_cast<std::uint8_t>(owner), 1)),
                "every faction taunt is wire-safe");
    REQUIRE(!TauntDue(1000, 5000));
    REQUIRE(TauntDue(1000, 21000));
    REQUIRE(TauntDue(-1, 0));

    // Seating: the lobby decides the faction; the brain follows the seat.
    {
        BotSpec requested = ParseBotSpec("Ghorkov:Veteran");
        bool mismatch = false;
        BotSpec seated = BindSpecToOwner(requested, 4, mismatch);
        Require(mismatch && seated.owner == 4 && seated.valid &&
                seated.difficulty == Difficulty::Veteran, "bot follows its seated owner");
        seated = BindSpecToOwner(requested, 6, mismatch);
        Require(!mismatch && seated.owner == 6, "matching seat reports no mismatch");
        seated = BindSpecToOwner(BotSpec(), 4, mismatch);
        Require(!mismatch && !seated.valid, "invalid request stays invalid");
    }

    // Steering is stateless: any order sequence leaves every wish at its
    // profile value except the one lifted for the current order, so the six
    // personalities never converge on an all-100 station.
    {
        std::size_t count = 0;
        const BotProfile *all = AllProfiles(count);
        const OrderKind sequence[] = {OrderKind::Attack, OrderKind::Build, OrderKind::Move,
                                      OrderKind::Attack, OrderKind::None};
        std::set<std::vector<int>> distinct;
        for (std::size_t i = 0; i < count; ++i)
        {
            const BotProfile &p = all[i];
            WishWeights w;
            for (OrderKind k : sequence)
                w = WeightsForOrder(p, k);
            const WishWeights base = BaseWeights(p);
            Require(w.conquer == base.conquer && w.power == base.power &&
                    w.roboAttack == base.roboAttack && w.radar == base.radar,
                    "a no-order think restores the profile weights");
            const WishWeights attack = WeightsForOrder(p, OrderKind::Attack);
            Require(attack.roboAttack == std::min(100, base.roboAttack + 30) &&
                    attack.conquer == base.conquer && attack.power == base.power,
                    "attack lifts only the robo-attack wish");
            const WishWeights build = WeightsForOrder(p, OrderKind::Build);
            Require(build.power == std::min(100, base.power + 30) &&
                    build.roboAttack == base.roboAttack, "build lifts power/radar only");
            for (OrderKind k : {OrderKind::Attack, OrderKind::Build, OrderKind::Move})
            {
                const WishWeights x = WeightsForOrder(p, k);
                for (int v : {x.conquer, x.defense, x.radar, x.power, x.safety, x.recon, x.roboAttack})
                    Require(v >= 0 && v <= 100, "wish weight stays within 0..100");
            }
            distinct.insert({attack.conquer, attack.defense, attack.radar, attack.power,
                             attack.safety, attack.recon, attack.roboAttack});
        }
        Require(distinct.size() == count, "faction personalities stay distinct under orders");
    }

    std::cout << "net bot profiles, roster, observation, lifecycle and chat tests passed\n";
    return 0;
}
