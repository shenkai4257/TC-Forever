/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptResurrectAction.h"
#include "MiscPackets.h"
#include "Event.h"
#include "Playerbots.h"

bool AcceptResurrectAction::Execute(Event event)
{
    if (bot->IsAlive())
        return false;

    WorldPacket p(event.getPacket());
    p.rpos(0);
    ObjectGuid guid;
    p >> guid;

    botAI->RunOnWorldThread("resurrect accept", [resurrector = guid](Player* bot) {
        WorldPacket payload(CMSG_RESURRECT_RESPONSE, 9);
        WorldPackets::Misc::ResurrectResponse response(std::move(payload));
        response.Resurrecter = resurrector;
        response.Response = 1;                                      // accept
        bot->GetSession()->HandleResurrectResponse(response);
    });

    return true;
}
