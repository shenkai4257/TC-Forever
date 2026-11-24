/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotPlayerState.h"
#include "Player.h"

#include <unordered_map>

namespace PlayerbotState
{
namespace
{
    std::unordered_map<ObjectGuid::LowType, ObjectGuid> s_questDividers;
}

ObjectGuid GetQuestDivider(Player const* player)
{
    if (!player)
        return ObjectGuid::Empty;

    auto itr = s_questDividers.find(player->GetGUID().GetCounter());
    return itr != s_questDividers.end() ? itr->second : ObjectGuid::Empty;
}

void SetQuestDivider(Player const* player, ObjectGuid guid)
{
    if (!player)
        return;

    if (guid.IsEmpty())
        s_questDividers.erase(player->GetGUID().GetCounter());
    else
        s_questDividers[player->GetGUID().GetCounter()] = guid;
}
}
