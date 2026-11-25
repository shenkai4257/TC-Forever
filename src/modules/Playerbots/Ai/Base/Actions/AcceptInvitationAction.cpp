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

    // 12.x handlers take structured packets instead of a forged client packet.
    WorldPacket payload(CMSG_PARTY_INVITE_RESPONSE, 1);
    WorldPackets::Party::PartyInviteResponse accept(std::move(payload));
    accept.Accept = true;
    bot->GetSession()->HandlePartyInviteResponseOpcode(accept);

    if (!bot->GetGroup() || !bot->GetGroup()->IsMember(inviter->GetGUID()))
        return false;

    if (sRandomPlayerbotMgr.IsRandomBot(bot))
        botAI->SetMaster(inviter);
    // else
    // PlayerbotRepository::instance().Save(botAI);

    botAI->ResetStrategies();
    botAI->ChangeStrategy("+follow", BOT_STATE_NON_COMBAT);
    botAI->Reset();

    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault("hello", "Hello", {}));

    if (sPlayerbotAIConfig.SummonWhenGroup && bot->GetDistance(inviter) > sPlayerbotAIConfig.SightDistance)
    {
        Teleport(inviter, bot, true);
    }
    return true;
}
