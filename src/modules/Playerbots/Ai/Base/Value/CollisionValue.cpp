/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CollisionValue.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "ServerFacade.h"

bool CollisionValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);
    if (!target)
        return false;

    std::list<Unit*> targets;
    float range = sPlayerbotAIConfig.ContactDistance;
    Trinity::AnyUnitInObjectRangeCheck u_check(bot, range);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);

    for (Unit* target : targets)
    {
        if (bot == target)
            continue;

        float dist = ServerFacade::instance().GetDistance2d(bot, target->GetPositionX(), target->GetPositionY());
        if (ServerFacade::instance().IsDistanceLessThan(dist, target->GetCombatReach()))
            return true;
    }

    return false;
}
