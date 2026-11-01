#ifndef OPENNEOUA_NET_BUILDING_UNITS_H
#define OPENNEOUA_NET_BUILDING_UNITS_H

#include <cstddef>
#include <vector>

namespace World { namespace NetBuildingUnits {

struct Step
{
    std::size_t recordIndex;
    bool commander;
};

template<class Record, std::size_t Capacity>
std::vector<Step> BuildPlan(const Record (&records)[Capacity])
{
    // A building packet contains the commander at entry0 and its remaining
    // guns in subsequent entries. Zero ends a partial packet; a full array
    // needs no out-of-bounds sentinel. Each source entry is used exactly once.
    std::vector<Step> plan;
    for (std::size_t i = 0; i < Capacity && records[i].protoID != 0; ++i)
        plan.push_back({i, i == 0});
    return plan;
}

}} // namespace World::NetBuildingUnits
#endif
