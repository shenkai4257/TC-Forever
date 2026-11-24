/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptBattlegroundInvitationAction.h"
#include "BattlegroundPackets.h"
#include "Event.h"
#include "PlayerbotAI.h"

bool AcceptBgInvitationAction::Execute(Event /*event*/)
{
    // 12.x identifies the queued slot by index into the player's queue array and carries
    // a structured ticket; the handler re-points a stale slot at the invited queue itself,
    // so accepting with the first queued slot is enough.
    WorldPacket payload(CMSG_BATTLEFIELD_PORT, 4);
    WorldPackets::Battleground::BattlefieldPort port(std::move(payload));
    for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
    {
        if (bot->GetBattlegroundQueueTypeId(i) != BATTLEGROUND_QUEUE_NONE)
        {
            port.Ticket.Id = i;
            port.Ticket.RequesterGuid = bot->GetGUID();
            break;
        }
    }
    port.AcceptedInvite = true;
    bot->GetSession()->HandleBattleFieldPortOpcode(port);

    botAI->ResetStrategies();

    return true;
}
