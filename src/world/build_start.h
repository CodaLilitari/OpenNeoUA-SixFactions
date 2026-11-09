#ifndef WORLD_BUILD_START_H_INCLUDED
#define WORLD_BUILD_START_H_INCLUDED

#include <cstdint>

namespace World { namespace BuildStart {

// Whether NC_STACK_ypaworld::ypaworld_func148 starts a building, and if not,
// why. In a network game the builder's process starts the construction and
// broadcasts UAMSG_STARTBUILD; every peer then runs ypaworld_func148 on its
// own copy of the world. These are therefore also the reasons a peer refuses
// a building that its owner did start (yw_net.cpp logs which one).
enum class Refusal : std::uint8_t
{
    None = 0,
    OwnerBusy,          // the owner has a construction running, one at a time on this level
    BorderCell,         // the map border never takes a building
    UnderConstruction,  // the cell is under construction already
    Occupied,           // a host station, or the unit this process controls, is in the cell
    ProtectedCell,      // tech upgrade or gate cell
    DeactivatedTech,    // deactivated tech upgrade cell, in a network game
    ConstructionListed, // BuildingConstructBegin: the construction list already holds the cell
};

// What ypaworld_func148 decides on, as one process's world shows it.
struct Facts
{
    bool place = false;             // ypaworld_arg148::field_C: put the finished building down
                                    // at once (level setup, gates, saves), no construction
    bool multiBuild = false;        // the level lets an owner run several constructions
    bool ownerBuilding = false;     // the owner has a construction running
    bool netGame = false;
    bool border = false;            // the cell is on the map border
    bool underConstruction = false; // cellArea::PT_CONSTRUCTING
    bool occupied = false;          // a host station or this process's own unit is in the cell
    bool protectedCell = false;     // PT_TECHUPGRADE, PT_GATECLOSED or PT_GATEOPENED
    bool deactivatedTech = false;   // PT_TECHDEACTIVE
};

// The checks ypaworld_func148 makes, in its order, before it changes
// anything. A placement skips the owner, occupancy and protected-cell checks;
// the border and a running construction stop it too. Sector ownership is not
// checked at all.
inline Refusal Check(const Facts &f)
{
    if (!f.place && !f.multiBuild && f.ownerBuilding)
        return Refusal::OwnerBusy;
    if (f.border)
        return Refusal::BorderCell;
    if (f.underConstruction)
        return Refusal::UnderConstruction;
    if (f.occupied && !f.place)
        return Refusal::Occupied;
    if (f.protectedCell && !f.place)
        return Refusal::ProtectedCell;
    if (f.deactivatedTech && f.netGame)
        return Refusal::DeactivatedTech;
    return Refusal::None;
}

inline const char *Describe(Refusal r)
{
    switch (r)
    {
    case Refusal::None:               return "started";
    case Refusal::OwnerBusy:          return "the owner's previous construction is still running";
    case Refusal::BorderCell:         return "the cell is on the map border";
    case Refusal::UnderConstruction:  return "the cell is under construction already";
    case Refusal::Occupied:           return "a host station or the local player's unit is in the cell";
    case Refusal::ProtectedCell:      return "the cell is a tech upgrade or a gate";
    case Refusal::DeactivatedTech:    return "the cell is a deactivated tech upgrade";
    case Refusal::ConstructionListed: return "the construction list already holds the cell";
    }
    return "unknown refusal";
}

} }
#endif
