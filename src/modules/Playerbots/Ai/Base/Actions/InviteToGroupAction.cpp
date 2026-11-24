/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * Ported from the CMaNGOS playerbots project (https://github.com/cmangos/playerbots), GPL v2,
 * with modifications for AzerothCore.
 * Original authors:
 *   Sebastiaan Keek (mostlikely4r) <sebastiaan.keek@gmail.com> - original author
 *   David Parra Ausina (Flekz) <davidparraausina@gmail.com>
 *   celguar <celguar@gmail.com>
 */

#include "InviteToGroupAction.h"
#include "BroadcastHelper.h"
#include "Event.h"
#include "GuildMgr.h"
#include "PlayerbotOperations.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "PartyPackets.h"
#include "ServerFacade.h"

bool InviteToGroupAction::Invite(Player* inviter, Player* player)
{
    if (!player)
        return false;

    if (inviter == player)
        return false;

    if (!GET_PLAYERBOT_AI(player) && !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, true, player))
        return false;

    if (Group* group = inviter->GetGroup())
    {
        if (GET_PLAYERBOT_AI(player) && !IsSelfBot(player))
            if (!group->isRaidGroup() && group->GetMembersCount() > 4)
            {
                auto convertOp = std::make_unique<GroupConvertToRaidOperation>(inviter->GetGUID());
                PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(convertOp));
            }
    }

    WorldPacket raw(CMSG_PARTY_INVITE);
    WorldPackets::Party::PartyInviteClient invite(std::move(raw));
    invite.TargetName = player->GetName();
    inviter->GetSession()->HandlePartyInviteOpcode(invite);

    return true;
}

bool InviteNearbyToGroupAction::Execute(Event /*event*/)
{
    GuidVector nearGuids = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest friendly players")->Get();
    for (auto& i : nearGuids)
    {
        Player* player = ObjectAccessor::FindPlayer(i);
        if (!player)
            continue;

        if (player == bot)
            continue;

        if (player->GetMapId() != bot->GetMapId())
            continue;

        if (player->GetGroup())
            continue;

        if (!PlayerbotAIConfig::Instance().RandomBotInvitePlayer && IsSelfBot(player))
            continue;

        Group* group = bot->GetGroup();

        if (player->isDND())
            continue;

        if (player->IsBeingTeleported())
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);

        if (botAI)
        {
            // A solo-grouper bot with no real master (regular player or SelfBot master) does not invite.
            if (botAI->GetGrouperType() == GrouperType::SOLO && !botAI->HasGameClientMaster())
                continue;

            if (IsRealPlayer(botAI->GetMaster()))  // An active player's altbot does not auto-invite.
                continue;
        }

        if (abs(int32(player->GetLevel() - bot->GetLevel())) > 2)
            continue;

        if (ServerFacade::instance().GetDistance2d(bot, player) > PlayerbotAIConfig::Instance().SightDistance)
            continue;

        // When inviting the 5th member of the group convert to raid for future invites.
        if (group && botAI->GetGrouperType() > GrouperType::LEADER_5 && !group->isRaidGroup() &&
            bot->GetGroup()->GetMembersCount() > 3)
        {
            auto convertOp = std::make_unique<GroupConvertToRaidOperation>(bot->GetGUID());
            PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(convertOp));
        }

        if (PlayerbotAIConfig::Instance().InviteChat && RandomPlayerbotMgr::instance().IsRandomBot(bot))
        {
            std::map<std::string, std::string> placeholders;
            placeholders["%player"] = player->GetName();

            if (group && group->isRaidGroup())
                bot->Say(PlayerbotTextMgr::instance().GetBotText("join_raid", placeholders),
                         (bot->GetTeamId() == TEAM_ALLIANCE ? LANG_COMMON : LANG_ORCISH));
            else
                bot->Say(PlayerbotTextMgr::instance().GetBotText("join_group", placeholders),
                         (bot->GetTeamId() == TEAM_ALLIANCE ? LANG_COMMON : LANG_ORCISH));
        }

        return Invite(bot, player);
    }

    return false;
}

bool InviteNearbyToGroupAction::isUseful()
{
    if (!PlayerbotAIConfig::Instance().RandomBotGroupNearby)
        return false;

    if (bot->InBattleground())
        return false;

    if (bot->InBattlegroundQueue())
        return false;

    GrouperType grouperType = botAI->GetGrouperType();

    if (grouperType == GrouperType::SOLO || grouperType == GrouperType::MEMBER)
        return false;

    Group* group = bot->GetGroup();

    if (group)
    {
        if (group->isRaidGroup() && group->IsFull())
            return false;

        if (botAI->GetGroupLeader() != bot)
            return false;

        uint32 memberCount = group->GetMembersCount();

        if (memberCount >= uint8(grouperType))
            return false;
    }

    if (IsRealPlayer(botAI->GetMaster()))  // An active player's altbot does not auto-invite.
        return false;

    return true;
}

std::vector<Player*> InviteGuildToGroupAction::getGuildMembers()
{
    Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());

    FindGuildMembers worker;
    guild->BroadcastWorker(worker);

    return worker.GetResult();
}

bool InviteGuildToGroupAction::Execute(Event /*event*/)
{
    for (auto& member : getGuildMembers())
    {
        Player* player = member;

        if (!player)
            continue;

        if (player == bot)
            continue;

        if (player->GetGroup())
            continue;

        if (player->isDND())
            continue;

        if (!PlayerbotAIConfig::Instance().RandomBotInvitePlayer && IsSelfBot(player))
            continue;

        if (player->IsBeingTeleported())
            continue;

        if (player->GetMapId() != bot->GetMapId() && player->GetLevel() < 30)
            continue;

        if (WorldPosition(player).distance(bot) > 1000 && player->GetLevel() < 15)
            continue;

        PlayerbotAI* playerAi = GET_PLAYERBOT_AI(player);

        if (playerAi)
        {
            // Do not invite solo players.
            if (playerAi->GetGrouperType() == GrouperType::SOLO && !playerAi->HasGameClientMaster())
                continue;

            if (IsRealPlayer(playerAi->GetMaster()))  // Do not invite bots that belong to an active player.
                continue;

            if (player->GetLevel() >
                bot->GetLevel() + 5)  // Invite higher levels that need money so they can grind money and help out.
            {
                if (!PAI_VALUE(bool, "should get money"))
                    continue;
            }
        }

        if (bot->GetLevel() >
            player->GetLevel() + 5)  // Do not invite members that too low level or risk dragging them to deadly places.
            continue;

        if (!playerAi && ServerFacade::instance().GetDistance2d(bot, player) > PlayerbotAIConfig::Instance().SightDistance)
            continue;

        Group* group = bot->GetGroup();
        // When inviting the 5th member of the group convert to raid for future invites.
        if (group && botAI->GetGrouperType() > GrouperType::LEADER_5 && !group->isRaidGroup() &&
            bot->GetGroup()->GetMembersCount() > 3)
        {
            auto convertOp = std::make_unique<GroupConvertToRaidOperation>(bot->GetGUID());
            PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(convertOp));
        }

        if (PlayerbotAIConfig::Instance().InviteChat &&
            (RandomPlayerbotMgr::instance().IsRandomBot(bot) || !IsRealPlayer(botAI->GetMaster())))
        {
            BroadcastHelper::BroadcastGuildGroupOrRaidInvite(botAI, bot, player, group);
        }

        return Invite(bot, player);
    }

    return false;
}

bool JoinGroupAction::Execute(Event event)
{
    if (bot->InBattleground())
        return false;

    if (bot->InBattlegroundQueue())
        return false;

    Player* master = event.getOwner();

    Group* group = master->GetGroup();

    if (group)
    {
        if (group->IsFull())
            return false;

        if (bot->GetGroup() == group)
            return false;
    }

    if (bot->GetGroup())
    {
        if (botAI->HasGameClientMaster())
            return false;

        if (!botAI->DoSpecificAction("leave", event, true))
            return false;
    }

    return Invite(master, bot);
}

