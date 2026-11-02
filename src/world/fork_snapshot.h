#ifndef WORLD_FORK_SNAPSHOT_H_INCLUDED
#define WORLD_FORK_SNAPSHOT_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace World { namespace Fork {

// In-memory fork state: the deterministic subset of NC_STACK_ypaworld that
// fork stepping needs (sector grid + unit records keyed by gid). Engine code
// translates the live world into this shape (src/game/yw_fork.cpp); forks,
// bot lookahead and tests then share one representation.
struct SectorState
{
    std::int32_t id = 0;
    std::uint8_t owner = 0;        // faction owner id (game/data id)
    std::int32_t typeId = 0;
    std::uint8_t purpose = 0;      // cellArea::PTYPE value
    std::int32_t energyPower = 0;
    std::uint32_t viewMask = 0;    // cellArea::ViewMask bits per owner
    bool border = false;
};

struct UnitState
{
    std::uint32_t gid = 0;
    std::uint32_t parentGid = 0;   // 0 = station/root unit
    std::int32_t protoId = 0;
    std::uint8_t owner = 0;
    std::int32_t energy = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float targetX = 0.0f, targetY = 0.0f; // current move goal (map plane)
    bool hasTarget = false;
    std::uint32_t attackGid = 0;   // 0 = no attack order
    std::int32_t cooldownMs = 0;
};

struct WorldState
{
    std::int32_t mapW = 0, mapH = 0;
    std::vector<SectorState> sectors; // row-major, size == mapW*mapH
    std::vector<UnitState> units;     // kept sorted by gid
    std::int32_t timeStampMs = 0;
    std::uint32_t framesElapsed = 0;
    std::uint64_t rngState = 0;
    std::uint32_t nextGid = 0x10000;  // mirrors bact_id reset in LoadGame

    const UnitState *FindUnit(std::uint32_t gid) const
    {
        for (const UnitState &unit : units)
            if (unit.gid == gid)
                return &unit;
        return nullptr;
    }
    UnitState *FindUnit(std::uint32_t gid)
    {
        for (UnitState &unit : units)
            if (unit.gid == gid)
                return &unit;
        return nullptr;
    }
    const SectorState *SectorAt(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= mapW || y >= mapH)
            return nullptr;
        return &sectors[static_cast<std::size_t>(y) * mapW + x];
    }
    SectorState *SectorAt(int x, int y)
    {
        if (x < 0 || y < 0 || x >= mapW || y >= mapH)
            return nullptr;
        return &sectors[static_cast<std::size_t>(y) * mapW + x];
    }
};

// Structural validity for states entering a fork (capture, decode, merge).
inline bool ValidateState(const WorldState &state, std::size_t maxUnits)
{
    if (state.mapW < 0 || state.mapH < 0 || state.mapW > 256 || state.mapH > 256)
        return false;
    if (state.sectors.size() != static_cast<std::size_t>(state.mapW) * state.mapH)
        return false;
    if (state.units.size() > maxUnits)
        return false;
    for (std::size_t i = 0; i < state.units.size(); ++i)
    {
        if (state.units[i].gid == 0)
            return false;
        if (i && state.units[i - 1].gid >= state.units[i].gid)
            return false; // gid-sorted, unique
    }
    return true;
}

namespace detail {
inline void PutU32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 24));
}
inline void PutU64(std::vector<std::uint8_t> &out, std::uint64_t v)
{
    PutU32(out, static_cast<std::uint32_t>(v));
    PutU32(out, static_cast<std::uint32_t>(v >> 32));
}
inline void PutF32(std::vector<std::uint8_t> &out, float v)
{
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    PutU32(out, bits);
}
struct Reader
{
    const std::uint8_t *p;
    std::size_t left;
    bool U8(std::uint8_t &v)
    {
        if (left < 1) return false;
        v = *p++; --left; return true;
    }
    bool U32(std::uint32_t &v)
    {
        if (left < 4) return false;
        v = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
            (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        p += 4; left -= 4; return true;
    }
    bool U64(std::uint64_t &v)
    {
        std::uint32_t lo, hi;
        return U32(lo) && U32(hi) && (v = lo | (static_cast<std::uint64_t>(hi) << 32), true);
    }
    bool F32(float &v)
    {
        std::uint32_t bits;
        return U32(bits) && (std::memcpy(&v, &bits, 4), true);
    }
};
} // namespace detail

// Deterministic binary encoding: fixed field order, little-endian scalars,
// units in stored (gid-sorted) order. Same state => same bytes => same hash.
inline std::vector<std::uint8_t> EncodeState(const WorldState &state)
{
    std::vector<std::uint8_t> out;
    out.reserve(32 + state.sectors.size() * 18 + state.units.size() * 44);
    detail::PutU32(out, static_cast<std::uint32_t>(state.mapW));
    detail::PutU32(out, static_cast<std::uint32_t>(state.mapH));
    detail::PutU32(out, static_cast<std::uint32_t>(state.timeStampMs));
    detail::PutU32(out, state.framesElapsed);
    detail::PutU64(out, state.rngState);
    detail::PutU32(out, state.nextGid);
    detail::PutU32(out, static_cast<std::uint32_t>(state.sectors.size()));
    for (const SectorState &s : state.sectors)
    {
        detail::PutU32(out, static_cast<std::uint32_t>(s.id));
        out.push_back(s.owner);
        detail::PutU32(out, static_cast<std::uint32_t>(s.typeId));
        out.push_back(s.purpose);
        detail::PutU32(out, static_cast<std::uint32_t>(s.energyPower));
        detail::PutU32(out, s.viewMask);
        out.push_back(s.border ? 1 : 0);
    }
    detail::PutU32(out, static_cast<std::uint32_t>(state.units.size()));
    for (const UnitState &u : state.units)
    {
        detail::PutU32(out, u.gid);
        detail::PutU32(out, u.parentGid);
        detail::PutU32(out, static_cast<std::uint32_t>(u.protoId));
        out.push_back(u.owner);
        detail::PutU32(out, static_cast<std::uint32_t>(u.energy));
        detail::PutF32(out, u.x);
        detail::PutF32(out, u.y);
        detail::PutF32(out, u.z);
        detail::PutF32(out, u.targetX);
        detail::PutF32(out, u.targetY);
        out.push_back(u.hasTarget ? 1 : 0);
        detail::PutU32(out, u.attackGid);
        detail::PutU32(out, static_cast<std::uint32_t>(u.cooldownMs));
    }
    return out;
}

// Strict decoder: rejects truncation, trailing bytes and invalid states.
inline bool DecodeState(const std::uint8_t *data, std::size_t size, std::size_t maxUnits,
                        WorldState &out)
{
    if (!data)
        return false;
    detail::Reader r{data, size};
    WorldState s;
    std::uint32_t v;
    if (!r.U32(v)) return false;
    s.mapW = static_cast<std::int32_t>(v);
    if (!r.U32(v)) return false;
    s.mapH = static_cast<std::int32_t>(v);
    if (!r.U32(v)) return false;
    s.timeStampMs = static_cast<std::int32_t>(v);
    if (!r.U32(s.framesElapsed) || !r.U64(s.rngState) || !r.U32(s.nextGid))
        return false;
    std::uint32_t count;
    if (!r.U32(count) || count > 256u * 256u)
        return false;
    s.sectors.resize(count);
    for (SectorState &sec : s.sectors)
    {
        std::uint8_t byte;
        if (!r.U32(v)) return false;
        sec.id = static_cast<std::int32_t>(v);
        if (!r.U8(sec.owner) || !r.U32(v)) return false;
        sec.typeId = static_cast<std::int32_t>(v);
        if (!r.U8(sec.purpose) || !r.U32(v)) return false;
        sec.energyPower = static_cast<std::int32_t>(v);
        if (!r.U32(sec.viewMask) || !r.U8(byte)) return false;
        sec.border = byte != 0;
    }
    if (!r.U32(count) || count > maxUnits)
        return false;
    s.units.resize(count);
    for (UnitState &u : s.units)
    {
        std::uint8_t byte;
        if (!r.U32(u.gid) || !r.U32(u.parentGid) || !r.U32(v)) return false;
        u.protoId = static_cast<std::int32_t>(v);
        if (!r.U8(u.owner) || !r.U32(v)) return false;
        u.energy = static_cast<std::int32_t>(v);
        if (!r.F32(u.x) || !r.F32(u.y) || !r.F32(u.z) ||
            !r.F32(u.targetX) || !r.F32(u.targetY) || !r.U8(byte))
            return false;
        u.hasTarget = byte != 0;
        if (!r.U32(u.attackGid) || !r.U32(v)) return false;
        u.cooldownMs = static_cast<std::int32_t>(v);
    }
    if (r.left != 0 || !ValidateState(s, maxUnits))
        return false;
    out = s;
    return true;
}

// FNV-1a 64 over EncodeState bytes; used for SnapshotHandle and CRC-style
// desync detection between fork/base/live worlds.
inline std::uint64_t HashState(const WorldState &state)
{
    const std::vector<std::uint8_t> bytes = EncodeState(state);
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint8_t b : bytes)
    {
        hash ^= b;
        hash *= 1099511628211ull;
    }
    return hash;
}

} }
#endif
