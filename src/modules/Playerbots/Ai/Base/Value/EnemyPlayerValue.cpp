/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "EnemyPlayerValue.h"
#include "CombatManager.h"
#include "InvalidTargetValue.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "Vehicle.h"

bool NearestEnemyPlayersValue::AcceptUnit(Unit* unit)
{
    // Apply parent's filtering first (includes level difference checks)
    if (!PossibleTargetsValue::AcceptUnit(unit))
        return false;

    bool inCannon = botAI->IsInVehicle(false, true);
    Player* enemy = dynamic_cast<Player*>(unit);
    if (enemy && botAI->IsOpposing(enemy) && enemy->IsPvP() &&
        !sPlayerbotAIConfig.IsPvpProhibited(enemy->GetZoneId(), enemy->GetAreaId()) &&
        !enemy->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NON_ATTACKABLE_2) &&
        ((inCannon || !enemy->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE))) &&
        /*!enemy->HasStealthAura() && !enemy->HasInvisibilityAura()*/ (bot->InBattleground() ? bot->CanSeeOrDetect(enemy) : enemy->CanSeeOrDetect(bot)) &&
        !(enemy->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION)))
    {
        // If with master, only attack if master is PvP flagged
        Player* master = botAI->GetMaster();
        if (master && !master->IsPvP() && !master->IsFFAPvP())
            return false;

        return true;
    }

    return false;
}

Unit* EnemyPlayerValue::Calculate()
{
    bool controllingCannon = false;
    bool controllingVehicle = false;
    bool passenger = false;
    if (Vehicle* vehicle = bot->GetVehicle())
    {
        VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(bot);
        if (!seat || (!seat->HasFlag(VEHICLE_SEAT_FLAG_CAN_CONTROL) && !(seat->Flags & VEHICLE_SEAT_FLAG_CAN_ATTACK)))
            return nullptr;  // a passenger seat that can't attack
        if (!seat->HasFlag(VEHICLE_SEAT_FLAG_CAN_CONTROL))
            passenger = true;  // fights from its seat: it can't close in
        else if (botAI->IsInVehicle(false, false, false, false, true))
            controllingCannon = true;
        else
            controllingVehicle = true;
    }

    // 1. Check units we are currently in PvP combat with.
    std::vector<Unit*> targets;
    Unit* pVictim = bot->GetVictim();
    for (auto const& [guid, combatRef] : bot->GetCombatManager().GetPvPCombatRefs())
    {
        Unit* pTarget = combatRef->GetOther(bot);
        if (IsUnavailableCombatTarget(bot, pTarget))
            continue;
        if (pTarget == pVictim || !pTarget->IsPlayer() ||
            !(bot->InBattleground() ? bot->CanSeeOrDetect(pTarget) : pTarget->CanSeeOrDetect(bot)) ||
            !bot->IsWithinDist(pTarget, VISIBILITY_DISTANCE_NORMAL))
            continue;

        targets.push_back(pTarget);
    }

    if (!targets.empty())
    {
        std::sort(targets.begin(), targets.end(),
                  [&](Unit const* pUnit1, Unit const* pUnit2)
                  { return bot->GetDistance(pUnit1) < bot->GetDistance(pUnit2); });

        return *targets.begin();
    }

    // 2. Find enemy player in range.

    GuidVector players = AI_VALUE(GuidVector, "nearest enemy players");
    float const maxAggroDistance = 60.0f;
    for (auto const& gTarget : players)
    {
        Unit* pUnit = botAI->GetUnit(gTarget);
        if (!pUnit || IsUnavailableCombatTarget(bot, pUnit))
            continue;

        Player* pTarget = dynamic_cast<Player*>(pUnit);
        if (!pTarget)
            continue;

        if (pTarget == pVictim)
            continue;

        // Aggro weak enemies from further away.
        // If controlling mobile vehicle only agro close enemies (otherwise will never reach objective)
        uint32 const aggroDistance = passenger                                                        ? 30.0f
                                     : controllingVehicle                                             ? 5.0f
                                     : (controllingCannon || bot->GetHealth() > pTarget->GetHealth()) ? maxAggroDistance
                                                                                                      : 20.0f;
        if (!bot->IsWithinDist(pTarget, aggroDistance))
            continue;

        if (bot->IsWithinLOSInMap(pTarget) &&
            (controllingCannon || (fabs(bot->GetPositionZ() - pTarget->GetPositionZ()) < 30.0f)))
            return pTarget;
    }

    // 3. Check party attackers.

    if (Group* pGroup = bot->GetGroup())
    {
        // 12.x exposes the group members through GroupRefManager (GroupReference range).
        for (GroupReference const& memberRef : pGroup->GetMembers())
        {
            if (Unit* pMember = memberRef.GetSource())
            {
                if (pMember == bot)
                    continue;

                if (ServerFacade::instance().GetDistance2d(bot, pMember) > 30.0f)
                    continue;

                if (Unit* pAttacker = pMember->getAttackerForHelper())
                    if (!IsUnavailableCombatTarget(bot, pAttacker) && pAttacker->IsPlayer() &&
                        bot->IsWithinDist(pAttacker, maxAggroDistance * 2.0f) && bot->IsWithinLOSInMap(pAttacker) &&
                        pAttacker != pVictim &&
                        (bot->InBattleground() ? bot->CanSeeOrDetect(pAttacker) : pAttacker->CanSeeOrDetect(bot)))
                        return pAttacker;
            }
        }
    }

    return nullptr;
}
