/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AreaTriggerAction.h"
#include "AreaTriggerPackets.h"
#include "DB2Stores.h"
#include "Event.h"
#include "LastMovementValue.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "Transport.h"

namespace
{
    // 12.x keeps two tables: areatrigger_teleport (ObjectMgr::GetAreaTrigger,
    // a WorldSafeLocsEntry - the destination) and the DB2 AreaTriggerEntry
    // store (the trigger volume the client reports). AC's single
    // GetAreaTriggerTeleport test becomes "is it in the teleport table".
    AreaTriggerEntry const* FindAreaTriggerVolume(uint32 triggerId)
    {
        return sAreaTriggerStore.LookupEntry(triggerId);
    }

    void SendAreaTriggerToServer(Player* bot, uint32 triggerId)
    {
        WorldPacket payload(CMSG_AREA_TRIGGER, 4);
        WorldPackets::AreaTrigger::AreaTrigger packet(std::move(payload));
        packet.AreaTriggerID = int32(triggerId);
        packet.Entered = true;
        packet.FromClient = true;
        bot->GetSession()->HandleAreaTriggerOpcode(packet);
    }
}

bool ReachAreaTriggerAction::Execute(Event event)
{
    if (IsSelfBot(bot))  // SelfBots reach their own area triggers through the client.
        return false;

    uint32 triggerId;
    WorldPacket p(event.getPacket());
    p.rpos(0);
    p >> triggerId;

    AreaTriggerEntry const* at = FindAreaTriggerVolume(triggerId);
    if (!at)
        return false;

    if (!sObjectMgr->GetAreaTrigger(triggerId))
    {
        SendAreaTriggerToServer(bot, triggerId);
        return true;
    }

    if (bot->GetMapId() != at->ContinentID)
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "area_trigger_follow_too_far_error", "I won't follow: too far away", {}));
        return true;
    }

    bot->GetMotionMaster()->MovePoint(
        /*id*/ triggerId,
        /*coords*/ at->Pos.X, at->Pos.Y, at->Pos.Z,
        /*generatePath*/ true);      // true => terrain path (2d mmap); false => straight spline (3d vmap)

    float distance = bot->GetDistance(at->Pos.X, at->Pos.Y, at->Pos.Z);
    float delay = IN_MILLISECONDS * distance / bot->GetSpeed(MOVE_RUN) + sPlayerbotAIConfig.ReactDelay;
    botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "area_trigger_wait_for_me", "Wait for me", {}));
    botAI->SetNextCheckDelay(delay);
    context->GetValue<LastMovement&>("last area trigger")->Get().lastAreaTrigger = triggerId;

    return true;
}

bool UseAreaTriggerAction::Execute(Event /*event*/)
{
    LastMovement& movement = context->GetValue<LastMovement&>("last area trigger")->Get();
    uint32 triggerId = movement.lastAreaTrigger;
    movement.lastAreaTrigger = 0;

    if (!triggerId)
        return true;

    if (!sObjectMgr->GetAreaTrigger(triggerId))
        return false;

    SendAreaTriggerToServer(bot, triggerId);

    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault("hello", "Hello", {}));
    return true;
}
