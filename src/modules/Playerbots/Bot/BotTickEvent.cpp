/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BotTickEvent.h"

#include "Playerbots.h"

namespace
{
thread_local ObjectGuid tickingBotGuid;
}

bool BotTickEvent::IsTickingPlayer(ObjectGuid guid) { return !guid.IsEmpty() && tickingBotGuid == guid; }

bool BotTickEvent::Execute(uint64 /*eTime*/, uint32 pTime)
{
    // Mark this player for the duration of the tick; LogoutPlayerBot checks it and defers a logout
    // requested from here (see IsTickingPlayer).
    ObjectGuid const previousTicking = tickingBotGuid;
    tickingBotGuid = _bot->GetGUID();

    // Same order the PlayerScript::OnAfterUpdate hook used: the bot AI first, then the manager a
    // real player owns. Both are looked up by guid, so a recreated instance is picked up here.
    if (PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(_bot))
        botAI->UpdateAI(pTime);

    if (PlayerbotMgr* playerbotMgr = PlayerbotsMgr::instance().GetPlayerbotMgr(_bot))
        playerbotMgr->UpdateAI(pTime);

    // Re-arm for the next player update (one tick later), then let EventProcessor drop the handle
    // only: returning false means "do not delete", and this pointer was already removed from the
    // queue before Execute ran.
    tickingBotGuid = previousTicking;

    _bot->m_Events.AddEvent(this, _bot->m_Events.CalculateTime(Milliseconds(1)), false);
    return false;
}

void BotTickEvent::Install(Player* bot)
{
    if (!bot)
        return;

    for (auto const& [execTime, event] : bot->m_Events.GetEvents())
        if (dynamic_cast<BotTickEvent*>(event))
            return;

    bot->m_Events.AddEvent(new BotTickEvent(bot), bot->m_Events.CalculateTime(Milliseconds(0)));
}
