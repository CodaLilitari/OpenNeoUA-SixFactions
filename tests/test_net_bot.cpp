#include "../src/world/net_bot.h"
#include "require.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace World::NetBot;
using World::Fork::EncodeState;
using World::Fork::ForkRng;
using World::Fork::SectorState;
using World::Fork::UnitState;
using World::Fork::UnitVisibleTo;
using World::Fork::VisibleWorld;
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

// CapWorld(true) with the roles turned around: Resistance massed in one spot
// and half the cap Sulgogars on a circle 2100 around it. That is inside
// Veteran sensor range (2160), so the bot sees every Sulgogar, but beyond the
// 2000 of the Normal bot the strongest faction answers as. Every answer's fog
// check then tests each Resistance unit against the hundreds of circle units
// in cells within reach, none in range.
static WorldState CircledWorld()
{
    WorldState s = CapWorld(true);
    ForkRng rng(5);
    const float len = World::CVSectorLength;
    const float cx = 20.5f * len + 10.0f, cy = 16.5f * len + 10.0f;
    const std::uint32_t circle = World::Fork::MaxWorldUnits / 2;
    for (std::uint32_t i = 0; i < s.units.size(); ++i)
    {
        UnitState &u = s.units[i];
        if (i < circle)
        {
            const float a = static_cast<float>(i) * (6.2831853f / static_cast<float>(circle));
            u.owner = 2;
            u.x = cx + 2100.0f * std::cos(a);
            u.y = cy + 2100.0f * std::sin(a);
        }
        else
        {
            u.owner = 1;
            u.x = 20.5f * len + static_cast<float>(rng.Below(20));
            u.y = 16.5f * len + static_cast<float>(rng.Below(20));
        }
    }
    return s;
}

// A small random world like the fog probe's: a 6 x 4 map whose sectors
// belong to nobody, Resistance (1), the Sulgogars (2), the Mykonians (3) or
// Ghorkov (6), each seen by its owner and now and then by the others, with
// random energy, power stations and borders; one to three Resistance units,
// up to three of each enemy faction and sometimes one of nobody's, anywhere
// on the map, with gids in random owner order and with gaps.
static WorldState FogWorld(ForkRng &rng)
{
    WorldState s;
    s.mapW = 6;
    s.mapH = 4;
    s.sectors.resize(24);
    const std::uint8_t sectorOwners[] = {0, 0, 1, 2, 3, 6};
    for (int i = 0; i < 24; ++i)
    {
        SectorState &sec = s.sectors[i];
        sec.id = i;
        sec.owner = sectorOwners[rng.Below(6)];
        sec.energyPower = 50 + static_cast<std::int32_t>(rng.Below(300));
        sec.purpose = rng.Below(4) == 0 ? 2 : 0;
        sec.typeId = static_cast<std::int32_t>(rng.Below(40));
        sec.border = rng.Below(8) == 0;
        sec.viewMask = sec.owner ? 1u << sec.owner : 0u;
        for (unsigned o : {1u, 2u, 3u, 6u})
            if (rng.Below(3) == 0)
                sec.viewMask |= 1u << o;
    }
    std::vector<std::uint8_t> owners(1 + rng.Below(3), 1);
    for (std::uint8_t o : {2, 3, 6})
        owners.insert(owners.end(), rng.Below(4), o);
    if (rng.Below(4) == 0)
        owners.push_back(0);
    for (std::size_t i = owners.size(); i > 1; --i)
        std::swap(owners[i - 1], owners[rng.Below(static_cast<std::uint32_t>(i))]);
    const float len = World::CVSectorLength;
    std::uint32_t gid = 0x10000;
    for (std::uint8_t o : owners)
    {
        UnitState u;
        u.gid = gid;
        gid += 1 + rng.Below(3);
        u.owner = o;
        u.protoId = 10 + static_cast<std::int32_t>(rng.Below(4));
        u.energy = 20 + static_cast<std::int32_t>(rng.Below(400));
        u.x = static_cast<float>(rng.Below(static_cast<std::uint32_t>(6 * len)));
        u.y = static_cast<float>(rng.Below(static_cast<std::uint32_t>(4 * len)));
        s.units.push_back(u);
    }
    s.nextGid = gid;
    return s;
}

// FogWorld s with everything Resistance (owner 1) cannot see at `range`
// changed: the units it cannot see moved (out of its sight), re-energized and
// re-typed, some dropped, new ones added out of its sight with gids below,
// between and above the others; every sector it cannot see given another
// owner, energy, purpose, type and view bits; the other owners' view bits
// changed on every sector; the parent, orders and cooldown of every unit of
// another owner changed; the gid counter moved on. Own units, Resistance's
// view bits and the border flags (the map fixes them) stay, so every position
// is in or out of its sight in both worlds alike. Returns the hidden units
// moved, dropped or added.
static int HideChanges(const WorldState &s, float range, ForkRng &rng, WorldState &v)
{
    const float len = World::CVSectorLength;
    const std::uint8_t others[] = {0, 2, 3, 6};
    const auto hidden = [&](const UnitState &u) {
        return u.owner != 1 && !UnitVisibleTo(s, u, 1, range);
    };
    const auto hide = [&](UnitState &u) {
        for (int k = 0; k < 20; ++k)
        {
            UnitState t = u;
            t.x = static_cast<float>(rng.Below(static_cast<std::uint32_t>(6 * len)));
            t.y = static_cast<float>(rng.Below(static_cast<std::uint32_t>(4 * len)));
            if (hidden(t))
            {
                u = t;
                return true;
            }
        }
        return false;
    };
    const auto anyGid = [&]() { return s.units[rng.Below(static_cast<std::uint32_t>(s.units.size()))].gid; };
    int touched = 0;
    v = s;
    v.units.clear();
    for (UnitState u : s.units)
    {
        if (hidden(u))
        {
            ++touched;
            if (rng.Below(4) == 0)
                continue;
            hide(u);
            u.energy = 20 + static_cast<std::int32_t>(rng.Below(400));
            u.protoId = 10 + static_cast<std::int32_t>(rng.Below(4));
        }
        if (u.owner != 1)
        {
            u.parentGid = anyGid();
            u.hasTarget = rng.Below(2) != 0;
            u.targetX = static_cast<float>(rng.Below(static_cast<std::uint32_t>(6 * len)));
            u.targetY = static_cast<float>(rng.Below(static_cast<std::uint32_t>(4 * len)));
            u.attackGid = anyGid();
            u.cooldownMs = static_cast<std::int32_t>(rng.Below(500));
        }
        v.units.push_back(u);
    }
    const std::uint32_t top = s.units.back().gid;
    for (std::uint32_t n = rng.Below(4); n > 0; --n)
    {
        UnitState u;
        u.gid = 0xFF00 + rng.Below(top - 0xFF00 + 0x100);
        u.owner = others[rng.Below(4)];
        u.protoId = 10 + static_cast<std::int32_t>(rng.Below(4));
        u.energy = 20 + static_cast<std::int32_t>(rng.Below(400));
        if (v.FindUnit(u.gid) || !hide(u))
            continue;
        ++touched;
        v.units.insert(std::lower_bound(v.units.begin(), v.units.end(), u,
                                        [](const UnitState &a, const UnitState &b) { return a.gid < b.gid; }),
                       u);
    }
    for (SectorState &sec : v.sectors)
    {
        const bool seen = sec.owner == 1 || (sec.viewMask & 2u);
        sec.viewMask = (sec.viewMask & 2u) | (rng.Below(128) & ~2u);
        if (seen)
            continue;
        sec.owner = others[rng.Below(4)];
        sec.energyPower = static_cast<std::int32_t>(rng.Below(400));
        sec.purpose = static_cast<std::uint8_t>(rng.Below(3));
        sec.typeId = static_cast<std::int32_t>(rng.Below(40));
    }
    v.nextGid = std::max(top, v.units.back().gid) + 1 + rng.Below(100);
    return touched;
}

// FogWorld s with what Resistance sees at `range` changed: every unit of
// another owner it sees re-energized, every sector of another owner it sees
// given another owner and energy.
static void SeenChanges(const WorldState &s, float range, ForkRng &rng, WorldState &w)
{
    const std::uint8_t others[] = {0, 2, 3, 6};
    w = s;
    for (UnitState &u : w.units)
        if (u.owner != 1 && UnitVisibleTo(s, u, 1, range))
            u.energy = 20 + static_cast<std::int32_t>(rng.Below(400));
    for (SectorState &sec : w.sectors)
        if (sec.owner != 1 && (sec.viewMask & 2u))
        {
            sec.owner = others[rng.Below(4)];
            sec.energyPower = 50 + static_cast<std::int32_t>(rng.Below(300));
        }
}

// Whether forks of the captured worlds, as the lookahead used to make them,
// tell s and v apart: one of the bot's best four candidates, stepped as
// Decide steps it, leaves it a position judged differently.
static bool ForksOfTheWorldDiffer(const WorldState &s, const WorldState &v, const BotSpec &spec)
{
    ForkRng rng(777);
    const std::vector<BotOrder> candidates = CandidateOrders(s, spec, rng);
    World::Fork::ForkConfig cfg;
    cfg.controllerOwner = spec.owner;
    const float range = ParamsFor(spec.difficulty).sensorRange;
    for (std::size_t i = 0; i < candidates.size() && i < 4; ++i)
    {
        std::unique_ptr<World::Fork::Fork> a = World::Fork::Fork::Create(s, cfg, World::Fork::ForkId{1});
        std::unique_ptr<World::Fork::Fork> b = World::Fork::Fork::Create(v, cfg, World::Fork::ForkId{1});
        REQUIRE(a && b);
        const World::Fork::ForkActionBatch batch(1, ToForkAction(candidates[i]));
        a->Step(batch, 25);
        b->Step(batch, 25);
        if (EvaluateFor(a->State(), spec.owner, range) != EvaluateFor(b->State(), spec.owner, range))
            return true;
    }
    return false;
}

static bool SameOrder(const BotOrder &a, const BotOrder &b)
{
    return a.kind == b.kind && a.gid == b.gid && a.targetGid == b.targetGid && a.x == b.x &&
           a.y == b.y && a.protoId == b.protoId && a.cost == b.cost && a.score == b.score;
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
    // every 250 ms for a Veteran. Its fog checks (the picture, the
    // candidates, and an answer and an evaluation per candidate) used to scan
    // the world once per enemy unit (3413 x 4096 unit visits per check here);
    // they now test only own units near each enemy. Budget: a Veteran think
    // in this match makes at most one range test per unit of the world.
    {
        const WorldState spread = CapWorld(false);
        const float range = ParamsFor(spec.difficulty).sensorRange;
        DecideReport report;
        ForkRng rng(11);
        const BotOrder order = Decide(spread, spec, rng, World::Fork::ForkId{1}, &report);
        Require(order.kind != OrderKind::None, "a think at the cap decides");
        Require(report.forksCreated == 8 && report.forkFailures == 0 && !report.greedyFallback &&
                !report.lookaheadCut, "a think at the cap gets its whole lookahead");
        Require(report.rangeTests > 0 && report.rangeTests <= World::Fork::MaxWorldUnits,
                "a think at the cap stays within its fog-check budget");
        // Every kind of fog check goes through the counted view.
        std::uint64_t pictureTests = 0, candidateTests = 0, evaluateTests = 0;
        VisibleWorld(spread, spec.owner, range, &pictureTests);
        ForkRng crng(11);
        CandidateOrders(spread, spec, crng, &candidateTests);
        EvaluateFor(spread, spec.owner, range, &evaluateTests);
        Require(pictureTests > 0 && pictureTests <= spread.units.size() &&
                candidateTests > 0 && candidateTests <= spread.units.size() &&
                evaluateTests > 0 && evaluateTests <= spread.units.size(),
                "each fog check tests only own units near the enemies");

        // Own units strung just beyond sensor range around every enemy: the
        // fog check of Resistance tests about 683 own units per enemy. The
        // lookahead pays that once, for its picture, where no enemy is left:
        // nothing after it makes a range test, nothing is cut, and every
        // candidate is scored.
        const WorldState ring = CapWorld(true);
        DecideReport once;
        ForkRng rng2(11);
        const BotOrder ringOrder = Decide(ring, spec, rng2, World::Fork::ForkId{1}, &once);
        std::uint64_t ringPicture = 0;
        const WorldState ringSeen = VisibleWorld(ring, spec.owner, range, &ringPicture);
        Require(ringSeen.units.size() == (World::Fork::MaxWorldUnits + 5) / 6,
                "the bot sees its own units only");
        Require(ringOrder.kind != OrderKind::None && ringPicture > MaxLookaheadRangeTests / 4 &&
                once.rangeTests == ringPicture, "units out of sight cost the picture's check only");
        Require(once.forksCreated == 4 && once.forkFailures == 0 && !once.greedyFallback &&
                !once.lookaheadCut, "units out of sight do not cut the lookahead");

        // The costly layout for the lookahead itself: every Sulgogar on a
        // circle around the bot is in its sight, and the answer of the
        // Sulgogars, the strongest faction it sees, tests each Resistance unit
        // against hundreds of them. The think stops forking once
        // MaxLookaheadRangeTests is spent in the picture, says so, and still
        // plays the best order scored before the cut.
        const WorldState circled = CircledWorld();
        std::uint64_t picture = 0;
        const WorldState circledSeen = VisibleWorld(circled, spec.owner, range, &picture);
        Require(circledSeen.units.size() == circled.units.size() &&
                StrongestFoe(circledSeen, spec.owner) == 2, "the bot sees the whole circle");
        DecideReport cut;
        ForkRng rng3(11);
        const BotOrder cutOrder = Decide(circled, spec, rng3, World::Fork::ForkId{1}, &cut);
        Require(cutOrder.kind != OrderKind::None, "a cut think still decides");
        Require(cut.lookaheadCut && cut.rangeTests - picture > MaxLookaheadRangeTests,
                "a lookahead past its budget is cut and reported");
        Require(cut.forksCreated > 0 && cut.forksCreated < 8 && !cut.greedyFallback,
                "the candidates scored before the cut count");
        // At most the picture's check, the budget and the two fog checks of
        // the candidate that crossed it, each at most enemies x own units.
        const std::uint64_t perCheck = std::uint64_t(World::Fork::MaxWorldUnits / 2) *
                                       (World::Fork::MaxWorldUnits / 2);
        Require(picture <= perCheck && cut.rangeTests - picture <= MaxLookaheadRangeTests + 2 * perCheck &&
                cut.rangeTests <= MaxLookaheadRangeTests + 2 * perCheck, "the cut bounds the think");
        DecideReport again;
        ForkRng rng4(11);
        const BotOrder repeat = Decide(circled, spec, rng4, World::Fork::ForkId{1}, &again);
        Require(repeat.kind == cutOrder.kind && repeat.gid == cutOrder.gid &&
                repeat.targetGid == cutOrder.targetGid && again.rangeTests == cut.rangeTests,
                "a cut think is deterministic");
    }

    // What the bot cannot see never reaches its order. For random worlds, a
    // second world that differs only in what Resistance cannot see
    // (HideChanges) gives the same picture and the same order and report at
    // every difficulty: with the real budget and with budgets small enough to
    // cut the lookahead, so units out of sight cannot move the cut either.
    // Each world decides as its own picture does, and the picture yields the
    // same candidates. Cadet still plays the best candidate of the world.
    // The checks are not vacuous: the hidden changes reach forks of the
    // captured worlds, which the lookahead used to step, and changing what
    // the bot sees (SeenChanges) changes its order.
    {
        const std::uint64_t budgets[] = {MaxLookaheadRangeTests, 0, 3, 12};
        const int worlds = 1500;
        int touched = 0, felt = 0, cuts = 0, changed[3] = {};
        ForkRng rng(20261109);
        for (int world = 0; world < worlds; ++world)
        {
            const WorldState s = FogWorld(rng);
            for (int d = 0; d < 3; ++d)
            {
                BotSpec spec1;
                spec1.owner = 1;
                spec1.difficulty = static_cast<Difficulty>(d);
                spec1.valid = true;
                const float range = ParamsFor(spec1.difficulty).sensorRange;
                WorldState v;
                touched += HideChanges(s, range, rng, v) > 0 ? 1 : 0;
                const WorldState seen = VisibleWorld(s, 1, range);
                Require(EncodeState(VisibleWorld(v, 1, range)) == EncodeState(seen),
                        "a world changed out of sight gives the same picture");
                ForkRng c1(777), c2(777);
                std::uint64_t worldTests = 0;
                const std::vector<BotOrder> fromWorld = CandidateOrders(s, spec1, c1, &worldTests);
                const std::vector<BotOrder> fromPicture = CandidateOrders(seen, spec1, c2);
                bool sameCandidates = fromWorld.size() == fromPicture.size();
                for (std::size_t i = 0; sameCandidates && i < fromWorld.size(); ++i)
                    sameCandidates = SameOrder(fromWorld[i], fromPicture[i]);
                Require(sameCandidates && c1.State() == c2.State(), "the picture keeps every candidate");
                BotOrder decided;
                for (std::uint64_t budget : budgets)
                {
                    ForkRng r1(777), r2(777), r3(777);
                    DecideReport a, b, p;
                    const BotOrder x = Decide(s, spec1, r1, World::Fork::ForkId{1}, &a, budget);
                    const BotOrder y = Decide(v, spec1, r2, World::Fork::ForkId{1}, &b, budget);
                    const BotOrder z = Decide(seen, spec1, r3, World::Fork::ForkId{1}, &p, budget);
                    const bool same = SameOrder(x, y) && SameOrder(x, z) && r1.State() == r2.State() &&
                                      a.forksCreated == b.forksCreated && a.forkFailures == b.forkFailures &&
                                      a.greedyFallback == b.greedyFallback && a.lookaheadCut == b.lookaheadCut &&
                                      a.forksCreated == p.forksCreated && a.lookaheadCut == p.lookaheadCut;
                    if (!same)
                    {
                        std::cerr << "world " << world << ", difficulty " << d << ", budget " << budget << ": ";
                        Require(false, "what the bot cannot see never changes its order");
                    }
                    cuts += a.lookaheadCut ? 1 : 0;
                    if (budget == MaxLookaheadRangeTests)
                        decided = x;
                    if (d == 0)
                        Require(a.forksCreated == 0 && a.rangeTests == worldTests &&
                                SameOrder(x, fromWorld.empty() ? BotOrder() : fromWorld[0]),
                                "Cadet plays the best candidate of the world");
                }
                if (d == 1 && ForksOfTheWorldDiffer(s, v, spec1))
                    ++felt;
                WorldState w;
                SeenChanges(s, range, rng, w);
                ForkRng r4(777);
                if (!SameOrder(decided, Decide(w, spec1, r4, World::Fork::ForkId{1})))
                    ++changed[d];
            }
        }
        Require(touched > 3 * worlds / 2, "most worlds change units out of sight");
        Require(felt > worlds / 10, "forks of the captured world feel the hidden changes");
        Require(cuts > worlds, "small budgets cut the lookahead");
        for (int d = 0; d < 3; ++d)
            Require(changed[d] > worlds / 10, "what the bot sees changes its order");
    }

    // The faction that answers a Veteran candidate is the strongest one the
    // bot sees. A Mykonian unit out of sight with the lowest gid and the most
    // energy used to answer; now the visible Ghorkov unit does, then on equal
    // energy the visible Sulgogars (lower owner id), and nobody's units never.
    {
        WorldState s = MakeState(true);
        UnitState hidden;
        hidden.gid = 0xF000;
        hidden.owner = 3;
        hidden.energy = 400;
        hidden.x = 4000.0f;
        hidden.y = 3000.0f;
        UnitState weak;
        weak.gid = 0x10002;
        weak.owner = 2;
        weak.energy = 30;
        weak.x = 300.0f;
        weak.y = 200.0f;
        s.units.insert(s.units.begin(), hidden);
        s.units.push_back(weak);
        s.nextGid = 0x10003;
        const float range = ParamsFor(Difficulty::Veteran).sensorRange;
        REQUIRE(StrongestFoe(s, 1) == 3);
        REQUIRE(StrongestFoe(VisibleWorld(s, 1, range), 1) == 6);
        s.units.back().energy = 80;
        REQUIRE(StrongestFoe(VisibleWorld(s, 1, range), 1) == 2);
        WorldState nobody = MakeState(true);
        nobody.units[1].owner = 0;
        REQUIRE(StrongestFoe(nobody, 1) == 0);

        // In Decide: a weak Sulgogar unit next to the bot could answer every
        // candidate with an attack, but the strong Ghorkov unit it sees at
        // 2100 is the one that answers, and it sees nothing it could order
        // (both other units are beyond its 2000 as a Normal bot, and it owns
        // no sector the bot sees). So the candidates are judged after their
        // own step.
        WorldState two = MakeState(true);
        two.units[1].owner = 2;
        two.units[1].energy = 50;
        two.units[1].x = 100.0f;
        two.units[1].y = 250.0f;
        UnitState strong;
        strong.gid = 0x10002;
        strong.owner = 6;
        strong.energy = 400;
        strong.x = 100.0f + 2100.0f;
        strong.y = 100.0f;
        two.units.push_back(strong);
        two.nextGid = 0x10003;
        two.sectors[1].owner = 1;
        two.sectors[11].owner = 0;
        DecideReport answered, alone;
        ForkRng ra(5), rb(5);
        Decide(two, spec, ra, World::Fork::ForkId{1}, &answered);
        REQUIRE(StrongestFoe(VisibleWorld(two, 1, range), 1) == 6);
        Require(answered.forksCreated == 4 && !answered.greedyFallback,
                "the strongest faction in sight answers, not the first by gid");
        two.units.pop_back();
        Decide(two, spec, rb, World::Fork::ForkId{1}, &alone);
        Require(alone.forksCreated == 8, "without it, the Sulgogars answer every candidate");
    }

    // A Veteran with no enemy in sight still looks ahead at every candidate.
    // It used to judge only the first one unless some enemy unit, seen or
    // not, could answer. Here the best-scored candidate builds (Resistance
    // scores that 30, the moves 12.5 and 15, apart even under Cadet noise),
    // but the unit stands 30 units from the centre of the empty sector it is
    // in, so moving there takes the sector within the step: a better
    // position than a build that only spends sector energy.
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
                "a Veteran with no enemy in sight takes it too");
        // A faction out of sight changes nothing, even one that can answer:
        // Ghorkov holds a rich sector in the far corner with a unit in it, so
        // it would defend or build there. The Veteran used to look ahead only
        // when some faction, seen or not, could answer, and built here.
        const float vetRange = ParamsFor(Difficulty::Veteran).sensorRange;
        WorldState answerer = s;
        answerer.sectors[11].owner = 6;
        answerer.sectors[11].energyPower = 200;
        UnitState far;
        far.gid = 0x10001;
        far.owner = 6;
        far.energy = 100;
        far.x = 3.5f * World::CVSectorLength;
        far.y = 2.5f * World::CVSectorLength;
        answerer.units.push_back(far);
        answerer.nextGid = 0x10002;
        REQUIRE(!UnitVisibleTo(answerer, far, 1, vetRange) && !(answerer.sectors[11].viewMask & 2u));
        ForkRng r3(1);
        Require(SameOrder(Decide(answerer, spec, r3, World::Fork::ForkId{1}), veteran),
                "a faction out of sight that can answer changes nothing");
        // Nor does a strong unit just beyond sensor range that the move would
        // bring into range (2175 from the unit, 2145 from the centre). Forks
        // of the captured world counted its energy against the move, and the
        // Veteran went back to its own sector instead.
        WorldState edge = s;
        UnitState strong;
        strong.gid = 0x10001;
        strong.owner = 6;
        strong.energy = 400;
        strong.x = own.x + 2175.0f;
        strong.y = own.y;
        edge.units.push_back(strong);
        edge.nextGid = 0x10002;
        REQUIRE(!UnitVisibleTo(edge, strong, 1, vetRange) &&
                World::Fork::InSensorRange(centreX, centreY, strong.x, strong.y, vetRange * vetRange));
        ForkRng r4(1);
        Require(SameOrder(Decide(edge, spec, r4, World::Fork::ForkId{1}), veteran),
                "a unit out of sight that the move would bring into range changes nothing");
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
