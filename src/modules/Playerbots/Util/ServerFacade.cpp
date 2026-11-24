/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ServerFacade.h"
#include "Player.h"
#include "ChaseMovementGenerator.h"

float ServerFacade::GetDistance2d(Unit* unit, WorldObject* wo)
{
    ASSERT_NOTNULL(unit);
    ASSERT_NOTNULL(wo);

    float dist = unit->GetDistance2d(wo);
    return std::round(dist * 10.0f) / 10.0f;
}

float ServerFacade::GetDistance2d(Unit* unit, float x, float y)
{
    float dist = unit->GetDistance2d(x, y);
    return std::round(dist * 10.0f) / 10.0f;
}

bool ServerFacade::IsDistanceLessThan(float dist1, float dist2)
{
    // return dist1 - dist2 < sPlayerbotAIConfig.TargetPosRecalcDistance;
    return dist1 < dist2;
}

bool ServerFacade::IsDistanceGreaterThan(float dist1, float dist2)
{
    // return dist1 - dist2 > sPlayerbotAIConfig.TargetPosRecalcDistance;
    return dist1 > dist2;
}

bool ServerFacade::IsDistanceGreaterOrEqualThan(float dist1, float dist2) { return !IsDistanceLessThan(dist1, dist2); }

bool ServerFacade::IsDistanceLessOrEqualThan(float dist1, float dist2) { return !IsDistanceGreaterThan(dist1, dist2); }

void ServerFacade::SetFacingTo(Player* bot, WorldObject* wo, bool force)
{
    if (!bot)
        return;

    float angle = bot->GetAbsoluteAngle(wo);

    // 12.x has no SendMovementFlagUpdate(): a plain SetOrientation only changes the server-side
    // field, and the observer clients keep drawing the old heading (the bot's melee arc is right,
    // so AttackAction stops asking, and the model stays turned away for good). A stopped unit gets
    // a face spline instead -- MoveFace sends the move packet the clients actually turn on. A
    // moving unit leaves the heading to its own movement packet (a face spline would cut the
    // chase), so only the field is written there.
    if (!force && bot->isMoving())
        bot->SetOrientation(angle);
    else
    {
        // Write the field first so the melee arc is already right on the next AI check (otherwise
        // AttackAction keeps asking until the face spline lands), then send the spline the clients
        // turn on.
        bot->SetOrientation(angle);
        bot->SetFacingTo(angle, /*force*/ false);
    }
}

Unit* ServerFacade::GetChaseTarget(Unit* target)
{
    MovementGenerator* movementGen = target->GetMotionMaster()->GetCurrentMovementGenerator();
    if (movementGen && movementGen->GetMovementGeneratorType() == CHASE_MOTION_TYPE)
        return static_cast<ChaseMovementGenerator const*>(movementGen)->GetTarget();

    return nullptr;
}

void ServerFacade::SendPacket(Player* player, WorldPacket* packet)
{
    player->GetSession()->SendPacket(packet);
}
