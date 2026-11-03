// Delta encoding for ForkDiff (design Phase 3): a fork diff becomes the
// delta payload against an acknowledged snapshot baseline; an empty diff is
// the heartbeat case. Pure little-endian codec, fail-closed decode, so the
// format is unit-tested standalone before the transport cutover uses it.
#ifndef WORLD_DELTA_WIRE_H_INCLUDED
#define WORLD_DELTA_WIRE_H_INCLUDED

#include <cstdint>
#include <cstring>
#include <vector>
#include "fork.h"

namespace World { namespace DeltaWire {

constexpr std::size_t MaxDiffUnits = Fork::MaxWorldUnits;
constexpr std::size_t MaxDiffSectors = 65536;

namespace detail {
inline void PutU8(std::vector<std::uint8_t> &out, std::uint8_t v) { out.push_back(v); }
inline void PutU32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) out.push_back((std::uint8_t)((v >> (8 * i)) & 0xFF));
}
inline void PutI32(std::vector<std::uint8_t> &out, std::int32_t v)
{
    PutU32(out, (std::uint32_t)v);
}
inline void PutF32(std::vector<std::uint8_t> &out, float v)
{
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    PutU32(out, bits);
}

struct Reader
{
    const std::uint8_t *data;
    std::size_t size;
    std::size_t pos = 0;
    bool U8(std::uint8_t &v)
    {
        if (pos + 1 > size) return false;
        v = data[pos++];
        return true;
    }
    bool U32(std::uint32_t &v)
    {
        if (pos + 4 > size) return false;
        v = (std::uint32_t)data[pos] | ((std::uint32_t)data[pos + 1] << 8) |
            ((std::uint32_t)data[pos + 2] << 16) | ((std::uint32_t)data[pos + 3] << 24);
        pos += 4;
        return true;
    }
    bool I32(std::int32_t &v)
    {
        std::uint32_t u;
        if (!U32(u)) return false;
        v = (std::int32_t)u;
        return true;
    }
    bool F32(float &v)
    {
        std::uint32_t bits;
        if (!U32(bits)) return false;
        std::memcpy(&v, &bits, 4);
        return true;
    }
    bool Done() const { return pos == size; }
};
} // namespace detail

inline bool EncodeDiff(const Fork::ForkDiff &diff, std::vector<std::uint8_t> &out)
{
    if (diff.units.size() > MaxDiffUnits || diff.sectors.size() > MaxDiffSectors)
        return false;
    out.clear();
    detail::PutU32(out, (std::uint32_t)diff.units.size());
    for (const Fork::UnitDelta &u : diff.units)
    {
        detail::PutU32(out, u.gid);
        detail::PutU8(out, (std::uint8_t)u.kind);
        detail::PutI32(out, u.protoId);
        detail::PutU8(out, u.owner);
        detail::PutI32(out, u.energyBefore);
        detail::PutI32(out, u.energyAfter);
        detail::PutF32(out, u.x);
        detail::PutF32(out, u.y);
    }
    detail::PutU32(out, (std::uint32_t)diff.sectors.size());
    for (const Fork::SectorDelta &s : diff.sectors)
    {
        detail::PutI32(out, s.id);
        detail::PutU8(out, s.ownerBefore);
        detail::PutU8(out, s.ownerAfter);
        detail::PutI32(out, s.energyBefore);
        detail::PutI32(out, s.energyAfter);
    }
    return true;
}

inline bool DecodeDiff(const std::uint8_t *data, std::size_t len, Fork::ForkDiff &out)
{
    if (!data && len)
        return false;
    detail::Reader r{data, len, 0};
    std::uint32_t unitCount = 0;
    if (!r.U32(unitCount) || unitCount > MaxDiffUnits)
        return false;
    Fork::ForkDiff diff;
    diff.units.reserve(unitCount);
    for (std::uint32_t i = 0; i < unitCount; ++i)
    {
        Fork::UnitDelta u;
        std::uint8_t kind = 0;
        if (!r.U32(u.gid) || !r.U8(kind) || kind > 2 ||
            !r.I32(u.protoId) || !r.U8(u.owner) ||
            !r.I32(u.energyBefore) || !r.I32(u.energyAfter) ||
            !r.F32(u.x) || !r.F32(u.y))
            return false;
        u.kind = (Fork::UnitDelta::Kind)kind;
        diff.units.push_back(u);
    }
    std::uint32_t sectorCount = 0;
    if (!r.U32(sectorCount) || sectorCount > MaxDiffSectors)
        return false;
    diff.sectors.reserve(sectorCount);
    for (std::uint32_t i = 0; i < sectorCount; ++i)
    {
        Fork::SectorDelta s;
        if (!r.I32(s.id) || !r.U8(s.ownerBefore) || !r.U8(s.ownerAfter) ||
            !r.I32(s.energyBefore) || !r.I32(s.energyAfter))
            return false;
        diff.sectors.push_back(s);
    }
    if (!r.Done())
        return false;
    out = diff;
    return true;
}

} }
#endif
