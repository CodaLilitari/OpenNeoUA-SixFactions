#include "../src/world/net_building_units.h"
#include <algorithm>
#include "require.h"
#include <cstdint>
#include <deque>
#include <set>
#include <vector>

struct Record { uint32_t id = 0; int protoID = 0; };
struct Unit
{
    uint32_t id;
    bool dead = false;
    Unit *parent = nullptr;
    std::vector<Unit *> children;
};

// A small ownership graph retains production AddSubject's push_front order
// and NetGetBactByID's commander-then-subordinate lookup order.
void Attach(Unit &parent, Unit &unit)
{
    if (unit.parent)
    {
        auto &old = unit.parent->children;
        old.erase(std::find(old.begin(), old.end(), &unit));
    }
    unit.parent = &parent;
    parent.children.insert(parent.children.begin(), &unit);
}

Unit *Find(Unit &host, uint32_t id)
{
    for (Unit *commander : host.children)
    {
        if (commander->id == id) return commander;
        for (Unit *child : commander->children)
            if (child->id == id) return child;
    }
    return nullptr;
}

void Apply(const Record *records,
           const std::vector<World::NetBuildingUnits::Step> &plan,
           Unit &host, std::deque<Unit> &units)
{
    Unit *commander = nullptr;
    for (const auto &step : plan)
    {
        units.push_back({records[step.recordIndex].id, false, nullptr, {}});
        Unit &unit = units.back();
        if (step.commander)
        {
            commander = &unit;
            Attach(host, unit);
        }
        else
        {
            REQUIRE(commander);
            Attach(*commander, unit);
        }
    }
}

void DieAndPromote(Unit &host, Unit &commander, Unit &survivor)
{
    Attach(host, survivor);
    while (!commander.children.empty())
        Attach(survivor, *commander.children.front());
    commander.dead = true;
}

int main()
{
    // Actual Crosshill Black building31 packet IDs predicted by authored load
    // order: four TFL guns. Entry0 is the commander, not a fifth turret.
    const Record packet[8] = {
        {83951642,108}, {83951643,108}, {83951644,108}, {83951645,108}
    };
    const auto plan = World::NetBuildingUnits::BuildPlan(packet);
    REQUIRE(plan.size() == 4);
    Unit host{83951636, false, nullptr, {}};
    std::deque<Unit> units;
    Apply(packet, plan, host, units);
    REQUIRE(units.size() == 4 && host.children.size() == 1);
    std::set<uint32_t> ids;
    for (const Unit &unit : units) REQUIRE(ids.insert(unit.id).second);
    Unit *commander = Find(host, packet[0].id);
    Unit *survivor = Find(host, packet[1].id);
    REQUIRE(commander && survivor && commander->children.size() == 3);
    DieAndPromote(host, *commander, *survivor);
    // A later DESTROY must resolve the logically dead original, even though
    // promotion moved the survivor to the front of the station's children.
    REQUIRE(Find(host, packet[0].id) == commander);
    REQUIRE(Find(host, packet[0].id)->dead);

    // Reproduce the historical extra entry0 to show why this exact lifecycle
    // produces a live duplicate before the original dead commander.
    const std::vector<World::NetBuildingUnits::Step> legacy = {
        {0,true}, {0,false}, {1,false}, {2,false}, {3,false}
    };
    Unit oldHost{83951636, false, nullptr, {}};
    std::deque<Unit> oldUnits;
    Apply(packet, legacy, oldHost, oldUnits);
    Unit *oldCommander = Find(oldHost, packet[0].id);
    DieAndPromote(oldHost, *oldCommander, *Find(oldHost, packet[1].id));
    REQUIRE(Find(oldHost, packet[0].id) != oldCommander);
    REQUIRE(!Find(oldHost, packet[0].id)->dead);

    // A full wire array is valid without an extra sentinel beyond its end.
    const Record full[8] = {
        {10,108},{11,108},{12,108},{13,108},
        {14,108},{15,108},{16,108},{17,108}
    };
    const auto fullPlan = World::NetBuildingUnits::BuildPlan(full);
    REQUIRE(fullPlan.size() == 8);
    for (std::size_t i = 0; i < fullPlan.size(); ++i)
    {
        REQUIRE(fullPlan[i].recordIndex == i);
        REQUIRE(fullPlan[i].commander == (i == 0));
    }
    const Record earlyEnd[8] = {{10,108},{0,0},{99,108}};
    const auto one = World::NetBuildingUnits::BuildPlan(earlyEnd);
    REQUIRE(one.size() == 1 && one[0].commander && one[0].recordIndex == 0);
    const Record empty[8] = {};
    REQUIRE(World::NetBuildingUnits::BuildPlan(empty).empty());
}
