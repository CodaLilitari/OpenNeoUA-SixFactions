// Squad fight motivation and the retail retreat rule (src/world/fight_motivation.h).
#include "../src/world/fight_motivation.h"
#include "require.h"
#include <initializer_list>
#include <iostream>

using World::FightMotivationUsesEnergy;
using World::SquadFightMotivation;

// The decision at aggression 60, the level of the station AI's own squads.
static bool Fit(bool escaping, int energy, int energyMax)
{
    return SquadFightMotivation(60, escaping, energy, energyMax, nullptr);
}

int main()
{
    // A squad that is not escaping is fit above a fifth of its maximum energy.
    // At a fifth: the ratio is a float and the threshold the double 0.2, as in
    // the decompiled engine. One fifth rounds to 0.2f, which lies just above
    // the double 0.2, so that squad is fit; the float below 0.2f
    // (13421772 / 2^26, exact) is not above the threshold.
    REQUIRE(!Fit(false, 19999, 100000));
    REQUIRE(!Fit(false, 13421772, 67108864));
    REQUIRE(Fit(false, 20000, 100000));
    REQUIRE(Fit(false, 20001, 100000));
    REQUIRE(Fit(false, 50000, 100000));
    REQUIRE(Fit(false, 50001, 100000));

    // An escaping squad is fit only above half its maximum energy (retail).
    // Upstream 7bc426f flattened the condition so that the 0.2 rule also
    // passed escaping squads between a fifth and half; all three below did.
    REQUIRE(!Fit(true, 19999, 100000));
    REQUIRE(!Fit(true, 20000, 100000));
    REQUIRE(!Fit(true, 20001, 100000));
    REQUIRE(!Fit(true, 50000, 100000));
    REQUIRE(Fit(true, 50001, 100000));

    // The ratio is reported whether or not the squad is fit.
    float ratio = -1.0f;
    REQUIRE(!SquadFightMotivation(60, true, 30000, 100000, &ratio));
    REQUIRE(ratio == 0.3f);
    ratio = -1.0f;
    REQUIRE(SquadFightMotivation(60, false, 30000, 100000, &ratio));
    REQUIRE(ratio == 0.3f);

    // Level 5 (aggression 100) always fights and Level 1 (0) never does,
    // whatever the energy and the escape flag; neither reports a ratio.
    for (bool escaping : {false, true})
    {
        float untouched = -1.0f;
        REQUIRE(SquadFightMotivation(100, escaping, 0, 100000, &untouched));
        REQUIRE(SquadFightMotivation(100, escaping, 0, 0, &untouched));
        REQUIRE(!SquadFightMotivation(0, escaping, 100000, 100000, &untouched));
        REQUIRE(!SquadFightMotivation(0, escaping, 1, 0, &untouched));
        REQUIRE(untouched == -1.0f);
    }

    // The engine sums the squad's energy only where FightMotivationUsesEnergy
    // says so: exactly the levels whose decision reads it (and reports a ratio).
    REQUIRE(!FightMotivationUsesEnergy(0) && !FightMotivationUsesEnergy(100));
    for (int aggression = 0; aggression <= 255; ++aggression)
    {
        float reported = -1.0f;
        SquadFightMotivation(aggression, false, 30000, 100000, &reported);
        REQUIRE(FightMotivationUsesEnergy(aggression) == (reported == 0.3f));
    }

    // A zero maximum counts as 1 instead of dividing by zero.
    for (bool escaping : {false, true})
    {
        float zero = -1.0f;
        REQUIRE(!SquadFightMotivation(60, escaping, 0, 0, &zero));
        REQUIRE(zero == 0.0f);
        float one = -1.0f;
        REQUIRE(SquadFightMotivation(60, escaping, 1, 0, &one));
        REQUIRE(one == 1.0f);
    }

    std::cout << "fight motivation: retail escape and fight thresholds, Levels 1 and 5, zero maximum\n";
    return 0;
}
