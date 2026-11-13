// World::BuildComplete (src/world/build_complete.h): which of the units a
// finished building covers each process of a match destroys.
#include "../src/world/build_complete.h"
#include "require.h"

#include <iostream>

using World::BuildComplete::DestroysCoveredUnit;

int main()
{
    // Outside a network game every unit is this process's own: a finished
    // building destroys whatever it covers, whoever owns it.
    for (int unitOwner = 0; unitOwner < 8; ++unitOwner)
        REQUIRE(DestroysCoveredUnit(false, unitOwner, 1));

    // In a six-seat match every process that accepted the STARTBUILD
    // completes the construction and finds the same units in the cell. Each
    // unit is destroyed by exactly one process, its owner's, whose
    // UAMSG_DEAD then reaches the others; no process destroys a unit it does
    // not own and sends a DEAD that the unit's own process would apply.
    const int seats[] = {1, 2, 3, 4, 5, 6};
    int pairs = 0;
    for (int unitOwner : seats)
    {
        int destroyers = 0;
        for (int process : seats)
        {
            if (DestroysCoveredUnit(true, unitOwner, process))
            {
                REQUIRE(process == unitOwner);
                ++destroyers;
            }
            ++pairs;
        }
        REQUIRE(destroyers == 1);
    }
    REQUIRE(pairs == 36);

    // A bot (owner 4) builds next to the host (owner 1). The bot's guns reach
    // the host by UAMSG_BUILDINGVHCL when the bot's construction completes,
    // often before the host's copy does, and stay alone there; the host's own
    // tank in the cell dies on the host, not by the bot's hand.
    const int bot = 4;
    const int host = 1;
    REQUIRE(!DestroysCoveredUnit(true, bot, host));
    REQUIRE(!DestroysCoveredUnit(true, host, bot));
    REQUIRE(DestroysCoveredUnit(true, host, host));

    // A unit of no seat (owner 0) is no process's own in a network game.
    for (int process : seats)
        REQUIRE(!DestroysCoveredUnit(true, 0, process));

    std::cout << "covered units destroyed by their own process only (" << pairs
              << " owner/process pairs)\n";
    return 0;
}
