// World::BuildStart (src/world/build_start.h): the refusals of
// NC_STACK_ypaworld::ypaworld_func148, which a network peer also applies to a
// remote UAMSG_STARTBUILD.
#include "../src/world/build_start.h"
#include "require.h"

#include <cstring>
#include <iostream>
#include <set>
#include <string>

using World::BuildStart::Check;
using World::BuildStart::Describe;
using World::BuildStart::Facts;
using World::BuildStart::Refusal;

namespace
{

// cellArea::PTYPE (src/game/yw.h), which this test cannot include.
enum Purpose
{
    PT_NONE = 0, PT_CONSTRUCTING, PT_POWERSTATION, PT_BUILDINGS, PT_TECHUPGRADE,
    PT_GATECLOSED, PT_GATEOPENED, PT_TECHDEACTIVE, PT_STOUDSON, PURPOSE_COUNT
};

struct Inputs
{
    bool place, multiBuild, ownerBuilding, netGame, border, occupied;
    int purpose;
};

// ypaworld_func148 as it read before the refusals moved into
// World::BuildStart::Check, up to its first change to the world, with each of
// its refusals named: the first return it reaches gives the reason, and
// Refusal::None means it went on to start (or place) the building.
Refusal LegacyRefusal(const Inputs &in)
{
    if ( !in.place && !in.multiBuild && in.ownerBuilding )
        return Refusal::OwnerBusy;
    const bool UserInSec = in.occupied;
    if ( in.border )
        return Refusal::BorderCell;
    if ( in.purpose == PT_CONSTRUCTING )
        return Refusal::UnderConstruction;
    else if ( UserInSec && !in.place )
        return Refusal::Occupied;
    else if ( in.purpose == PT_POWERSTATION )
    {
        // PowerStationErase, then the building
    }
    else if ( (in.purpose == PT_TECHUPGRADE || in.purpose == PT_GATECLOSED ||
               in.purpose == PT_GATEOPENED) && !in.place )
        return Refusal::ProtectedCell;
    else if ( in.purpose == PT_TECHDEACTIVE && in.netGame )
        return Refusal::DeactivatedTech;
    return Refusal::None;
}

// The facts ypaworld_func148 now gathers for the same inputs.
Facts FactsOf(const Inputs &in)
{
    Facts f;
    f.place = in.place;
    f.multiBuild = in.multiBuild;
    f.ownerBuilding = in.ownerBuilding;
    f.netGame = in.netGame;
    f.border = in.border;
    f.underConstruction = in.purpose == PT_CONSTRUCTING;
    f.occupied = in.occupied;
    f.protectedCell = in.purpose == PT_TECHUPGRADE || in.purpose == PT_GATECLOSED ||
                      in.purpose == PT_GATEOPENED;
    f.deactivatedTech = in.purpose == PT_TECHDEACTIVE;
    return f;
}

Facts Construction()
{
    Facts f; // a remote STARTBUILD and every station build: field_C = 0
    f.netGame = true;
    return f;
}

} // namespace

int main()
{
    // Nothing in the way: a construction and a placement both start.
    Facts f = Construction();
    REQUIRE(Check(f) == Refusal::None);
    f.place = true;
    REQUIRE(Check(f) == Refusal::None);

    // One construction per owner at a time, unless the level allows more. A
    // placement (level setup, gates, saves) never waits for the owner.
    f = Construction();
    f.ownerBuilding = true;
    REQUIRE(Check(f) == Refusal::OwnerBusy);
    f.multiBuild = true;
    REQUIRE(Check(f) == Refusal::None);
    f.multiBuild = false;
    f.place = true;
    REQUIRE(Check(f) == Refusal::None);

    // The owner is checked before anything about the cell, which
    // ypaworld_func148 reads only afterwards.
    f = Construction();
    f.ownerBuilding = true;
    f.border = true;
    f.underConstruction = true;
    f.occupied = true;
    REQUIRE(Check(f) == Refusal::OwnerBusy);

    // The border and a running construction stop a placement too, the
    // border first.
    f = Construction();
    f.place = true;
    f.border = true;
    f.underConstruction = true;
    REQUIRE(Check(f) == Refusal::BorderCell);
    f.border = false;
    REQUIRE(Check(f) == Refusal::UnderConstruction);

    // A host station or this process's own unit in the cell stops a
    // construction only; that check reads this process's copy of the
    // positions, so peers can disagree on it.
    f = Construction();
    f.occupied = true;
    REQUIRE(Check(f) == Refusal::Occupied);
    f.place = true;
    REQUIRE(Check(f) == Refusal::None);

    // Occupancy comes after the border and a running construction: the
    // reason a peer logs names the cell before what stands in it.
    f = Construction();
    f.occupied = true;
    f.border = true;
    REQUIRE(Check(f) == Refusal::BorderCell);
    f.border = false;
    f.underConstruction = true;
    REQUIRE(Check(f) == Refusal::UnderConstruction);

    // Occupancy comes before the cell's purpose.
    f = Construction();
    f.occupied = true;
    f.protectedCell = true;
    REQUIRE(Check(f) == Refusal::Occupied);
    f.protectedCell = false;
    f.deactivatedTech = true;
    REQUIRE(Check(f) == Refusal::Occupied);

    // Tech upgrades and gates take only a placement.
    f = Construction();
    f.protectedCell = true;
    REQUIRE(Check(f) == Refusal::ProtectedCell);
    f.place = true;
    REQUIRE(Check(f) == Refusal::None);

    // A deactivated tech upgrade refuses in a network game only, a placement
    // as well.
    f = Construction();
    f.deactivatedTech = true;
    REQUIRE(Check(f) == Refusal::DeactivatedTech);
    f.place = true;
    REQUIRE(Check(f) == Refusal::DeactivatedTech);
    f.netGame = false;
    REQUIRE(Check(f) == Refusal::None);

    // Sector ownership is no fact at all: a peer whose copy of the owner of
    // the cell lags behind still starts the building.

    // For every input, Check refuses exactly when the code it replaced in
    // ypaworld_func148 refused, and for the reason of the return that code
    // reached first: the order of the checks decides the reason a peer logs.
    int compared = 0;
    int refused = 0;
    std::set<Refusal> reasons;
    for (int bits = 0; bits < (1 << 6); ++bits)
    {
        for (int purpose = PT_NONE; purpose < PURPOSE_COUNT; ++purpose)
        {
            const Inputs in{(bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0,
                            (bits & 8) != 0, (bits & 16) != 0, (bits & 32) != 0, purpose};
            const Refusal expected = LegacyRefusal(in);
            const Refusal got = Check(FactsOf(in));
            if (got != expected)
            {
                std::cerr << "inputs " << bits << " purpose " << purpose << ": Check gives \""
                          << Describe(got) << "\", the replaced code \"" << Describe(expected)
                          << "\"\n";
                Require(false, "Check disagrees with the ypaworld_func148 it replaced");
            }
            if (expected != Refusal::None)
                ++refused;
            reasons.insert(expected);
            ++compared;
        }
    }
    REQUIRE(compared == 64 * PURPOSE_COUNT);
    // The inputs reach every reason Check gives (ConstructionListed comes
    // from BuildingConstructBegin, after Check), and both outcomes.
    REQUIRE(reasons == std::set<Refusal>({Refusal::None, Refusal::OwnerBusy, Refusal::BorderCell,
                                          Refusal::UnderConstruction, Refusal::Occupied,
                                          Refusal::ProtectedCell, Refusal::DeactivatedTech}));
    REQUIRE(refused > 0 && refused < compared);

    // Every refusal reads as its own reason in the netlog.
    std::set<std::string> texts;
    for (Refusal r : {Refusal::OwnerBusy, Refusal::BorderCell, Refusal::UnderConstruction,
                      Refusal::Occupied, Refusal::ProtectedCell, Refusal::DeactivatedTech,
                      Refusal::ConstructionListed})
    {
        const char *text = Describe(r);
        REQUIRE(text != nullptr && std::strlen(text) > 0);
        REQUIRE(texts.insert(text).second);
    }
    REQUIRE(std::string(Describe(Refusal::None)) == "started");
    REQUIRE(texts.count("started") == 0);

    std::cout << "build start refusals and their reasons checked against the code they replaced ("
              << compared << " inputs, " << refused << " refused)\n";
    return 0;
}
