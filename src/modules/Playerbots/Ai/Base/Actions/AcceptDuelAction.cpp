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
        botAI->RunOnWorldThread("duel cancel", [] (Player* bot) {
            bot->GetSession()->HandleDuelCancelled();
        });
        return false;
    }

    // CMSG_DUEL_RESPONSE is PROCESS_THREADUNSAFE (these two are its internal entry points),
    // so the accept runs in the world tick window like the other player-interaction handlers.
    botAI->RunOnWorldThread("duel accept", [flagGuid] (Player* bot) {
        bot->GetSession()->HandleDuelAccepted(flagGuid);
    });

    botAI->ResetStrategies();
    return true;
}
