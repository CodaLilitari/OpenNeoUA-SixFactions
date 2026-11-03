#include "../src/world/net_restore.h"
#include "require.h"
#include <cstdint>
#include <map>
#include <vector>

struct Record { int type; uint32_t id; };
using World::NetRestore::Step;

int main()
{
    // A Black Sect station snapshot with both different turrets firing, plus
    // an ordinary squad. Use the production plan to apply IDs and launchers.
    const Record snapshot[] = {
        {1, 100}, {4, 101}, {5, 110}, {4, 111}, {4, 112},
        {5, 120}, {4, 121}, {2, 130}, {4, 131}, {3, 140},
        {4, 141}, {3, 150}, {4, 151}, {2, 160}, {4, 161}, {0, 0}
    };
    std::vector<Step> plan;
    REQUIRE(World::NetRestore::BuildPlan(snapshot, 16, 2, plan));
    REQUIRE(plan.size() == 15);
    uint32_t turretIds[2] = {};
    std::map<uint32_t, uint32_t> parents;
    for (std::size_t i = 0; i < plan.size(); ++i)
    {
        if (snapshot[i].type == 5)
            turretIds[plan[i].gun] = snapshot[i].id;
        if (snapshot[i].type != 1)
            parents[snapshot[i].id] = snapshot[plan[i].parent].id;
    }
    REQUIRE(turretIds[0] == 110 && turretIds[1] == 120);
    REQUIRE(parents[111] == 110 && parents[112] == 110);
    REQUIRE(parents[121] == 120); // second turret must not overwrite first
    REQUIRE(parents[101] == 100 && parents[131] == 130);
    REQUIRE(parents[140] == 130 && parents[141] == 140);
    REQUIRE(parents[150] == 130 && parents[151] == 150);
    REQUIRE(parents[160] == 100 && parents[161] == 160);

    // Legacy four-gun hosts and the one-gun Queen use the same receiver.
    const Record four[] = {{1,1},{5,2},{5,3},{5,4},{5,5},{0,0}};
    REQUIRE(World::NetRestore::BuildPlan(four, 6, 4, plan));
    REQUIRE(plan[1].gun == 0 && plan[4].gun == 3);
    const Record queen[] = {{1,1},{5,2},{4,3},{0,0}};
    REQUIRE(World::NetRestore::BuildPlan(queen, 4, 1, plan));
    REQUIRE(plan[2].parent == 1);

    const std::vector<std::vector<Record>> malformed = {
        {}, {{0,0}}, {{4,1},{0,0}}, {{1,1},{3,2},{0,0}},
        {{1,1},{1,2},{0,0}}, {{1,1},{9,2},{0,0}},
        {{1,1},{0,0},{0,0}}, {{1,1},{5,2}},
        {{1,1},{2,2},{5,3},{3,4},{0,0}}
    };
    for (const auto &bad : malformed)
    {
        REQUIRE(!World::NetRestore::BuildPlan(bad.data(), bad.size(), 4, plan));
        REQUIRE(plan.empty()); // callers cannot accidentally use a partial plan
    }
    REQUIRE(!World::NetRestore::BuildPlan(four, 6, 2, plan));
    REQUIRE(!World::NetRestore::BuildPlan<Record>(nullptr, 4, 2, plan));
    REQUIRE(!World::NetRestore::BuildPlan(snapshot, 1025, 2, plan));

    // Model nested send traversal: a host's missiles, commanders, their
    // missiles, slaves and slave missiles must all share one reserved limit.
    const std::vector<std::vector<int>> groups = {
        {1,4,4,4,4,4,4,4,4}, {2,4,4,4}, {3,4,4,4}, {2,3,4}
    };
    for (std::size_t capacity : {2u, 8u, 12u, 16u})
    {
        Record guarded[18];
        for (auto &record : guarded) record = {99, 999};
        World::NetRestore::SnapshotWriter<Record> writer(guarded + 1, capacity);
        uint32_t id = 1;
        bool overflow = false;
        for (const auto &group : groups)
            for (int type : group)
                if (!writer.Append({type, id++})) overflow = true;
        REQUIRE(overflow && !writer.Finish());
        REQUIRE(writer.Count() == capacity - 1);
        REQUIRE(guarded[0].type == 99 && guarded[capacity + 1].type == 99);
        REQUIRE(guarded[capacity].type == 99); // reserved terminator not overwritten
    }
    Record complete[6] = {};
    World::NetRestore::SnapshotWriter<Record> writer(complete, 6);
    for (int i = 0; i < 5; ++i) REQUIRE(writer.Append(four[i]));
    REQUIRE(writer.Finish() && writer.Count() == 6 && complete[5].type == 0);
    REQUIRE(World::NetRestore::BuildPlan(complete, writer.Count(), 4, plan));
    REQUIRE(!writer.Finish());
}
