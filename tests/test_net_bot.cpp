#include "../src/world/net_bot.h"
#include "require.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>

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
    REQUIRE(BotDisplayName(gh) == "BOT Ghorkov (Veteran)");

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
    Require(life.OnLeaveRequested() && life.OnDestroyed(), "leave flow");
    REQUIRE(life.phase == BotPhase::Gone);

    // Chat rules mirror the UAMSG_MESSAGE receiver (no leading digit).
    Require(ValidBotChatLine("Holding the line."), "plain taunt valid");
    Require(!ValidBotChatLine("9 lives left"), "leading digit rejected");
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
