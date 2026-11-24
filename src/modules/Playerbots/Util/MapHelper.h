/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MAPHELPER_H
#define PLAYERBOTS_MAPHELPER_H

#include "Position.h"

class Map;
class WorldObject;

/**
 * Core helpers the AC module used that the TrinityCore 12.x core does not expose.
 */
namespace PlayerbotGeo
{
    /**
     * Resolve a destination against the terrain and game objects, mirroring AC's
     * Map::CheckCollisionAndGetValidCoords (which 12.x does not have).
     *
     * Offsets `destX/destY/destZ` to the first collision point (or to the end of the
     * calculated path). Returns false when the destination cannot be used at all.
     * With `failOnCollision`, also returns false when a collider was hit.
     */
    bool CheckCollisionAndGetValidCoords(Map* map, WorldObject const* source, float startX, float startY, float startZ,
                                         float& destX, float& destY, float& destZ, bool failOnCollision = true);

    /**
     * Rotate by `angle` and move `dist` yards forward, mirroring AC's
     * Position::RelocatePolarOffset (not present in 12.x).
     */
    void RelocatePolarOffset(Position& pos, float angle, float dist, float z = 0.0f);
}

#endif
