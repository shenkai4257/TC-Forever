/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptInvitationAction.h"
#include "PartyPackets.h"
#include "Event.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "WorldPacket.h"

bool AcceptInvitationAction::Execute(Event /*event*/)
{
    Group* grp = bot->GetGroupInvite();
    if (!grp)
        return false;
    Player* inviter = ObjectAccessor::FindPlayer(grp->GetLeaderGUID());
    if (!inviter)
        return false;

    if (!botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter))
    {
        // 12.x sends the decliner's name with a 9 bit length prefix (WorldPackets::Party::GroupDecline)
        WorldPackets::Party::GroupDecline decline(bot->GetName());
        inviter->SendDirectMessage(decline.Write());
        bot->UninviteFromGroup();
        return false;
    }

    if (bot->isAFK() && !IsSelfBot(bot))
        bot->ToggleAFK();

    // Accept and everything that must observe its result (group membership, master assignment,
    // strategy reset, follow, summon) form one world-thread step: the group is created on the world
    // thread and the inviter may sit on any other map. The AI and the summon helper are resolved
    // fresh inside - this action object is gone by the time the lambda runs.
    ObjectGuid const inviterGuid = inviter->GetGUID();
    bool const randomBot = sRandomPlayerbotMgr.IsRandomBot(bot);
    botAI->RunOnWorldThread("party invite accept", [inviterGuid, randomBot](Player* bot)
    {
        WorldPacket payload(CMSG_PARTY_INVITE_RESPONSE, 1);
        WorldPackets::Party::PartyInviteResponse accept(std::move(payload));
        accept.Accept = true;
        bot->GetSession()->HandlePartyInviteResponseOpcode(accept);

        PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
        Player* inviter = ObjectAccessor::FindConnectedPlayer(inviterGuid);
        if (!ai || !inviter || !bot->GetGroup() || !bot->GetGroup()->IsMember(inviterGuid))
            return;

        if (randomBot)
            ai->SetMaster(inviter);

        ai->ResetStrategies();
        ai->ChangeStrategy("+follow", BOT_STATE_NON_COMBAT);
        ai->Reset();

        ai->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault("hello", "Hello", {}));

        if (sPlayerbotAIConfig.SummonWhenGroup && bot->GetDistance(inviter) > sPlayerbotAIConfig.SightDistance)
        {
            // The teleport helper is stateless besides its AI: a throwaway instance avoids holding
            // on to this (long gone) action object.
            SummonAction(ai, "accept invitation summon").Teleport(inviter, bot, true);
        }
    });
    return true;
}
