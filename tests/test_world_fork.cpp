#include "../src/world/fork.h"
#include "../src/agent/fork_protocol.h"
#include "require.h"
#include <cstdlib>
#include <iostream>

using namespace World::Fork;

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

    std::cout << "world fork, snapshot, registry and agent fork protocol tests passed\n";
    return 0;
}
