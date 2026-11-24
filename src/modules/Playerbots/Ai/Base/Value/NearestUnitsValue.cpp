/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NearestUnitsValue.h"
#include "Playerbots.h"

GuidVector NearestUnitsValue::Calculate()
{
    std::list<Unit*> targets;
    FindUnits(targets);

    GuidVector results;
    uint32 friendlySeen = 0;
    for (Unit* unit : targets)
    {
        // Faction probe: a bot that sees every unit as friendly has a broken faction template
        // (FindUnits itself relies on IsFriendlyTo, so friendlySeen > 0 means data, not filtering).
        if (bot->IsFriendlyTo(unit))
            ++friendlySeen;

        if (AcceptUnit(unit) && (ignoreLos || bot->IsWithinLOSInMap(unit)))
            results.push_back(unit->GetGUID());
    }

    if (results.empty() && !targets.empty())
        TC_LOG_DEBUG("playerbots", "[nearest-diag] {} {} ({:.0f}yd): searched {} units, accepted 0, friendly-seen {}",
                     bot->GetName(), getName(), range, targets.size(), friendlySeen);

    return results;
}
