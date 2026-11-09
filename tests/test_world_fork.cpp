#include "../src/world/fork.h"
#include "../src/agent/fork_protocol.h"
#include "require.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace World::Fork;

// FNV-1a 64 over bytes: what HashState must equal over EncodeState.
static std::uint64_t Fnv1a(const std::vector<std::uint8_t> &bytes)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint8_t b : bytes)
    {
        hash ^= b;
        hash *= 1099511628211ull;
    }
    return hash;
}

// A coordinate for the fog checks on an axis of `cells` sectors: anywhere on
// the map or a sector beyond it, on a cell edge, near one of four spots, or
// one of the values a float cannot place on the map.
static float FogCoordinate(ForkRng &rng, int cells, const float spots[4])
{
    const float len = World::CVSectorLength;
    switch (rng.Below(10))
    {
    case 0:
        return static_cast<float>(rng.Below(2 * (cells + 2) + 1)) * len / 2.0f - len;
    case 1:
    case 2:
    case 3:
        return spots[rng.Below(4)] + static_cast<float>(rng.Below(4001)) - 2000.0f;
    case 4:
    {
        static const float odd[] = {0.0f, -0.0f, 1e30f, -1e30f, 3e38f, -3e38f,
                                    HUGE_VALF, -HUGE_VALF, std::nanf(""), 1e-30f};
        return rng.Below(3) == 0 ? odd[rng.Below(10)] : spots[rng.Below(4)];
    }
    default:
        return (static_cast<float>(rng.Below(100000)) / 100000.0f * (cells + 2) - 1.0f) * len;
    }
}

static WorldState MakeState()
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
    s.sectors[1].owner = 1;
    s.sectors[1].viewMask = (1u << 1) | (1u << 6);
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
    foe.energy = 100;
    foe.x = 250; foe.y = 100;
    UnitState far;
    far.gid = 0x10002;
    far.protoId = 11;
    far.owner = 6;
    far.energy = 100;
    far.x = 4000; far.y = 3000;
    s.units = {own, foe, far};
    s.nextGid = 0x10003;
    return s;
}

static ForkConfig Cfg(std::uint8_t owner = 1)
{
    ForkConfig c;
    c.controllerOwner = owner;
    return c;
}

int main()
{
    // RNG determinism.
    ForkRng a(42), b(42);
    for (int i = 0; i < 100; ++i)
        REQUIRE(a.Next() == b.Next());
    ForkRng c(7);
    for (int i = 0; i < 100; ++i)
        REQUIRE(c.Below(5) < 5);
    REQUIRE(c.Below(0) == 0);

    // Snapshot round-trip + strict decode.
    const WorldState state = MakeState();
    const std::vector<std::uint8_t> bytes = EncodeState(state);
    WorldState back;
    Require(DecodeState(bytes.data(), bytes.size(), 1024, back), "snapshot round-trips");
    REQUIRE(HashState(back) == HashState(state));
    // The hash streams the encoding instead of building it, and the size
    // check of every fork computes the size instead of encoding the world.
    REQUIRE(HashState(state) == Fnv1a(bytes));
    REQUIRE(EncodedStateSize(state) == bytes.size());
    REQUIRE(EncodedStateSize(WorldState()) == EncodeState(WorldState()).size());
    REQUIRE(HashState(WorldState()) == Fnv1a(EncodeState(WorldState())));
    Require(!DecodeState(bytes.data(), bytes.size() - 1, 1024, back), "truncated snapshot rejected");
    Require(!DecodeState(nullptr, 0, 1024, back), "null snapshot rejected");
    Require(!ValidateState(back, 2), "unit cap enforced by validation");

    // Create validation.
    Require(!Fork::Create(state, Cfg(), ForkId{0}), "zero fork id rejected");
    ForkConfig badDt = Cfg();
    badDt.fixedDtMs = 0;
    Require(!Fork::Create(state, badDt, ForkId{1}), "zero fixed dt rejected");

    // Determinism: same seed + actions => identical hash.
    auto f1 = Fork::Create(state, Cfg(), ForkId{1});
    auto f2 = Fork::Create(state, Cfg(), ForkId{2});
    Require(f1 && f2, "forks created");
    ForkActionBatch move;
    ForkAction m;
    m.kind = ActionKind::Move;
    m.gid = 0x10000;
    m.x = 1300; m.y = 100;
    move.push_back(m);
    Require(f1->Step(move, 10) && f2->Step(move, 10), "move steps");
    REQUIRE(f1->Capture().hash == f2->Capture().hash);
    REQUIRE(f1->Capture().hash != HashState(state));

    // ForkFrom isolation.
    auto child = f1->ForkFrom(ForkId{3});
    Require(child != nullptr, "child fork");
    const std::uint64_t parentHash = f1->Capture().hash;
    ForkActionBatch hold;
    hold.push_back(ForkAction());
    Require(child->Step(hold, 25), "child steps");
    REQUIRE(f1->Capture().hash == parentHash);

    // Illegal batch is atomic: enemy move rejected, nothing applied.
    ForkActionBatch illegal;
    ForkAction em;
    em.kind = ActionKind::Move;
    em.gid = 0x10001; // enemy unit, controller is owner 1
    em.x = 0; em.y = 0;
    illegal.push_back(m);
    illegal.push_back(em);
    const std::uint64_t beforeIllegal = f1->Capture().hash;
    Require(!f1->Step(illegal, 1), "ordering an enemy unit is rejected");
    REQUIRE(f1->Capture().hash == beforeIllegal);
    REQUIRE(f1->ActionLog().size() == 1);

    // Tick limit.
    Require(!f1->Step(hold, Cfg().maxTicksPerStep + 1), "tick budget enforced");
    Require(!f1->Step(hold, 0), "zero ticks rejected");

    // Attack kills and diff reports it.
    auto fa = Fork::Create(state, Cfg(), ForkId{4});
    auto fb = Fork::Create(state, Cfg(), ForkId{5});
    ForkActionBatch atk;
    ForkAction at;
    at.kind = ActionKind::Attack;
    at.gid = 0x10000;
    at.targetGid = 0x10001;
    atk.push_back(at);
    Require(fa->Step(atk, 200), "attack steps");
    Require(fa->State().FindUnit(0x10001) == nullptr, "attack destroys target in range");
    const ForkDiff diff = fb->Diff(*fa);
    bool sawDestroyed = false;
    for (const UnitDelta &d : diff.units)
        if (d.gid == 0x10001 && d.kind == UnitDelta::Destroyed)
            sawDestroyed = true;
    Require(sawDestroyed, "diff reports destroyed unit");

    // Create charges the sector and spawns a gid-sorted unit.
    auto fc = Fork::Create(state, Cfg(), ForkId{6});
    ForkActionBatch cr;
    ForkAction ca;
    ca.kind = ActionKind::Create;
    ca.x = 100; ca.y = 100;
    ca.protoId = 42;
    ca.cost = 50;
    cr.push_back(ca);
    Require(fc->Step(cr, 1), "create steps");
    REQUIRE(fc->State().units.size() == 4);
    REQUIRE(fc->State().SectorAt(0, 0)->energyPower == 150);
    REQUIRE(fc->State().FindUnit(0x10003) != nullptr);
    ForkActionBatch poor = cr;
    poor[0].cost = 100000;
    Require(!fc->Step(poor, 1), "unaffordable create rejected");

    // Fog-filtered observation: near foe visible, far foe hidden.
    const Observation obs = fc->Observe(1);
    bool sawNear = false, sawFar = false, sawOwn = false;
    for (const ObservedUnit &u : obs.units)
    {
        if (u.gid == 0x10001) sawNear = true;
        if (u.gid == 0x10002) sawFar = true;
        if (u.gid == 0x10000) sawOwn = true;
    }
    Require(sawOwn && sawNear && !sawFar, "observation is fog-filtered");
    REQUIRE(obs.ownEnergyTotal >= 100);

    // Restore returns to the captured hash.
    auto fr = Fork::Create(state, Cfg(), ForkId{7});
    const WorldState snap = fr->State();
    Require(fr->Step(move, 10), "pre-restore step");
    Require(fr->Restore(snap), "restore accepted");
    REQUIRE(fr->Capture().hash == HashState(state));
    REQUIRE(fr->ActionLog().empty());

    // Commit-by-replay: fresh destination accepts, stale destination rejects.
    auto src = Fork::Create(state, Cfg(), ForkId{8});
    Require(src->Step(move, 5), "source steps");
    auto dst = Fork::Create(state, Cfg(), ForkId{9});
    Require(src->TryCommit(*dst), "commit to fresh destination");
    REQUIRE(dst->State().FindUnit(0x10000)->hasTarget);
    auto stale = Fork::Create(state, Cfg(), ForkId{10});
    ForkActionBatch other;
    ForkAction om;
    om.kind = ActionKind::Move;
    om.gid = 0x10000;
    om.x = 500; om.y = 500;
    other.push_back(om);
    Require(stale->Step(other, 5), "stale destination advanced differently");
    Require(!src->TryCommit(*stale), "stale commit rejected by default");
    Require(src->TryCommit(*stale, MergePolicy::ReplayAnyway), "forced replay applies");

    // Registry budget + discard frees the slot.
    ForkRegistry reg(2);
    auto r1 = Fork::Create(state, Cfg(), reg.NextId());
    reg.Track(r1.get());
    auto r2 = Fork::Create(state, Cfg(), reg.NextId());
    reg.Track(r2.get());
    REQUIRE(reg.NextId().v == 0);
    r1->Discard();
    Require(!r1->Step(hold, 1), "discarded fork cannot step");
    REQUIRE(reg.NextId().v != 0);

    // Agent fork protocol.
    using namespace Agent;
    const std::string sess = "s1";
    auto mk = [&](const std::string &ops) {
        return ParseFork("1 " + sess + " 7 9999999999 " + ops + "\n", sess);
    };
    Require(mk("fork_create 6").valid, "fork_create parses");
    REQUIRE(mk("fork_create 6").command.op == ForkOp::Create);
    auto step = mk("fork_step 3 1 65536 0 100 200 0 0 10");
    Require(step.valid && step.command.ticks == 10, "fork_step parses");
    int kind = -1, proto = 0, cost = 0;
    std::uint32_t gid = 0, target = 0;
    float fx = 0, fy = 0;
    Require(ToForkAction(step.command, kind, gid, target, fx, fy, proto, cost) && kind == 1,
            "fork_step maps to a move action");
    Require(mk("fork_observe 3 6").valid, "fork_observe parses");
    Require(mk("fork_commit 3 4").valid, "fork_commit parses");
    Require(mk("fork_discard 3").valid, "fork_discard parses");
    Require(!mk("fork_step 3 9 1 2 3 4 5 6 7").valid, "bad action kind rejected");
    Require(!ParseFork("1 other 7 9999999999 fork_create 6\n", sess).valid,
            "foreign session rejected");
    Require(!ParseFork("1 s1 7 9999999999 fork_create 6", sess).valid,
            "missing newline rejected");
    Require(!mk("fork_create 999").valid, "owner range enforced");

    // Engine positions are (x, altitude, z) with z negative across rows; the
    // fork plane must follow World::PositionToSectorID (x, -z).
    {
        WorldState s = MakeState();
        float px = 0, py = 0, alt = 0;
        EnginePositionToPlane(1800.0, -300.0, -1800.0, px, py, alt);
        Require(px == 1800.0f && py == 1800.0f && alt == -300.0f, "engine to plane mapping");
        Require(SectorIndexOf(s, px, py) == 1 * s.mapW + 1, "engine position lands in sector (1,1)");

        // Same column and altitude, two rows apart: invisible to owner 1.
        // Reading engine y as the plane axis would place both units 50
        // units apart and leak the enemy through the fog.
        UnitState own;
        own.gid = 0x20000; own.owner = 1; own.energy = 100;
        EnginePositionToPlane(600.0, -300.0, -600.0, own.x, own.y, own.z);
        UnitState foe;
        foe.gid = 0x20001; foe.owner = 6; foe.energy = 100;
        EnginePositionToPlane(650.0, -250.0, -3000.0, foe.x, foe.y, foe.z);
        s.units = {own, foe};
        s.nextGid = 0x20002;
        for (SectorState &sec : s.sectors)
            sec.viewMask = 0;
        Require(!UnitVisibleTo(s, foe, 1, 1800.0f), "distant row enemy stays fogged");
        UnitState near = foe;
        EnginePositionToPlane(650.0, -250.0, -900.0, near.x, near.y, near.z);
        Require(UnitVisibleTo(s, near, 1, 1800.0f), "nearby enemy is within sensor range");
    }

    // One whole-world unit cap for the engine capture and every ForkConfig.
    // A six-player match passes the old fork cap of 1024 long before the
    // capture cap, and every fork then failed.
    {
        REQUIRE(ForkConfig().maxUnits == MaxWorldUnits);
        REQUIRE(MaxWorldUnits == 4096);
        WorldState big = MakeState();
        big.units.clear();
        for (std::uint32_t i = 0; i < 1500; ++i)
        {
            UnitState u;
            u.gid = 0x10000 + i;
            u.protoId = 10;
            u.owner = static_cast<std::uint8_t>(1 + i % 6);
            u.energy = 100;
            u.x = 100.0f + static_cast<float>(i % 40) * 110.0f;
            u.y = 100.0f + static_cast<float>(i / 40) * 90.0f;
            big.units.push_back(u);
        }
        big.nextGid = 0x10000 + 1500;
        Require(ValidateState(big, MaxWorldUnits), "1500 units are a valid world");
        Require(Fork::Create(big, Cfg(), ForkId{20}) != nullptr, "a 1500-unit world forks");
        while (big.units.size() <= MaxWorldUnits)
        {
            UnitState u = big.units.back();
            u.gid = big.nextGid++;
            big.units.push_back(u);
        }
        Require(!ValidateState(big, MaxWorldUnits), "the cap is enforced");
        Require(!Fork::Create(big, Cfg(), ForkId{21}), "a world above the cap does not fork");
        REQUIRE(EncodedStateSize(big) == EncodeState(big).size());
        REQUIRE(HashState(big) == Fnv1a(EncodeState(big)));
    }

    // Sector lookup is defined for any float: positions that are not finite
    // or lie beyond int are off the map.
    {
        const WorldState s = MakeState();
        for (float v : {std::nanf(""), HUGE_VALF, -HUGE_VALF, 3e38f, -3e38f, 1e12f})
        {
            REQUIRE(SectorIndexOf(s, v, 100.0f) == -1);
            REQUIRE(SectorIndexOf(s, 100.0f, v) == -1);
        }
        REQUIRE(SectorIndexOf(s, 0.0f, 0.0f) == 0);
        REQUIRE(SectorIndexOf(s, 4 * World::CVSectorLength - 1.0f, 3 * World::CVSectorLength - 1.0f) == 11);
        REQUIRE(SectorIndexOf(s, -0.5f, 100.0f) == -1);
    }

    // SensorView answers exactly what UnitVisibleTo answers, for every unit
    // and owner: on and off the map, on cell edges, at the edge of the range,
    // in clusters, with no map at all, for odd ranges and for positions that
    // are not finite.
    {
        const float ranges[] = {1800.0f, 2000.0f, 2160.0f, 0.0f, -1800.0f, 1e-25f,
                                50.0f, 7000.0f, 1e30f, HUGE_VALF, std::nanf("")};
        ForkRng rng(2024);
        std::size_t checked = 0;
        for (int world = 0; world < 40; ++world)
        {
            WorldState s;
            s.mapW = static_cast<int>(rng.Below(9));
            s.mapH = s.mapW ? static_cast<int>(1 + rng.Below(8)) : 0;
            s.sectors.resize(static_cast<std::size_t>(s.mapW) * s.mapH);
            for (std::size_t i = 0; i < s.sectors.size(); ++i)
            {
                s.sectors[i].id = static_cast<std::int32_t>(i);
                s.sectors[i].viewMask = rng.Below(3) == 0 ? (1u << rng.Below(7)) : 0u;
            }
            float spotsX[4], spotsY[4];
            for (int k = 0; k < 4; ++k)
            {
                spotsX[k] = static_cast<float>(rng.Below(1 + 1200 * (s.mapW + 1)));
                spotsY[k] = static_cast<float>(rng.Below(1 + 1200 * (s.mapH + 1)));
            }
            const std::uint32_t count = 1 + rng.Below(150);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                UnitState u;
                u.gid = 0x10000 + i;
                u.owner = static_cast<std::uint8_t>(rng.Below(7));
                u.x = FogCoordinate(rng, s.mapW, spotsX);
                u.y = FogCoordinate(rng, s.mapH, spotsY);
                s.units.push_back(u);
            }
            // Units at the edge of the range of an own unit: straight, on a
            // 3-4-5 diagonal, one float step beyond, and one step inside.
            const UnitState anchor = s.units[0];
            if (std::isfinite(anchor.x) && std::isfinite(anchor.y))
                for (float r : {1800.0f, 2000.0f, 2160.0f})
                {
                    const float xs[] = {anchor.x + r, anchor.x, anchor.x + 0.6f * r,
                                        std::nextafter(anchor.x + r, HUGE_VALF),
                                        std::nextafter(anchor.x + r, -HUGE_VALF)};
                    const float ys[] = {anchor.y, anchor.y - r, anchor.y + 0.8f * r,
                                        anchor.y, anchor.y};
                    for (int k = 0; k < 5; ++k)
                    {
                        UnitState u;
                        u.gid = 0x10000 + static_cast<std::uint32_t>(s.units.size());
                        u.owner = static_cast<std::uint8_t>((anchor.owner + 1) % 7);
                        u.x = xs[k];
                        u.y = ys[k];
                        s.units.push_back(u);
                    }
                }
            for (float range : ranges)
                for (std::uint8_t owner = 0; owner < 7; ++owner)
                {
                    const SensorView view(s, owner, range);
                    for (const UnitState &u : s.units)
                    {
                        ++checked;
                        if (view.Sees(u) != UnitVisibleTo(s, u, owner, range))
                        {
                            std::cerr << "world " << world << " owner " << int(owner) << " range "
                                      << range << " unit at " << u.x << "," << u.y << ": ";
                            Require(false, "SensorView agrees with UnitVisibleTo");
                        }
                    }
                }
        }
        REQUIRE(checked > 100000);

        // An owner whose only unit has no finite position: with an infinite
        // range, UnitVisibleTo still lets that unit see a finite one, since
        // inf <= inf.
        WorldState s = MakeState();
        UnitState lost;
        lost.gid = 0x20000;
        lost.owner = 4;
        lost.x = HUGE_VALF;
        s.units.push_back(lost);
        Require(UnitVisibleTo(s, s.units[0], 4, HUGE_VALF), "a unit at infinity sees with an infinite range");
        for (float range : {HUGE_VALF, 1800.0f, std::nanf("")})
        {
            const SensorView view(s, 4, range);
            for (const UnitState &u : s.units)
                Require(view.Sees(u) == UnitVisibleTo(s, u, 4, range),
                        "SensorView agrees for an owner without a finite position");
        }
    }

    // VisibleWorld: the world as one faction sees it. Own units stay whole,
    // the units it sees keep only what can be seen of them, the sectors it
    // sees lose the other owners' view bits, every other sector is blank and
    // nothing it cannot see survives: not even the gid counter.
    {
        const WorldState s = MakeState();
        std::uint64_t tests = 0;
        const WorldState seen = VisibleWorld(s, 1, 1800.0f, &tests);
        REQUIRE(seen.units.size() == 2);
        REQUIRE(seen.units[0].gid == 0x10000 && seen.units[1].gid == 0x10001);
        Require(seen.FindUnit(0x10002) == nullptr, "a unit out of sight is not in the picture");
        REQUIRE(seen.nextGid == 0x10002 && s.nextGid == 0x10003);
        REQUIRE(seen.sectors.size() == s.sectors.size());
        REQUIRE(seen.sectors[0].owner == 1 && seen.sectors[0].energyPower == 200);
        Require(seen.sectors[1].viewMask == (1u << 1), "the other owners' view bits are cleared");
        const SectorState &blank = seen.sectors[11];
        Require(blank.id == 11 && blank.owner == 0 && blank.energyPower == 0 && blank.purpose == 0 &&
                blank.typeId == 0 && blank.viewMask == 0 && !blank.border,
                "a sector out of sight is blank");
        REQUIRE(seen.mapW == s.mapW && seen.mapH == s.mapH && seen.timeStampMs == s.timeStampMs &&
                seen.framesElapsed == s.framesElapsed && seen.rngState == s.rngState);
        // Orders, cooldown and parent of a unit in sight cannot be seen; the
        // owner's own units keep theirs.
        WorldState busy = s;
        for (UnitState &u : busy.units)
        {
            u.parentGid = 0x10002;
            u.hasTarget = true;
            u.targetX = 500.0f;
            u.targetY = 700.0f;
            u.attackGid = 0x10002;
            u.cooldownMs = 300;
            u.z = 42.0f;
        }
        const WorldState busySeen = VisibleWorld(busy, 1, 1800.0f);
        const UnitState &own = busySeen.units[0];
        Require(own.parentGid == 0x10002 && own.hasTarget && own.targetX == 500.0f &&
                own.attackGid == 0x10002 && own.cooldownMs == 300, "own units stay whole");
        const UnitState &foe = busySeen.units[1];
        Require(foe.gid == 0x10001 && foe.protoId == 11 && foe.owner == 6 && foe.energy == 100 &&
                foe.x == 250.0f && foe.y == 100.0f && foe.z == 42.0f,
                "what can be seen of a unit in sight stays");
        Require(foe.parentGid == 0 && !foe.hasTarget && foe.targetX == 0.0f && foe.targetY == 0.0f &&
                foe.attackGid == 0 && foe.cooldownMs == 0, "what cannot be seen of it is cleared");
        // The border flag stays on a blank sector: the map size fixes it.
        WorldState edged = s;
        edged.sectors[11].border = true;
        Require(VisibleWorld(edged, 1, 1800.0f).sectors[11].border, "a blank sector keeps its border flag");
        // A sector the owner owns but has no view bit on is its own all the same.
        WorldState unviewed = s;
        unviewed.sectors[0].viewMask = 0;
        Require(VisibleWorld(unviewed, 1, 1800.0f).sectors[0].owner == 1, "own sectors stay in the picture");
    }

    // Random worlds, every owner, odd ranges and odd positions: the picture
    // keeps exactly what UnitVisibleTo reports and the visible sectors, costs
    // one counted SensorView pass, observes as the world does, and is its own
    // picture.
    {
        const float ranges[] = {1800.0f, 2000.0f, 2160.0f, 0.0f, 50.0f, 7000.0f, HUGE_VALF,
                                std::nanf("")};
        ForkRng rng(77);
        std::size_t pictures = 0, hiddenUnits = 0, blankSectors = 0;
        for (int world = 0; world < 40; ++world)
        {
            WorldState s;
            s.mapW = static_cast<int>(rng.Below(9));
            s.mapH = s.mapW ? static_cast<int>(1 + rng.Below(8)) : 0;
            s.sectors.resize(static_cast<std::size_t>(s.mapW) * s.mapH);
            for (std::size_t i = 0; i < s.sectors.size(); ++i)
            {
                SectorState &sec = s.sectors[i];
                sec.id = static_cast<std::int32_t>(i);
                sec.owner = static_cast<std::uint8_t>(rng.Below(7));
                sec.typeId = static_cast<std::int32_t>(rng.Below(60));
                sec.purpose = static_cast<std::uint8_t>(rng.Below(4));
                sec.energyPower = static_cast<std::int32_t>(rng.Below(400));
                sec.viewMask = rng.Below(128);
                sec.border = rng.Below(5) == 0;
            }
            s.timeStampMs = static_cast<std::int32_t>(rng.Below(100000));
            s.framesElapsed = rng.Below(5000);
            s.rngState = rng.Next();
            float spotsX[4], spotsY[4];
            for (int k = 0; k < 4; ++k)
            {
                spotsX[k] = static_cast<float>(rng.Below(1 + 1200 * (s.mapW + 1)));
                spotsY[k] = static_cast<float>(rng.Below(1 + 1200 * (s.mapH + 1)));
            }
            const std::uint32_t count = 1 + rng.Below(150);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                UnitState u;
                u.gid = 0x10000 + i * (1 + rng.Below(3));
                if (!s.units.empty() && u.gid <= s.units.back().gid)
                    u.gid = s.units.back().gid + 1;
                u.parentGid = rng.Below(2) ? 0 : 0x10000 + rng.Below(count);
                u.protoId = static_cast<std::int32_t>(rng.Below(80));
                u.owner = static_cast<std::uint8_t>(rng.Below(7));
                u.energy = static_cast<std::int32_t>(rng.Below(500));
                u.x = FogCoordinate(rng, s.mapW, spotsX);
                u.y = FogCoordinate(rng, s.mapH, spotsY);
                u.z = static_cast<float>(rng.Below(300));
                u.hasTarget = rng.Below(2) != 0;
                u.targetX = static_cast<float>(rng.Below(9000));
                u.targetY = static_cast<float>(rng.Below(9000));
                u.attackGid = rng.Below(2) ? 0 : 0x10000 + rng.Below(count);
                u.cooldownMs = static_cast<std::int32_t>(rng.Below(500));
                s.units.push_back(u);
            }
            s.nextGid = s.units.back().gid + 1 + rng.Below(50);
            REQUIRE(ValidateState(s, MaxWorldUnits));
            for (float range : ranges)
                for (std::uint8_t owner = 1; owner < 7; ++owner)
                {
                    ++pictures;
                    std::uint64_t tests = 0;
                    const WorldState seen = VisibleWorld(s, owner, range, &tests);
                    const SensorView view(s, owner, range);
                    std::uint32_t nextGid = WorldState().nextGid;
                    std::size_t k = 0;
                    for (const UnitState &u : s.units)
                    {
                        if (!view.Sees(u))
                        {
                            REQUIRE(!UnitVisibleTo(s, u, owner, range));
                            ++hiddenUnits;
                            continue;
                        }
                        REQUIRE(k < seen.units.size());
                        const UnitState &p = seen.units[k++];
                        REQUIRE(p.gid == u.gid && p.protoId == u.protoId && p.owner == u.owner &&
                                p.energy == u.energy);
                        REQUIRE(std::memcmp(&p.x, &u.x, sizeof u.x) == 0 &&
                                std::memcmp(&p.y, &u.y, sizeof u.y) == 0 && p.z == u.z);
                        if (u.owner == owner)
                            REQUIRE(p.parentGid == u.parentGid && p.hasTarget == u.hasTarget &&
                                    p.targetX == u.targetX && p.targetY == u.targetY &&
                                    p.attackGid == u.attackGid && p.cooldownMs == u.cooldownMs);
                        else
                            REQUIRE(p.parentGid == 0 && !p.hasTarget && p.targetX == 0.0f &&
                                    p.targetY == 0.0f && p.attackGid == 0 && p.cooldownMs == 0);
                        if (u.gid >= nextGid)
                            nextGid = u.gid + 1;
                    }
                    REQUIRE(k == seen.units.size());
                    Require(tests == view.RangeTests(), "the picture counts its fog check");
                    REQUIRE(seen.nextGid == nextGid);
                    REQUIRE(seen.sectors.size() == s.sectors.size());
                    for (std::size_t i = 0; i < s.sectors.size(); ++i)
                    {
                        const SectorState &a = s.sectors[i];
                        const SectorState &b = seen.sectors[i];
                        REQUIRE(b.id == a.id && b.border == a.border);
                        if (a.owner == owner || (a.viewMask & (1u << owner)))
                            REQUIRE(b.owner == a.owner && b.energyPower == a.energyPower &&
                                    b.purpose == a.purpose && b.typeId == a.typeId &&
                                    b.viewMask == (a.viewMask & (1u << owner)));
                        else
                        {
                            REQUIRE(b.owner == 0 && b.energyPower == 0 && b.purpose == 0 &&
                                    b.typeId == 0 && b.viewMask == 0);
                            ++blankSectors;
                        }
                    }
                    REQUIRE(ValidateState(seen, MaxWorldUnits));
                    REQUIRE(seen.mapW == s.mapW && seen.mapH == s.mapH &&
                            seen.timeStampMs == s.timeStampMs &&
                            seen.framesElapsed == s.framesElapsed && seen.rngState == s.rngState);
                    // Everything kept is seen in the picture as well, so the
                    // picture observes exactly as the world does.
                    const SensorView again(seen, owner, range);
                    for (const UnitState &u : seen.units)
                        Require(again.Sees(u), "every unit in the picture is in sight there");
                    const auto world = Fork::Create(s, Cfg(owner), ForkId{1});
                    const auto picture = Fork::Create(seen, Cfg(owner), ForkId{2});
                    Require(world && picture, "world and picture fork");
                    const Observation a = world->Observe(owner, range);
                    const Observation b = picture->Observe(owner, range);
                    REQUIRE(a.units.size() == b.units.size() && a.sectors.size() == b.sectors.size() &&
                            a.ownEnergyTotal == b.ownEnergyTotal);
                    for (std::size_t i = 0; i < a.units.size(); ++i)
                        REQUIRE(a.units[i].gid == b.units[i].gid && a.units[i].energy == b.units[i].energy &&
                                a.units[i].owner == b.units[i].owner && a.units[i].own == b.units[i].own);
                    for (std::size_t i = 0; i < a.sectors.size(); ++i)
                        REQUIRE(a.sectors[i].id == b.sectors[i].id && a.sectors[i].owner == b.sectors[i].owner &&
                                a.sectors[i].energyPower == b.sectors[i].energyPower &&
                                a.sectors[i].purpose == b.sectors[i].purpose);
                    Require(EncodeState(VisibleWorld(seen, owner, range)) == EncodeState(seen),
                            "the picture is its own picture");
                }
        }
        REQUIRE(pictures == 40 * 8 * 6);
        Require(hiddenUnits > 1000 && blankSectors > 1000, "the random worlds hide units and sectors");
    }

    // The cost of a fog pass at the cap. Asking UnitVisibleTo about every unit
    // scans the world per unit; the view tests only own units in cells whose
    // box is in range. Six owners, owner 1 massed 2300-2700 units west of all
    // the others: every enemy is in reach of the cells, none in sensor range.
    {
        WorldState s;
        s.mapW = 54;
        s.mapH = 32;
        s.sectors.resize(54 * 32);
        for (int i = 0; i < 54 * 32; ++i)
            s.sectors[i].id = i;
        const float len = World::CVSectorLength;
        for (std::uint32_t i = 0; i < MaxWorldUnits; ++i)
        {
            UnitState u;
            u.gid = 0x10000 + i;
            u.owner = static_cast<std::uint8_t>(1 + i % 6);
            u.x = 20.5f * len + static_cast<float>(i % 200) + (u.owner == 1 ? 0.0f : 2500.0f);
            u.y = 16.5f * len + static_cast<float>((i / 6) % 200);
            s.units.push_back(u);
        }
        const SensorView view(s, 1, 2160.0f);
        std::size_t seen = 0;
        for (const UnitState &u : s.units)
            seen += view.Sees(u) ? 1 : 0;
        REQUIRE(seen == (MaxWorldUnits + 5) / 6);  // its own units only
        Require(view.RangeTests() == 0, "a cluster out of range costs no range test");
        // Brought in range, each enemy costs its first test.
        for (UnitState &u : s.units)
            if (u.owner != 1)
                u.x -= 600.0f;
        const SensorView near(s, 1, 2160.0f);
        for (const UnitState &u : s.units)
            Require(near.Sees(u), "every enemy within range is seen");
        Require(near.RangeTests() <= s.units.size(), "one range test per enemy in range");
    }

    std::cout << "world fork, snapshot, registry, fog view and agent fork protocol tests passed\n";
    return 0;
}
