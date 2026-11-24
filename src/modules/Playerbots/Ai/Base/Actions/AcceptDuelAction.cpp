/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptDuelAction.h"
#include "Event.h"
#include "Playerbots.h"

bool AcceptDuelAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());

    ObjectGuid flagGuid;
    p >> flagGuid;
    ObjectGuid playerGuid;
    p >> playerGuid;

    // Do not auto duel with low health
    if ((!botAI->HasGameClientMaster() ||
        (botAI->GetMaster() && botAI->GetMaster()->GetGUID() != playerGuid)) &&
        AI_VALUE2(uint8, "health", "self target") < 90)
    {
        bot->GetSession()->HandleDuelCancelled();
        return false;
    }

    // 12.x handlers take structured arguments instead of a forged client packet.
    bot->GetSession()->HandleDuelAccepted(flagGuid);

    botAI->ResetStrategies();
    return true;
}
