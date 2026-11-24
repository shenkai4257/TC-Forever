/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERSTATE_H
#define PLAYERBOTS_PLAYERSTATE_H

#include "ObjectGuid.h"

class Player;

// AC keeps a per-player "divider" guid (the NPC or player a quest dialogue is
// currently with). 12.x has no such field, so the ported module keeps it here.
// Only the world thread touches it.
namespace PlayerbotState
{
    ObjectGuid GetQuestDivider(Player const* player);
    void SetQuestDivider(Player const* player, ObjectGuid guid);
}

#endif
