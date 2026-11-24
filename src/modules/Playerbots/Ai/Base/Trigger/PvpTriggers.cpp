/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PvpTriggers.h"

#include "BattlegroundMgr.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "PvpValues.h"
#include "ServerFacade.h"
#include "Timer.h"

bool WsgSupportThreat::IsActive()
{
    // 12.x implements battleground flag logic in scripts; the WSG-specific state is not
    // reachable from the module, so this trigger stays inactive.
    return false;
}

namespace
{
// a game from the random queue records BATTLEGROUND_RB as the player's type: use the rolled map
BattlegroundTypeId RealBgType(Player* bot)
{
    BattlegroundTypeId bgType = bot->GetBattlegroundTypeId();
    if (bgType == BATTLEGROUND_RB && bot->GetBattleground())
        bgType = bot->GetBattleground()->GetTypeID();
    return bgType;
}
}  // namespace

bool EnemyPlayerNear::IsActive() { return AI_VALUE(Unit*, "enemy player target"); }

bool PlayerHasNoFlag::IsActive()
{
    // 12.x implements battleground flag logic in scripts; the WSG-specific state is not
    // reachable from the module, so this trigger stays inactive.
    return false;
}

bool PlayerIsInBattleground::IsActive() { return botAI->GetBot()->InBattleground(); }

bool BgWaitingTrigger::IsActive()
{
    if (bot->InBattleground())
    {
        if (bot->GetBattleground() && bot->GetBattleground()->GetStatus() == STATUS_WAIT_JOIN)
            return true;
    }

    return false;
}

bool BgActiveTrigger::IsActive()
{
    if (bot->InBattleground())
    {
        if (bot->GetBattleground() && bot->GetBattleground()->GetStatus() == STATUS_IN_PROGRESS)
            return true;
    }

    return false;
}

bool BgInviteActiveTrigger::IsActive()
{
    if (bot->InBattleground() || !bot->InBattlegroundQueue())
    {
        return false;
    }

    for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
    {
        BattlegroundQueueTypeId queueTypeId = bot->GetBattlegroundQueueTypeId(i);
        if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
            continue;

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(queueTypeId);

        GroupQueueInfo ginfo;
        if (bgQueue.GetPlayerGroupInfoData(bot->GetGUID(), &ginfo))
        {
            if (ginfo.IsInvitedToBGInstanceGUID && ginfo.RemoveInviteTime)
            {
                TC_LOG_INFO("playerbots", "Bot {} <{}> ({} {}) : Invited to BG but not in BG",
                         bot->GetGUID().ToString().c_str(), bot->GetName(), bot->GetLevel(),
                         bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H");
                return true;
            }
        }
    }

    return false;
}

bool InsideBGTrigger::IsActive() { return bot->InBattleground() && bot->GetBattleground(); }

bool PlayerIsInBattlegroundWithoutFlag::IsActive()
{
    // 12.x implements battleground flag logic in scripts; the WSG-specific state is not
    // reachable from the module, so this trigger stays inactive.
    return false;
}

bool PlayerHasFlag::IsActive()
{
    // 12.x implements battleground flag logic in scripts; the WSG-specific state is not
    // reachable from the module, so this trigger stays inactive.
    return false;
}

bool PlayerHasFlag::IsCapturingFlag(Player* bot)
{
    // 12.x implements battleground flag logic in scripts; the WSG-specific state is not
    // reachable from the module, so this trigger stays inactive.
    return false;
}

bool TeamHasFlag::IsActive()
{
    // 12.x implements battleground flag logic in scripts; the WSG-specific state is not
    // reachable from the module, so this trigger stays inactive.
    return false;
}

bool EnemyTeamHasFlag::IsActive()
{
    // 12.x implements battleground flag logic in scripts; the WSG-specific state is not
    // reachable from the module, so this trigger stays inactive.
    return false;
}

bool EnemyFlagCarrierNear::IsActive()
{
    // 12.x implements battleground flag logic in scripts; the WSG-specific state is not
    // reachable from the module, so this trigger stays inactive.
    return false;
}

bool WsgEscortSeparated::IsActive()
{
    // 12.x keeps WSG flag state inside the battleground scripts; not reachable here.
    return false;
}

bool PlayerWantsInBattlegroundTrigger::IsActive()
{
    if (bot->InBattleground())
        return false;

    if (bot->GetBattleground() && bot->GetBattleground()->GetStatus() == STATUS_WAIT_JOIN)
        return false;

    if (bot->GetBattleground() && bot->GetBattleground()->GetStatus() == STATUS_IN_PROGRESS)
        return false;

    if (bot->IsDeserter())
        return false;

    return true;
}

bool VehicleNearTrigger::IsActive()
{
    GuidVector npcs = AI_VALUE(GuidVector, "nearest vehicles");
    return npcs.size();
}

bool InVehicleTrigger::IsActive() { return botAI->IsInVehicle(); }

bool WsgFlagStateChanged::IsActive()
{
    // 12.x keeps WSG flag state inside the battleground scripts; not reachable here.
    return false;
}
