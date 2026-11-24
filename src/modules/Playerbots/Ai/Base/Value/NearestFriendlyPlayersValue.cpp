/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NearestFriendlyPlayersValue.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"

void NearestFriendlyPlayersValue::FindUnits(std::list<Unit*>& targets)
{
    Trinity::AnyFriendlyUnitInObjectRangeCheck u_check(bot, bot, range);
    Trinity::UnitListSearcher<Trinity::AnyFriendlyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestFriendlyPlayersValue::AcceptUnit(Unit* unit)
{
    ObjectGuid guid = unit->GetGUID();
    return guid.IsPlayer() && guid != botAI->GetBot()->GetGUID();
}
