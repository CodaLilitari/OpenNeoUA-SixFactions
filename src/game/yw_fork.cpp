// Engine bridge for World::Fork (see yw_fork.h). Only reads the live world;
// stepping happens inside World::Fork on the translated state.
#include "yw_fork.h"

#include <algorithm>

#include "game/yw.h"
#include "game/ypabact.h"
#include "agent/bridge.h"
#include <map>
#include <string>

namespace ForkBridge {
namespace {

void CollectUnits(World::RefBactList &list, World::Fork::WorldState &out)
{
    for (NC_STACK_ypabact *bact : list)
    {
        if (!bact)
            continue;
        World::Fork::UnitState u;
        u.gid = bact->_gid;
        u.protoId = bact->_vehicleID;
        u.owner = bact->_owner;
        u.energy = bact->_energy;
        u.x = static_cast<float>(bact->_position.x);
        u.y = static_cast<float>(bact->_position.y);
        u.z = static_cast<float>(bact->_position.z);
        out.units.push_back(u);
        if (bact->_gid >= out.nextGid)
            out.nextGid = bact->_gid + 1;
        CollectUnits(bact->_kidList, out);
    }
}

} // namespace

bool CaptureWorldState(NC_STACK_ypaworld *world, World::Fork::WorldState &out)
{
    if (!world)
        return false;
    const Common::Point size = world->GetMapSize();
    if (size.x <= 0 || size.y <= 0)
        return false;
    out = World::Fork::WorldState();
    out.mapW = size.x;
    out.mapH = size.y;
    out.timeStampMs = world->_timeStamp;
    out.framesElapsed = world->_framesElapsed;
    out.sectors.reserve(static_cast<std::size_t>(size.x) * size.y);
    int index = 0;
    for (const cellArea &cell : world->Sectors())
    {
        World::Fork::SectorState s;
        s.id = index++;
        s.owner = cell.owner;
        s.typeId = cell.type_id;
        s.purpose = cell.PurposeType;
        s.energyPower = cell.energy_power;
        s.viewMask = cell.view_mask;
        s.border = cell.IsBorder();
        out.sectors.push_back(s);
    }
    CollectUnits(world->_unitsList, out);
    std::sort(out.units.begin(), out.units.end(),
              [](const World::Fork::UnitState &a, const World::Fork::UnitState &b) {
                  return a.gid < b.gid;
              });
    out.units.erase(std::unique(out.units.begin(), out.units.end(),
                                [](const World::Fork::UnitState &a,
                                   const World::Fork::UnitState &b) {
                                    return a.gid == b.gid;
                                }),
                    out.units.end());
    return World::Fork::ValidateState(out, 4096);
}

bool ValidateActions(NC_STACK_ypaworld *world, std::uint8_t controllerOwner,
                     const World::Fork::ForkActionBatch &actions, std::uint32_t ticks)
{
    World::Fork::WorldState state;
    if (!CaptureWorldState(world, state))
        return false;
    World::Fork::ForkConfig cfg;
    cfg.controllerOwner = controllerOwner;
    std::unique_ptr<World::Fork::Fork> fork =
        World::Fork::Fork::Create(state, cfg, World::Fork::ForkId{1});
    return fork && fork->Step(actions, ticks ? ticks : 1);
}

namespace {

NC_STACK_ypaworld *agentWorld = nullptr;
World::Fork::ForkRegistry agentRegistry;
std::map<std::uint64_t, std::unique_ptr<World::Fork::Fork>> agentForks;

std::string HashJson(const World::Fork::Fork &fork)
{
    const World::Fork::SnapshotHandle h = fork.Capture();
    return "\"hash\":" + std::to_string(h.hash) +
           ",\"time_ms\":" + std::to_string(h.timeStampMs);
}

World::Fork::Fork *FindFork(std::uint64_t id)
{
    const auto it = agentForks.find(id);
    if (it == agentForks.end() || !it->second || it->second->Discarded())
        return nullptr;
    return it->second.get();
}

Agent::ForkResult DispatchFork(const Agent::ForkCommand &cmd)
{
    Agent::ForkResult result;
    if (!agentWorld)
    {
        result.error = "fork_unavailable";
        return result;
    }
    switch (cmd.op)
    {
    case Agent::ForkOp::Create:
    {
        World::Fork::WorldState state;
        if (!CaptureWorldState(agentWorld, state))
        {
            result.error = "no_level";
            return result;
        }
        World::Fork::ForkConfig cfg;
        cfg.controllerOwner = cmd.owner;
        const World::Fork::ForkId id = agentRegistry.NextId();
        if (!id.Valid())
        {
            result.error = "fork_limit";
            return result;
        }
        std::unique_ptr<World::Fork::Fork> fork =
            World::Fork::Fork::Create(state, cfg, id);
        if (!fork)
        {
            result.error = "fork_create_failed";
            return result;
        }
        agentRegistry.Track(fork.get());
        agentForks[id.v] = std::move(fork);
        result.ok = true;
        result.json = "{\"fork\":" + std::to_string(id.v) + "," +
                      HashJson(*agentForks[id.v]) + "}";
        return result;
    }
    case Agent::ForkOp::Step:
    {
        World::Fork::Fork *fork = FindFork(cmd.fork);
        if (!fork)
        {
            result.error = "unknown_fork";
            return result;
        }
        int kind = 0, proto = 0, cost = 0;
        std::uint32_t gid = 0, target = 0;
        float x = 0.0f, y = 0.0f;
        if (!Agent::ToForkAction(cmd, kind, gid, target, x, y, proto, cost))
        {
            result.error = "bad_action";
            return result;
        }
        World::Fork::ForkAction action;
        action.kind = static_cast<World::Fork::ActionKind>(kind);
        action.gid = gid;
        action.targetGid = target;
        action.x = x;
        action.y = y;
        action.protoId = proto;
        action.cost = cost;
        World::Fork::ForkActionBatch batch;
        if (action.kind != World::Fork::ActionKind::Hold)
            batch.push_back(action);
        if (!fork->Step(batch, cmd.ticks))
        {
            result.error = "fork_step_failed";
            return result;
        }
        result.ok = true;
        result.json = "{\"fork\":" + std::to_string(cmd.fork) + "," + HashJson(*fork) + "}";
        return result;
    }
    case Agent::ForkOp::Observe:
    {
        World::Fork::Fork *fork = FindFork(cmd.fork);
        if (!fork)
        {
            result.error = "unknown_fork";
            return result;
        }
        const World::Fork::Observation obs = fork->Observe(cmd.owner);
        std::string json = "{\"fork\":" + std::to_string(cmd.fork) +
            ",\"owner\":" + std::to_string(obs.owner) +
            ",\"time_ms\":" + std::to_string(obs.timeStampMs) +
            ",\"own_energy\":" + std::to_string(obs.ownEnergyTotal) + ",\"units\":[";
        bool first = true;
        for (const World::Fork::ObservedUnit &u : obs.units)
        {
            if (!first) json += ",";
            first = false;
            json += "{\"gid\":" + std::to_string(u.gid) +
                    ",\"proto\":" + std::to_string(u.protoId) +
                    ",\"owner\":" + std::to_string(u.owner) +
                    ",\"energy\":" + std::to_string(u.energy) +
                    ",\"x\":" + std::to_string(static_cast<long long>(u.x)) +
                    ",\"y\":" + std::to_string(static_cast<long long>(u.y)) +
                    ",\"own\":" + (u.own ? "true" : "false") + "}";
        }
        json += "],\"sectors\":[";
        first = true;
        for (const World::Fork::ObservedSector &s : obs.sectors)
        {
            if (!first) json += ",";
            first = false;
            json += "{\"id\":" + std::to_string(s.id) +
                    ",\"owner\":" + std::to_string(s.owner) +
                    ",\"energy\":" + std::to_string(s.energyPower) + "}";
        }
        json += "]}";
        result.ok = true;
        result.json = json;
        return result;
    }
    case Agent::ForkOp::Commit:
    {
        World::Fork::Fork *src = FindFork(cmd.fork);
        World::Fork::Fork *dst = FindFork(cmd.dst);
        if (!src || !dst)
        {
            result.error = "unknown_fork";
            return result;
        }
        if (!src->TryCommit(*dst))
        {
            result.error = "fork_commit_failed";
            return result;
        }
        result.ok = true;
        result.json = "{\"fork\":" + std::to_string(cmd.fork) +
                      ",\"dst\":" + std::to_string(cmd.dst) + "," + HashJson(*dst) + "}";
        return result;
    }
    case Agent::ForkOp::Clone:
    {
        World::Fork::Fork *src = FindFork(cmd.fork);
        if (!src)
        {
            result.error = "unknown_fork";
            return result;
        }
        World::Fork::ForkConfig cfg = src->Config();
        cfg.controllerOwner = cmd.owner;
        const World::Fork::ForkId childForkId = agentRegistry.NextId();
        if (!childForkId.Valid())
        {
            result.error = "fork_limit";
            return result;
        }
        const std::uint64_t childId = childForkId.v;
        std::unique_ptr<World::Fork::Fork> child =
            World::Fork::Fork::Create(src->State(), cfg, childForkId);
        if (!child)
        {
            result.error = "fork_create_failed";
            return result;
        }
        World::Fork::Fork *raw = child.get();
        agentForks[childId] = std::move(child);
        agentRegistry.Track(raw);
        result.ok = true;
        result.json = "{\"fork\":" + std::to_string(childId) +
                      ",\"src\":" + std::to_string(cmd.fork) + "," +
                      HashJson(*raw) + "}";
        return result;
    }
    case Agent::ForkOp::Discard:
    {
        World::Fork::Fork *fork = FindFork(cmd.fork);
        if (!fork)
        {
            result.error = "unknown_fork";
            return result;
        }
        // Keep the object: ForkRegistry::Prune reads Discarded() through a
        // raw pointer, so the slot is freed logically, not by destruction.
        fork->Discard();
        result.ok = true;
        result.json = "{\"fork\":" + std::to_string(cmd.fork) + ",\"discarded\":true}";
        return result;
    }
    }
    result.error = "fork_failed";
    return result;
}

} // namespace

void RegisterAgentHandler(NC_STACK_ypaworld *world)
{
    agentWorld = world;
    Agent::SetForkHandler(&DispatchFork);
}

} // namespace ForkBridge
