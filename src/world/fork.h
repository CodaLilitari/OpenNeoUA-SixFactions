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
    std::uint32_t maxUnits = MaxWorldUnits;   // whole world, as captured
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

// Fork states live on the map plane: x grows east, y grows toward later
// sector rows, z is altitude. The engine stores positions as (x, altitude, z)
// with z negative toward later rows (World::PositionToSectorID uses x and
// -z). Every capture of live units must go through this mapping, otherwise
// sector lookup and sensor range are computed against altitude.
inline void EnginePositionToPlane(double engineX, double engineY, double engineZ,
                                  float &planeX, float &planeY, float &altitude)
{
    planeX = static_cast<float>(engineX);
    planeY = static_cast<float>(-engineZ);
    altitude = static_cast<float>(engineY);
}

inline int SectorIndexOf(const WorldState &s, float x, float y)
{
    // Compared as floats first: converting NaN or a value beyond int to int
    // is undefined.
    const float fx = std::floor(x / World::CVSectorLength);
    const float fy = std::floor(y / World::CVSectorLength);
    if (!(fx >= 0.0f && fy >= 0.0f && fx < static_cast<float>(s.mapW) &&
          fy < static_cast<float>(s.mapH)))
        return -1;
    return static_cast<int>(fy) * s.mapW + static_cast<int>(fx);
}
// The sensor range test of every fog check: an own unit at (ownX, ownY)
// sees a unit at (x, y). One definition, so all fog checks agree at the edge.
inline bool InSensorRange(float ownX, float ownY, float x, float y, float rangeSquared)
{
    const float dx = ownX - x, dy = ownY - y;
    return dx * dx + dy * dy <= rangeSquared;
}
// Whether `owner` may observe u: its own units, units in a sector it sees,
// and units within sensor range of one of its units. Scans the whole world;
// to ask about many units of one state, use SensorView.
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
        if (InSensorRange(own.x, own.y, u.x, u.y, r2))
            return true;
    }
    return false;
}

// UnitVisibleTo for every unit of one state, as one owner sees it. Asking
// UnitVisibleTo about each unit scans the world once per unit, so a fog pass
// over n units costs n * n: at the 4096-unit cap one Veteran bot think made
// nine such passes and stalled its frame by 70 ms or more. SensorView sorts
// the owner's units into sector cells once and keeps their bounding box per
// cell. A query then looks only at the cells around the unit it asks about,
// skips a cell whose box is out of range, and tests the units of the rest.
// Sees(u) returns exactly UnitVisibleTo(state, u, owner, sensorRange). The
// view reads the state, which must outlive it unchanged.
class SensorView
{
public:
    SensorView(const WorldState &state, std::uint8_t owner, float sensorRange)
        : state_(state), owner_(owner), rangeSquared_(sensorRange * sensorRange),
          cols_(std::max(1, state.mapW)), rows_(std::max(1, state.mapH))
    {
        // An own unit within range lies at most floor(range / cell) + 1 cells
        // from the queried unit's cell; one more cell absorbs float rounding.
        // Off-map positions count in the nearest edge cell, which keeps every
        // distance in cells at most the real one.
        const double reach =
            std::floor(std::fabs(static_cast<double>(sensorRange)) / World::CVSectorLength) + 2.0;
        const int whole = std::max(cols_, rows_);
        reach_ = reach < whole ? static_cast<int>(reach) : whole;
        const std::size_t cells = static_cast<std::size_t>(cols_) * rows_;
        start_.assign(cells + 1, 0);
        for (const UnitState &u : state.units)
            if (u.owner == owner && Placed(u))
                ++start_[CellOf(u) + 1];
        for (std::size_t c = 1; c < start_.size(); ++c)
            start_[c] += start_[c - 1];
        xs_.resize(start_.back());
        ys_.resize(start_.back());
        boxes_.assign(cells, Box());
        std::vector<std::uint32_t> next(start_.begin(), start_.end() - 1);
        for (const UnitState &u : state.units)
        {
            if (u.owner != owner)
                continue;
            if (!Placed(u))
            {
                unplaced_.push_back(&u); // no cell; tested on every query
                continue;
            }
            const std::size_t c = CellOf(u);
            const std::uint32_t i = next[c]++;
            xs_[i] = u.x;
            ys_[i] = u.y;
            Box &b = boxes_[c];
            b.x0 = std::min(b.x0, u.x);
            b.x1 = std::max(b.x1, u.x);
            b.y0 = std::min(b.y0, u.y);
            b.y1 = std::max(b.y1, u.y);
        }
    }

    bool Sees(const UnitState &u) const
    {
        if (u.owner == owner_)
            return true;
        const int idx = SectorIndexOf(state_, u.x, u.y);
        if (idx >= 0 && (state_.sectors[idx].viewMask & (1u << owner_)))
            return true;
        for (const UnitState *own : unplaced_)
            if (Test(own->x, own->y, u))
                return true;
        if (!Placed(u))
        {
            // No cell to start from: test every own unit.
            for (std::uint32_t i = 0; i < start_.back(); ++i)
                if (Test(xs_[i], ys_[i], u))
                    return true;
            return false;
        }
        const int cx = Column(u.x), cy = Row(u.y);
        const int x0 = std::max(0, cx - reach_), x1 = std::min(cols_ - 1, cx + reach_);
        const int y0 = std::max(0, cy - reach_), y1 = std::min(rows_ - 1, cy + reach_);
        for (int y = y0; y <= y1; ++y)
        {
            const std::size_t row = static_cast<std::size_t>(y) * cols_;
            if (start_[row + x0] == start_[row + x1 + 1])
                continue; // no own unit in this row of the window
            for (int x = x0; x <= x1; ++x)
            {
                const std::size_t c = row + x;
                if (start_[c] == start_[c + 1] || OutOfRange(boxes_[c], u))
                    continue;
                for (std::uint32_t i = start_[c]; i < start_[c + 1]; ++i)
                    if (Test(xs_[i], ys_[i], u))
                        return true;
            }
        }
        return false;
    }

    // Own-unit range tests made by Sees so far: the cost of the queries.
    std::uint64_t RangeTests() const { return rangeTests_; }

private:
    struct Box
    {
        float x0 = HUGE_VALF, y0 = HUGE_VALF, x1 = -HUGE_VALF, y1 = -HUGE_VALF;
    };

    static bool Placed(const UnitState &u) { return std::isfinite(u.x) && std::isfinite(u.y); }
    static int Clamp(float cell, int count)
    {
        if (cell <= 0.0f)
            return 0;
        if (cell >= static_cast<float>(count - 1))
            return count - 1;
        return static_cast<int>(cell);
    }
    int Column(float x) const { return Clamp(std::floor(x / World::CVSectorLength), cols_); }
    int Row(float y) const { return Clamp(std::floor(y / World::CVSectorLength), rows_); }
    std::size_t CellOf(const UnitState &u) const
    {
        return static_cast<std::size_t>(Row(u.y)) * cols_ + Column(u.x);
    }
    bool Test(float ownX, float ownY, const UnitState &u) const
    {
        ++rangeTests_;
        return InSensorRange(ownX, ownY, u.x, u.y, rangeSquared_);
    }
    // Whether no unit in box b can pass InSensorRange for u. The distance to
    // the box is computed in double and must exceed the range by more than
    // the float test can round: its relative error stays below 1e-6, and
    // 1e-30 covers squares that underflow near zero.
    bool OutOfRange(const Box &b, const UnitState &u) const
    {
        const double x = u.x, y = u.y;
        const double dx = std::max(0.0, std::max(b.x0 - x, x - b.x1));
        const double dy = std::max(0.0, std::max(b.y0 - y, y - b.y1));
        return (dx * dx + dy * dy) * (1.0 - 1e-6) > static_cast<double>(rangeSquared_) + 1e-30;
    }

    const WorldState &state_;
    std::uint8_t owner_;
    float rangeSquared_;
    int cols_, rows_, reach_ = 0;
    std::vector<std::uint32_t> start_;   // own units of cell c: [start_[c], start_[c + 1])
    std::vector<float> xs_, ys_;         // their positions, grouped by cell
    std::vector<Box> boxes_;             // their bounds per cell
    std::vector<const UnitState *> unplaced_;
    mutable std::uint64_t rangeTests_ = 0;
};

// The world as one faction sees it, as a state a fork can start from, so a
// planner can step what-ifs without reading anything that faction cannot see.
// It holds what Observe reports, plus what stepping needs:
//  - The owner's own units, in full.
//  - Every other unit the owner sees (UnitVisibleTo, through a SensorView),
//    with only what can be seen of it: gid, prototype, owner, energy and
//    position. Its orders, cooldown and parent are cleared.
//  - Every sector the owner owns or has its view bit on, with the view bits
//    of all other owners cleared, since what an enemy sees is hidden too.
//  - Every other sector blank: it keeps its id and its border flag, which
//    the map size fixes, and has owner 0 and no energy, purpose, type or
//    view bits.
//  - nextGid one past the highest gid kept (at least the WorldState
//    default): a captured counter is one past the highest gid of all units,
//    the hidden ones included.
//  - Map size, clock and RNG state as they are.
// A blank sector reads as owner 0, nobody's, rather than as a marked
// unknown, because nothing reads a sector that no faction owns or sees:
// planned moves and builds target only sectors their faction sees
// (NetBot::CandidateOrders), Fork::Step builds and creates only in the
// controller's own sectors, evaluations count only the evaluator's own
// sectors (NetBot::EvaluateFor), and no faction has a view bit there. A blank
// sector was never the owner's own, since its own sectors are all kept. Only
// an own unit whose earlier move goal lies in one captures it on arrival, as
// it would an unowned sector, which is the owner's best guess. The owner
// must be a faction (not 0), or blank sectors would read as its own.
// Observing the picture as the owner with the same range returns what
// observing the world does. Adds the fog check's range tests to *rangeTests
// when given.
inline WorldState VisibleWorld(const WorldState &state, std::uint8_t owner, float sensorRange,
                               std::uint64_t *rangeTests = nullptr)
{
    WorldState seen;
    seen.mapW = state.mapW;
    seen.mapH = state.mapH;
    seen.timeStampMs = state.timeStampMs;
    seen.framesElapsed = state.framesElapsed;
    seen.rngState = state.rngState;
    const std::uint32_t ownBit = 1u << owner;
    seen.sectors.reserve(state.sectors.size());
    for (const SectorState &s : state.sectors)
    {
        SectorState v;
        if (s.owner == owner || (s.viewMask & ownBit))
        {
            v = s;
            v.viewMask = s.viewMask & ownBit;
        }
        else
        {
            v.id = s.id;
            v.border = s.border;
        }
        seen.sectors.push_back(v);
    }
    const SensorView view(state, owner, sensorRange);
    seen.units.reserve(state.units.size());
    for (const UnitState &u : state.units)
    {
        if (!view.Sees(u))
            continue;
        if (u.owner == owner)
        {
            seen.units.push_back(u);
        }
        else
        {
            UnitState v;
            v.gid = u.gid;
            v.protoId = u.protoId;
            v.owner = u.owner;
            v.energy = u.energy;
            v.x = u.x; v.y = u.y; v.z = u.z;
            seen.units.push_back(v);
        }
        if (u.gid >= seen.nextGid)
            seen.nextGid = u.gid + 1;
    }
    if (rangeTests)
        *rangeTests += view.RangeTests();
    return seen;
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
        if (EncodedStateSize(src) > cfg.maxSnapshotBytes)
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
        if (EncodedStateSize(snapshot) > cfg_.maxSnapshotBytes)
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
        const SensorView view(state_, owner, sensorRange);
        for (const UnitState &u : state_.units)
        {
            if (!view.Sees(u))
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
