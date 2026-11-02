#include "../src/world/delta_wire.h"
#include <cassert>
#include <cstring>
using namespace World::Fork;

static WorldState MakeState(std::uint32_t gid2Energy, bool extraUnit, std::uint8_t sectorOwner)
{
    WorldState s;
    s.mapW = 2; s.mapH = 1;
    SectorState a; a.id = 0; a.owner = sectorOwner; a.energyPower = 100;
    SectorState b; b.id = 1; b.owner = 0; b.energyPower = 50;
    s.sectors = {a, b};
    UnitState u1; u1.gid = 1; u1.protoId = 10; u1.owner = 1; u1.energy = 90; u1.x = 10.5f; u1.y = -3.25f;
    UnitState u2; u2.gid = 2; u2.protoId = 11; u2.owner = 6; u2.energy = (std::int32_t)gid2Energy; u2.x = 100.0f; u2.y = 200.0f;
    s.units = {u1, u2};
    if (extraUnit)
    {
        UnitState u3; u3.gid = 3; u3.protoId = 12; u3.owner = 1; u3.energy = 7; u3.x = 1.0f; u3.y = 2.0f;
        s.units.push_back(u3);
    }
    return s;
}

static bool SameDiff(const ForkDiff &a, const ForkDiff &b)
{
    if (a.units.size() != b.units.size() || a.sectors.size() != b.sectors.size())
        return false;
    for (std::size_t i = 0; i < a.units.size(); ++i)
    {
        const UnitDelta &x = a.units[i], &y = b.units[i];
        if (x.gid != y.gid || x.kind != y.kind || x.protoId != y.protoId || x.owner != y.owner ||
            x.energyBefore != y.energyBefore || x.energyAfter != y.energyAfter ||
            std::memcmp(&x.x, &y.x, 4) != 0 || std::memcmp(&x.y, &y.y, 4) != 0)
            return false;
    }
    for (std::size_t i = 0; i < a.sectors.size(); ++i)
    {
        const SectorDelta &x = a.sectors[i], &y = b.sectors[i];
        if (x.id != y.id || x.ownerBefore != y.ownerBefore || x.ownerAfter != y.ownerAfter ||
            x.energyBefore != y.energyBefore || x.energyAfter != y.energyAfter)
            return false;
    }
    return true;
}

int main() {
    ForkConfig cfg;
    auto base = Fork::Create(MakeState(80, false, 1), cfg, ForkId{1});
    auto next = Fork::Create(MakeState(55, true, 6), cfg, ForkId{2});
    assert(base && next);
    const ForkDiff diff = base->Diff(*next);
    assert(!diff.Empty());

    // Calls are unconditional: assert() may be compiled out (NDEBUG), so no
    // side effects inside assert expressions in this suite.
    std::vector<std::uint8_t> bytes;
    const bool encoded = World::DeltaWire::EncodeDiff(diff, bytes);
    assert(encoded);
    ForkDiff back;
    const bool decoded = World::DeltaWire::DecodeDiff(bytes.data(), bytes.size(), back);
    assert(decoded);
    assert(SameDiff(diff, back));

    // Empty diff round-trips (heartbeat case).
    ForkDiff empty;
    assert(empty.Empty());
    const bool emptyEncoded = World::DeltaWire::EncodeDiff(empty, bytes);
    assert(emptyEncoded);
    const bool emptyDecoded = World::DeltaWire::DecodeDiff(bytes.data(), bytes.size(), back);
    assert(emptyDecoded && back.Empty());

    // Fail closed: truncations, trailing garbage, null, bad kind.
    const bool reencoded = World::DeltaWire::EncodeDiff(diff, bytes);
    assert(reencoded);
    for (std::size_t cut = 0; cut < bytes.size(); ++cut)
    {
        const bool truncatedOk = World::DeltaWire::DecodeDiff(bytes.data(), cut, back);
        assert(!truncatedOk);
    }
    std::vector<std::uint8_t> tail = bytes; tail.push_back(0);
    const bool tailOk = World::DeltaWire::DecodeDiff(tail.data(), tail.size(), back);
    assert(!tailOk);
    const bool nullOk = World::DeltaWire::DecodeDiff(nullptr, 4, back);
    assert(!nullOk);
    std::vector<std::uint8_t> badKind = bytes;
    badKind[4 + 4] = 9; // first unit kind byte: count(4) + gid(4)
    const bool badKindOk = World::DeltaWire::DecodeDiff(badKind.data(), badKind.size(), back);
    assert(!badKindOk);
    return 0;
}
