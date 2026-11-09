// Whether a squad is fit to fight (NC_STACK_ypabact::GetFightMotivation).
#ifndef WORLD_FIGHT_MOTIVATION_H_INCLUDED
#define WORLD_FIGHT_MOTIVATION_H_INCLUDED

namespace World
{

// Aggression 0 (Level 1) never fights and 100 (Level 5) always does; only the
// levels between consult the squad's energy, so only they need it summed.
inline bool FightMotivationUsesEnergy(int aggression)
{
    return aggression != 0 && aggression != 100;
}

// NC_STACK_yparobo::checkCommander asks this of each squad commander of a
// station: a squad that is not fit starts escaping to its station, and an
// escaping squad that is fit again stops escaping.
//
// energy and energyMax are the summed current and maximum energy of the
// squad's living units. For the levels that consult them, *ratio (when given)
// receives energy / energyMax, a zero maximum counting as 1.
//
// Retail rule, from the decompiled engine: an escaping squad is fit above half
// its maximum energy, any other squad above a fifth, so a squad that retreated
// regroups before it fights again. The ratio is a float and the thresholds are
// doubles, as there: one fifth rounds to 0.2f, which lies just above the
// double 0.2, so a squad at exactly a fifth is still fit.
inline bool SquadFightMotivation(int aggression, bool escaping, int energy, int energyMax, float *ratio)
{
    if ( aggression == 100 )
        return true;

    if ( aggression == 0 )
        return false;

    if ( energyMax == 0 )
        energyMax = 1;

    const float energyRatio = static_cast<float>(energy) / static_cast<float>(energyMax);

    if ( ratio )
        *ratio = energyRatio;

    if ( escaping )
        return energyRatio > 0.5;

    return energyRatio > 0.2;
}

}

#endif // WORLD_FIGHT_MOTIVATION_H_INCLUDED
