/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MapHelper.h"
#include "GridDefines.h"
#include "Map.h"
#include "Object.h"
#include "ObjectDefines.h"
#include "PathGenerator.h"
#include "Unit.h"

#include <cmath>

bool PlayerbotGeo::CheckCollisionAndGetValidCoords(Map* map, WorldObject const* source, float startX, float startY,
                                                   float startZ, float& destX, float& destY, float& destZ,
                                                   bool failOnCollision)
{
    if (!Trinity::IsValidMapCoord(startX, startY, startZ) || !Trinity::IsValidMapCoord(destX, destY, destZ))
        return false;

    bool isWaterNext = map->IsInWater(source->GetPhaseShift(), destX, destY, destZ);

    PathGenerator path(source);

    // Use a detour raycast to get our first collision point
    path.SetUseRaycast(true);
    bool result = path.CalculatePath(startX, startY, startZ, destX, destY, destZ, false);

    Unit const* unit = source->ToUnit();
    bool notOnGround = (path.GetPathType() & PATHFIND_NOT_USING_PATH) != 0 || isWaterNext || (unit && unit->IsFlying());

    // Check for valid path types before we proceed
    if (!result ||
        (!notOnGround && (path.GetPathType() & ~(PATHFIND_NORMAL | PATHFIND_SHORTCUT | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY_END))))
        return false;

    G3D::Vector3 const& endPos = path.GetActualEndPosition();
    destX = endPos.x;
    destY = endPos.y;
    destZ = endPos.z;

    // Check dynamic collision (game objects); the static tree is already covered by the raycast.
    float halfHeight = source->GetCollisionHeight() * 0.5f;
    bool collided = map->getObjectHitPos(source->GetPhaseShift(), startX, startY, startZ + halfHeight, destX, destY,
                                         destZ + halfHeight, destX, destY, destZ, -CONTACT_DISTANCE);
    destZ -= halfHeight;

    float groundZ = INVALID_HEIGHT;
    source->UpdateAllowedPositionZ(destX, destY, destZ, &groundZ);

    // Position has no ground under it (or is too far away)
    if (groundZ <= INVALID_HEIGHT && unit && !unit->CanFly())
    {
        // Fall back to grid height if any
        float gridHeight = map->GetGridHeight(source->GetPhaseShift(), destX, destY);
        if (gridHeight > INVALID_HEIGHT)
            destZ = gridHeight + unit->GetHoverOffset();
        else
            return false;
    }

    return !failOnCollision || !collided;
}

void PlayerbotGeo::RelocatePolarOffset(Position& pos, float angle, float dist, float z)
{
    pos.SetOrientation(pos.GetOrientation() + angle);
    pos.Relocate(pos.GetPositionX() + dist * std::cos(pos.GetOrientation()),
                 pos.GetPositionY() + dist * std::sin(pos.GetOrientation()), pos.GetPositionZ() + z);
}
