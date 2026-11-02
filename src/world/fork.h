#ifndef WORLD_FORK_H_INCLUDED
#define WORLD_FORK_H_INCLUDED

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "consts.h"
#include "fork_rng.h"
#include "fork_snapshot.h"
#include "net_factions.h"

namespace World { namespace Fork {

// Forkable deterministic simulation. One API serves both consumers:
//  - local what-if: Fork::ForkFrom(parent) + Step() + Diff(), then Discard();
//  - server-side bot planning: capture the live world (yw_fork.cpp), Fork it,
//    Step() candidate plans, then commit ONLY the chosen actions by replay
//    (TryCommit validates and replays the action log; state is never copied
//    into the authoritative world, so net CRC checks still guard divergence).
// Forks never touch the net driver, renderer, audio or the agent bridge.

struct ForkId
{
    std::uint64_t v = 0;
    bool Valid() const { return v != 0; }
};

struct ForkConfig
{
    std::int32_t fixedDtMs = 40;              // matches forced screenshot step
    std::uint32_t maxTicksPerStep = 250;      // 10 s of sim per Step call
    std::uint32_t maxUnits = 1024;            // net snapshot caps (yw_net.h)
    std::size_t maxSnapshotBytes = 4u << 20;  // encoded snapshot ceiling
    std::uint32_t maxLiveForks = 8;           // per ForkRegistry
    std::uint8_t controllerOwner = 0;         // only owner this fork may order
    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    float unitSpeed = 60.0f;                  // map units per second
    float attackRange = 300.0f;
    std::int32_t attackDamage = 10;           // per hit (cooldown 500 ms)
};

struct SnapshotHandle
{
    std::uint64_t hash = 0;
    std::int32_t timeStampMs = 0;
    std::uint32_t framesElapsed = 0;
    bool Valid() const { return hash != 0; }
};

enum class ActionKind : std::uint8_t { Hold, Move, Attack, Create, Build };

struct ForkAction
{
    ActionKind kind = ActionKind::Hold;
    std::uint32_t gid = 0;        // actor (Move/Attack); 0 for Create/Build
    std::uint32_t targetGid = 0;  // Attack victim
    float x = 0.0f, y = 0.0f;     // Move goal / Create+Build sector position
    std::int32_t protoId = 0;     // Create/Build prototype
    std::int32_t cost = 0;        // energy cost charged to the sector pool
};

typedef std::vector<ForkAction> ForkActionBatch;

// Fog-filtered view for one owner: own units in full, enemy units only when
// legally observable (sector view mask or sensor range of an own unit).
struct ObservedUnit
{
    std::uint32_t gid = 0;
    std::int32_t protoId = 0;
    std::uint8_t owner = 0;
    std::int32_t energy = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool own = false;
};
struct ObservedSector
{
    std::int32_t id = 0;
    std::uint8_t owner = 0;
    std::int32_t energyPower = 0;
    std::uint8_t purpose = 0;
};
struct Observation
{
    std::uint8_t owner = 0;
    std::int32_t timeStampMs = 0;
    std::vector<ObservedUnit> units;     // gid-sorted
    std::vector<ObservedSector> sectors; // only visible sectors
    std::int32_t ownEnergyTotal = 0;
};

struct UnitDelta
{
    std::uint32_t gid = 0;
    enum Kind : std::uint8_t { Created, Destroyed, Changed } kind = Changed;
    std::int32_t protoId = 0;
    std::uint8_t owner = 0;
    std::int32_t energyBefore = 0, energyAfter = 0;
    float x = 0.0f, y = 0.0f;
};
struct SectorDelta
{
    std::int32_t id = 0;
    std::uint8_t ownerBefore = 0, ownerAfter = 0;
    std::int32_t energyBefore = 0, energyAfter = 0;
};
struct ForkDiff
{
    std::vector<UnitDelta> units;     // gid-sorted
    std::vector<SectorDelta> sectors; // id-sorted
    bool Empty() const { return units.empty() && sectors.empty(); }
};

enum class MergePolicy : std::uint8_t
{
    RejectIfStale,   // base hash must still match the destination (default)
    ReplayAnyway     // replay actions even if the destination advanced
};

inline int SectorIndexOf(const WorldState &s, float x, float y)
{
    const int sx = static_cast<int>(std::floor(x / World::CVSectorLength));
    const int sy = static_cast<int>(std::floor(y / World::CVSectorLength));
    if (sx < 0 || sy < 0 || sx >= s.mapW || sy >= s.mapH)
        return -1;
    return sy * s.mapW + sx;
}
inline bool UnitVisibleTo(const WorldState &s, const UnitState &u, std::uint8_t owner,
                          float sensorRange)
{
    if (u.owner == owner)
        return true;
    const int idx = SectorIndexOf(s, u.x, u.y);
    if (idx >= 0 && (s.sectors[idx].viewMask & (1u << owner)))
        return true;
    const float r2 = sensorRange * sensorRange;
    for (const UnitState &own : s.units)
    {
        if (own.owner != owner)
            continue;
        const float dx = own.x - u.x, dy = own.y - u.y;
        if (dx * dx + dy * dy <= r2)
            return true;
    }
    return false;
}

class Fork
{
public:
    // Deep-copies src into a new fork. Returns nullptr when the state is
    // invalid or exceeds config limits. Never references src afterwards.
    static std::unique_ptr<Fork> Create(const WorldState &src, const ForkConfig &cfg,
                                        ForkId id)
    {
        if (!id.Valid() || cfg.fixedDtMs <= 0 || cfg.maxTicksPerStep == 0)
            return nullptr;
        if (!ValidateState(src, cfg.maxUnits))
            return nullptr;
        if (EncodeState(src).size() > cfg.maxSnapshotBytes)
            return nullptr;
        std::unique_ptr<Fork> fork(new Fork(cfg, id));
        fork->state_ = src;
        fork->baseHash_ = HashState(src);
        fork->rng_.SetState(src.rngState);
        return fork;
    }

    ForkId Id() const { return id_; }
    const ForkConfig &Config() const { return cfg_; }
    const WorldState &State() const { return state_; }
    bool Discarded() const { return discarded_; }
    const ForkActionBatch &ActionLog() const { return log_; }

    SnapshotHandle Capture() const
    {
        SnapshotHandle h;
        h.hash = HashState(state_);
        h.timeStampMs = state_.timeStampMs;
        h.framesElapsed = state_.framesElapsed;
        return h;
    }

    // Restores a state previously produced from this fork lineage (same map
    // dimensions); the action log is cleared because history no longer
    // describes the restored state.
    bool Restore(const WorldState &snapshot)
    {
        if (discarded_ || snapshot.mapW != state_.mapW || snapshot.mapH != state_.mapH)
            return false;
        if (!ValidateState(snapshot, cfg_.maxUnits))
            return false;
        if (EncodeState(snapshot).size() > cfg_.maxSnapshotBytes)
            return false;
        state_ = snapshot;
        rng_.SetState(state_.rngState);
        log_.clear();
        return true;
    }

    // Child fork sharing nothing mutable with this fork (deep copy), so
    // stepping either one cannot disturb the other.
    std::unique_ptr<Fork> ForkFrom(ForkId childId) const
    {
        if (discarded_)
            return nullptr;
        return Create(state_, cfg_, childId);
    }

    // Validates the batch against controller ownership, then applies it and
    // advances `ticks` fixed steps. Illegal actions abort the whole batch
    // (no partial application, nothing logged).
    bool Step(const ForkActionBatch &actions, std::uint32_t ticks = 1)
    {
        if (discarded_ || ticks == 0 || ticks > cfg_.maxTicksPerStep)
            return false;
        for (const ForkAction &a : actions)
            if (!ActionLegal(a))
                return false;
        for (const ForkAction &a : actions)
        {
            ApplyAction(a);
            log_.push_back(a);
        }
        for (std::uint32_t t = 0; t < ticks; ++t)
            TickOnce();
        return state_.units.size() <= cfg_.maxUnits;
    }

    Observation Observe(std::uint8_t owner, float sensorRange = 1800.0f) const
    {
        Observation obs;
        obs.owner = owner;
        obs.timeStampMs = state_.timeStampMs;
        for (const SectorState &s : state_.sectors)
        {
            const bool visible = s.owner == owner || (s.viewMask & (1u << owner));
            if (visible)
                obs.sectors.push_back({s.id, s.owner, s.energyPower, s.purpose});
        }
        for (const UnitState &u : state_.units)
        {
            if (!UnitVisibleTo(state_, u, owner, sensorRange))
                continue;
            ObservedUnit ou;
            ou.gid = u.gid;
            ou.protoId = u.protoId;
            ou.owner = u.owner;
            ou.energy = u.energy;
            ou.x = u.x; ou.y = u.y; ou.z = u.z;
            ou.own = u.owner == owner;
            obs.units.push_back(ou);
            if (ou.own)
                obs.ownEnergyTotal += u.energy;
        }
        return obs;
    }

    ForkDiff Diff(const Fork &other) const
    {
        ForkDiff diff;
        std::size_t i = 0, j = 0;
        while (i < state_.units.size() || j < other.state_.units.size())
        {
            if (j == other.state_.units.size() ||
                (i < state_.units.size() && state_.units[i].gid < other.state_.units[j].gid))
            {
                const UnitState &u = state_.units[i++];
                diff.units.push_back({u.gid, UnitDelta::Destroyed, u.protoId, u.owner,
                                      u.energy, 0, u.x, u.y});
            }
            else if (i == state_.units.size() ||
                     other.state_.units[j].gid < state_.units[i].gid)
            {
                const UnitState &u = other.state_.units[j++];
                diff.units.push_back({u.gid, UnitDelta::Created, u.protoId, u.owner,
                                      0, u.energy, u.x, u.y});
            }
            else
            {
                const UnitState &a = state_.units[i++];
                const UnitState &b = other.state_.units[j++];
                if (a.energy != b.energy || a.owner != b.owner ||
                    a.x != b.x || a.y != b.y || a.protoId != b.protoId)
                    diff.units.push_back({a.gid, UnitDelta::Changed, b.protoId, b.owner,
                                          a.energy, b.energy, b.x, b.y});
            }
        }
        const std::size_t n = std::min(state_.sectors.size(), other.state_.sectors.size());
        for (std::size_t k = 0; k < n; ++k)
        {
            const SectorState &a = state_.sectors[k];
            const SectorState &b = other.state_.sectors[k];
            if (a.owner != b.owner || a.energyPower != b.energyPower)
                diff.sectors.push_back({a.id, a.owner, b.owner, a.energyPower, b.energyPower});
        }
        return diff;
    }

    // Commit-by-replay: re-validates this fork's logged actions against dst
    // and applies them there. State bytes are never copied, so a destination
    // that advanced differently fails loudly instead of being overwritten.
    bool TryCommit(Fork &dst, MergePolicy policy = MergePolicy::RejectIfStale) const
    {
        if (discarded_ || dst.discarded_ || &dst == this)
            return false;
        if (policy == MergePolicy::RejectIfStale && HashState(dst.state_) != baseHash_)
            return false;
        Fork probe = dst; // validate on a copy; dst stays untouched on failure
        for (const ForkAction &a : log_)
        {
            if (!probe.ActionLegal(a))
                return false;
            probe.ApplyAction(a); // later actions see earlier effects
        }
        for (const ForkAction &a : log_)
        {
            dst.ApplyAction(a);
            dst.log_.push_back(a);
        }
        return true;
    }

    void Discard() { discarded_ = true; log_.clear(); }

private:
    Fork(const ForkConfig &cfg, ForkId id) : cfg_(cfg), id_(id), rng_(cfg.seed) {}

    bool ActionLegal(const ForkAction &a) const
    {
        switch (a.kind)
        {
        case ActionKind::Hold:
            return true;
        case ActionKind::Move:
        {
            const UnitState *u = state_.FindUnit(a.gid);
            return u && u->owner == cfg_.controllerOwner;
        }
        case ActionKind::Attack:
        {
            const UnitState *u = state_.FindUnit(a.gid);
            const UnitState *v = state_.FindUnit(a.targetGid);
            return u && v && u->owner == cfg_.controllerOwner && v->owner != u->owner;
        }
        case ActionKind::Create:
        case ActionKind::Build:
        {
            if (a.protoId == 0 || a.cost < 0)
                return false;
            const int idx = SectorIndexOf(state_, a.x, a.y);
            if (idx < 0)
                return false;
            const SectorState &s = state_.sectors[idx];
            return s.owner == cfg_.controllerOwner && !s.border &&
                   s.energyPower >= a.cost &&
                   (a.kind != ActionKind::Create || state_.units.size() < cfg_.maxUnits);
        }
        }
        return false;
    }

    void ApplyAction(const ForkAction &a)
    {
        switch (a.kind)
        {
        case ActionKind::Hold:
            break;
        case ActionKind::Move:
            if (UnitState *u = state_.FindUnit(a.gid))
            {
                u->targetX = a.x; u->targetY = a.y; u->hasTarget = true;
                u->attackGid = 0;
            }
            break;
        case ActionKind::Attack:
            if (UnitState *u = state_.FindUnit(a.gid))
            {
                u->attackGid = a.targetGid; u->hasTarget = false;
            }
            break;
        case ActionKind::Create:
        {
            const int idx = SectorIndexOf(state_, a.x, a.y);
            if (idx < 0)
                break;
            state_.sectors[idx].energyPower -= a.cost;
            UnitState u;
            u.gid = state_.nextGid++;
            u.protoId = a.protoId;
            u.owner = cfg_.controllerOwner;
            u.energy = 100;
            u.x = a.x; u.y = a.y;
            state_.units.insert(
                std::lower_bound(state_.units.begin(), state_.units.end(), u.gid,
                                 [](const UnitState &l, std::uint32_t g) { return l.gid < g; }),
                u);
            break;
        }
        case ActionKind::Build:
        {
            const int idx = SectorIndexOf(state_, a.x, a.y);
            if (idx < 0)
                break;
            state_.sectors[idx].energyPower -= a.cost;
            state_.sectors[idx].purpose = 1; // PT_CONSTRUCTING
            break;
        }
        }
    }

    void TickOnce()
    {
        const float dt = cfg_.fixedDtMs / 1000.0f;
        const float stepLen = cfg_.unitSpeed * dt;
        std::vector<std::uint32_t> dead;
        for (UnitState &u : state_.units)
        {
            if (u.cooldownMs > 0)
                u.cooldownMs = std::max(0, u.cooldownMs - cfg_.fixedDtMs);
            if (u.attackGid)
            {
                UnitState *v = state_.FindUnit(u.attackGid);
                if (!v || v->owner == u.owner)
                {
                    u.attackGid = 0;
                }
                else
                {
                    const float dx = v->x - u.x, dy = v->y - u.y;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist > cfg_.attackRange && dist > 0.0f)
                    {
                        const float s = std::min(stepLen, dist) / dist;
                        u.x += dx * s; u.y += dy * s;
                    }
                    else if (u.cooldownMs == 0)
                    {
                        v->energy -= cfg_.attackDamage;
                        u.cooldownMs = 500;
                        if (v->energy <= 0)
                            dead.push_back(v->gid);
                    }
                }
            }
            else if (u.hasTarget)
            {
                const float dx = u.targetX - u.x, dy = u.targetY - u.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist <= stepLen || dist == 0.0f)
                {
                    u.x = u.targetX; u.y = u.targetY; u.hasTarget = false;
                    // Arriving in an enemy/neutral sector captures it when
                    // unoccupied by living enemy units.
                    const int idx = SectorIndexOf(state_, u.x, u.y);
                    if (idx >= 0)
                    {
                        SectorState &s = state_.sectors[idx];
                        bool enemyPresent = false;
                        for (const UnitState &o : state_.units)
                            if (o.owner != u.owner &&
                                SectorIndexOf(state_, o.x, o.y) == idx)
                                enemyPresent = true;
                        if (!enemyPresent && s.owner != u.owner && !s.border)
                            s.owner = u.owner;
                    }
                }
                else
                {
                    const float s = stepLen / dist;
                    u.x += dx * s; u.y += dy * s;
                }
            }
        }
        if (!dead.empty())
        {
            std::sort(dead.begin(), dead.end());
            state_.units.erase(
                std::remove_if(state_.units.begin(), state_.units.end(),
                               [&](const UnitState &u) {
                                   return std::binary_search(dead.begin(), dead.end(), u.gid);
                               }),
                state_.units.end());
        }
        state_.rngState = rng_.State();
        state_.timeStampMs += cfg_.fixedDtMs;
        ++state_.framesElapsed;
    }

    ForkConfig cfg_;
    ForkId id_;
    WorldState state_;
    ForkRng rng_;
    ForkActionBatch log_;
    std::uint64_t baseHash_ = 0;
    bool discarded_ = false;
};

// Owns live forks and enforces the per-session fork budget. Ids are issued
// monotonically; Discarded forks free their slot.
class ForkRegistry
{
public:
    explicit ForkRegistry(std::size_t maxLive = 8) : maxLive_(maxLive) {}

    ForkId NextId()
    {
        Prune();
        if (live_.size() >= maxLive_)
            return ForkId{0};
        return ForkId{next_++};
    }
    void Track(const Fork *fork)
    {
        if (fork)
            live_.push_back(fork);
    }
    std::size_t LiveCount()
    {
        Prune();
        return live_.size();
    }

private:
    void Prune()
    {
        live_.erase(std::remove_if(live_.begin(), live_.end(),
                                   [](const Fork *f) { return !f || f->Discarded(); }),
                    live_.end());
    }
    std::size_t maxLive_;
    std::uint64_t next_ = 1;
    std::vector<const Fork *> live_;
};

} }
#endif
