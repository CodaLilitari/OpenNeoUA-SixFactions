#ifndef WORLD_BUILD_COMPLETE_H_INCLUDED
#define WORLD_BUILD_COMPLETE_H_INCLUDED

namespace World { namespace BuildComplete {

// Whether this process destroys a unit that a building covers when its
// construction completes here (NC_STACK_ypaworld::DestroyUnitsCoveredByBuilding).
//
// Outside a network game every unit is this process's own. In a network game
// every process that accepted the building's UAMSG_STARTBUILD runs the
// construction on its own copy of the world and completes it, the builder
// first. A unit destroyed there has no attacker, so it dies on the spot and
// its UAMSG_DEAD goes to every peer, and every receiver applies a DEAD, the
// unit's own process included. So each process destroys only its own units,
// the rule DestroyAllGunsInSector keeps when a construction starts, and the
// owner's DEAD is the one the others apply. In particular a peer leaves alone
// the builder's guns, which reach it by UAMSG_BUILDINGVHCL when the builder's
// construction completes, often before its own copy does. A unit of no seat
// (owner 0) is no process's own.
inline bool DestroysCoveredUnit(bool netGame, int unitOwner, int localOwner)
{
    return !netGame || unitOwner == localOwner;
}

} }
#endif
