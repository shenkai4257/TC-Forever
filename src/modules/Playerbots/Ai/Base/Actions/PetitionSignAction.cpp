/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PetitionSignAction.h"
#include "FieldHelper.h"
#include "PetitionPackets.h"
#include "Event.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"

bool PetitionSignAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);
    ObjectGuid petitionGuid;
    ObjectGuid inviter;
    p >> petitionGuid >> inviter;

    // 1.60 has no arena teams: the only petition kind is a guild charter, and the 12.x petition
    // table has no type column at all. (The old "SELECT `type` ..." filter queried a nonexistent
    // column, the query always failed, and bots never signed a charter.)

    bool accept = true;

    if (bot->GetGuildId())
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotText("chat_guild_already_member", {}));
        accept = false;
    }

    if (bot->GetGuildIdInvited())
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotText("chat_guild_already_invited", {}));
        accept = false;
    }

    Player* _inviter = ObjectAccessor::FindPlayer(inviter);
    if (!_inviter)
        return false;

    if (_inviter == bot)
        return false;

    if (!accept || !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, _inviter, true))
    {
        botAI->RunOnWorldThread("petition decline", [petitionGuid](Player* bot) {
            WorldPacket rawDecline(CMSG_DECLINE_PETITION);
            WorldPackets::Petition::DeclinePetition decline(std::move(rawDecline));
            decline.PetitionGUID = petitionGuid;
            bot->GetSession()->HandleDeclinePetition(decline);
        });
        TC_LOG_INFO("playerbots", "Bot {} declines guild invite", bot->GetGUID().ToString().c_str(), bot->GetName().c_str());
        return false;
    }

    botAI->RunOnWorldThread("petition sign", [petitionGuid](Player* bot) {
        WorldPacket rawSign(CMSG_SIGN_PETITION);
        WorldPackets::Petition::SignPetition sign(std::move(rawSign));
        sign.PetitionGUID = petitionGuid;
        sign.Choice = 0;
        bot->GetSession()->HandleSignPetition(sign);
    });
    bot->Say(PlayerbotTextMgr::instance().GetBotText("chat_guild_invite_thanks", {}), LANG_UNIVERSAL);
    TC_LOG_INFO("playerbots", "Bot {} accepts guild invite", bot->GetGUID().ToString().c_str(), bot->GetName().c_str());
    return true;
}
