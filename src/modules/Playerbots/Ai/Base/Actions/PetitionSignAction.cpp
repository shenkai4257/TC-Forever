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

    QueryResult result =
        CharacterDatabase.PQuery("SELECT `type` FROM `petition` WHERE `petitionguid` = {}", petitionGuid.GetCounter());
    if (!result)
        return false;

    Field* fields = result->Fetch();
    uint32 type = PlayerbotFieldGet<uint32>(fields[0]);

    // 1.60 has no arena and no arena teams: only guild petitions (type 9) are handled.
    if (type != 9)
        return false;

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
        WorldPacket rawDecline(CMSG_DECLINE_PETITION);
        WorldPackets::Petition::DeclinePetition decline(std::move(rawDecline));
        decline.PetitionGUID = petitionGuid;
        bot->GetSession()->HandleDeclinePetition(decline);
        TC_LOG_INFO("playerbots", "Bot {} declines guild invite", bot->GetGUID().ToString().c_str(), bot->GetName().c_str());
        return false;
    }

    WorldPacket rawSign(CMSG_SIGN_PETITION);
    WorldPackets::Petition::SignPetition sign(std::move(rawSign));
    sign.PetitionGUID = petitionGuid;
    sign.Choice = 0;
    bot->GetSession()->HandleSignPetition(sign);
    bot->Say(PlayerbotTextMgr::instance().GetBotText("chat_guild_invite_thanks", {}), LANG_UNIVERSAL);
    TC_LOG_INFO("playerbots", "Bot {} accepts guild invite", bot->GetGUID().ToString().c_str(), bot->GetName().c_str());
    return true;
}
