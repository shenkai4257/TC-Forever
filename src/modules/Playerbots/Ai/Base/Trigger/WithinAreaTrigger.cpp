/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WithinAreaTrigger.h"
#include "LastMovementValue.h"
#include "Playerbots.h"

bool WithinAreaTrigger::IsActive()
{
    LastMovement& movement = context->GetValue<LastMovement&>("last area trigger")->Get();
    if (!movement.lastAreaTrigger)
        return false;

    // 12.x keeps the trigger geometry in the DB2 store and the teleport target in the
    // area-trigger teleport table.
    AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(movement.lastAreaTrigger);
    if (!at)
        return false;

    if (!sObjectMgr->GetAreaTrigger(movement.lastAreaTrigger))
        return false;

    return IsPointInAreaTriggerZone(at, bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                    0.5f);
}

bool WithinAreaTrigger::IsPointInAreaTriggerZone(AreaTriggerEntry const* atEntry, uint32 mapid, float x, float y, float z,
                                                 float delta)
{
    if (mapid != atEntry->ContinentID)
        return false;

    if (atEntry->Radius > 0)
    {
        // if we have radius check it
        float dist2 = (x - atEntry->Pos.X) * (x - atEntry->Pos.X) + (y - atEntry->Pos.Y) * (y - atEntry->Pos.Y) +
                      (z - atEntry->Pos.Z) * (z - atEntry->Pos.Z);
        if (dist2 > (atEntry->Radius + delta) * (atEntry->Radius + delta))
            return false;
    }
    else
    {
        // we have only extent

        // rotate the players position instead of rotating the whole cube, that way we can make a simplified
        // is-in-cube check and we have to calculate only one point instead of 4

        // 2PI = 360, keep in mind that ingame orientation is counter-clockwise
        double rotation = 2 * M_PI - atEntry->BoxYaw;
        double sinVal = sin(rotation);
        double cosVal = cos(rotation);

        float playerBoxDistX = x - atEntry->Pos.X;
        float playerBoxDistY = y - atEntry->Pos.Y;

        float rotPlayerX = float(atEntry->Pos.X + playerBoxDistX * cosVal - playerBoxDistY * sinVal);
        float rotPlayerY = float(atEntry->Pos.Y + playerBoxDistY * cosVal + playerBoxDistX * sinVal);

        // box edges are parallel to coordiante axis, so we can treat every dimension independently :D
        float dz = z - atEntry->Pos.Z;
        float dx = rotPlayerX - atEntry->Pos.X;
        float dy = rotPlayerY - atEntry->Pos.Y;
        if ((fabs(dx) > atEntry->BoxLength / 2 + delta) || (fabs(dy) > atEntry->BoxWidth / 2 + delta) ||
            (fabs(dz) > atEntry->BoxHeight / 2 + delta))
        {
            return false;
        }
    }

    return true;
}
