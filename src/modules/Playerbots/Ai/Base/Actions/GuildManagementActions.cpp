/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GuildManagementActions.h"
#include "ChatPacketHelper.h"
#include "BroadcastHelper.h"
#include "GuildMgr.h"
#include "GuildPackets.h"
#include "Playerbots.h"
#include "ServerFacade.h"

Player* GuidManageAction::GetPlayer(Event event)
{
    Player* player = nullptr;
    ObjectGuid guid = event.getObject();

    if (!guid.IsEmpty())
    {
        player = ObjectAccessor::FindPlayer(guid);

        if (player)
            return player;
    }

    std::string text = event.getParam();

    if (!text.empty())
    {
        if (normalizePlayerName(text))
        {
            player = ObjectAccessor::FindPlayerByName(text.c_str());

            if (player)
                return player;
        }

        return nullptr;
    }

    Player* master = GetMaster();
    if (!master)
        guid = bot->GetTarget();
    else
        guid = master->GetTarget();

    player = ObjectAccessor::FindPlayer(guid);

    if (player)
        return player;

    player = event.getOwner();

    if (player)
        return player;

    return nullptr;
}

// 12.x keeps Guild::GetMember private; resolve members through the public map.
static Guild::Member const* FindGuildMember(Guild* guild, ObjectGuid guid)
{
    auto itr = guild->GetMembers().find(guid);
    return itr != guild->GetMembers().end() ? &itr->second : nullptr;
}

bool GuidManageAction::Execute(Event event)
{
    Player* player = GetPlayer(event);

    if (!player || !PlayerIsValid(player) || player == bot)
        return false;

    return SendManage(player);
}

bool GuidManageAction::PlayerIsValid(Player* member) { return !member->GetGuildId(); }

uint8 GuidManageAction::GetRankId(Player* member)
{
    Guild::Member const* guildMember = FindGuildMember(sGuildMgr->GetGuildById(member->GetGuildId()), member->GetGUID());
    return guildMember ? uint8(guildMember->GetRankId()) : 0;
}

bool GuildInviteAction::SendManage(Player* member)
{
    WorldPacket raw(CMSG_GUILD_INVITE_BY_NAME);
    WorldPackets::Guild::GuildInviteByName packet(std::move(raw));
    packet.Name = member->GetName();
    bot->GetSession()->HandleGuildInviteByName(packet);
    return true;
}

bool GuildPromoteAction::SendManage(Player* member)
{
    WorldPacket raw(CMSG_GUILD_PROMOTE_MEMBER);
    WorldPackets::Guild::GuildPromoteMember packet(std::move(raw));
    packet.Promotee = member->GetGUID();
    bot->GetSession()->HandleGuildPromoteMember(packet);
    return true;
}

bool GuildDemoteAction::SendManage(Player* member)
{
    WorldPacket raw(CMSG_GUILD_DEMOTE_MEMBER);
    WorldPackets::Guild::GuildDemoteMember packet(std::move(raw));
    packet.Demotee = member->GetGUID();
    bot->GetSession()->HandleGuildDemoteMember(packet);
    return true;
}

bool GuildRemoveAction::SendManage(Player* member)
{
    WorldPacket raw(CMSG_GUILD_OFFICER_REMOVE_MEMBER);
    WorldPackets::Guild::GuildOfficerRemoveMember packet(std::move(raw));
    packet.Removee = member->GetGUID();
    bot->GetSession()->HandleGuildOfficerRemoveMember(packet);
    return true;
}

bool GuildInviteAction::isUseful()
{
    return bot->GetGuildId() && sGuildMgr->GetGuildById(bot->GetGuildId())->HasRankRight(bot, GR_RIGHT_INVITE);
}

bool GuildInviteAction::PlayerIsValid(Player* member)
{
    return !member->GetGuildId() && (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) ||
                                     (bot->GetTeamId() == member->GetTeamId()));
}

bool GuildPromoteAction::isUseful()
{
    return bot->GetGuildId() && sGuildMgr->GetGuildById(bot->GetGuildId())->HasRankRight(bot, GR_RIGHT_PROMOTE);
}

bool GuildPromoteAction::PlayerIsValid(Player* member)
{
    return member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member) - 1;
}

bool GuildDemoteAction::isUseful()
{
    return bot->GetGuildId() && sGuildMgr->GetGuildById(bot->GetGuildId())->HasRankRight(bot, GR_RIGHT_DEMOTE);
}

bool GuildDemoteAction::PlayerIsValid(Player* member)
{
    return member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member);
}

bool GuildRemoveAction::isUseful()
{
    return bot->GetGuildId() && sGuildMgr->GetGuildById(bot->GetGuildId())->HasRankRight(bot, GR_RIGHT_REMOVE);
}

bool GuildRemoveAction::PlayerIsValid(Player* member)
{
    return member->GetGuildId() == bot->GetGuildId() && GetRankId(bot) < GetRankId(member);
};

bool GuildManageNearbyAction::Execute(Event /*event*/)
{
    uint32 found = 0;

    Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    Guild::Member const* botMember = FindGuildMember(guild, bot->GetGUID());
    if (!botMember)
        return false;

    GuidVector nearGuids = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest friendly players")->Get();
    for (auto& guid : nearGuids)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);

        if (!player || bot == player)
            continue;

        if (player->isDND())
            continue;

        // Promote or demote nearby members based on chance.
        if (player->GetGuildId() && player->GetGuildId() == bot->GetGuildId())
        {
            uint32 dCount = AI_VALUE(uint32, "death count");

            if (!urand(0, 30) && dCount < 2 && guild->GetRankRights(botMember->GetRankId()) & GR_RIGHT_PROMOTE)
            {
                BroadcastHelper::BroadcastGuildMemberPromotion(botAI, bot, player);

                botAI->DoSpecificAction("guild promote", Event("guild management", guid), true);
                continue;
            }

            if (!urand(0, 30) && dCount > 2 && guild->GetRankRights(botMember->GetRankId()) & GR_RIGHT_DEMOTE)
            {
                BroadcastHelper::BroadcastGuildMemberDemotion(botAI, bot, player);

                botAI->DoSpecificAction("guild demote", Event("guild management", guid), true);
                continue;
            }

            continue;
        }

        if (!sPlayerbotAIConfig.RandomBotGuildNearby)
            return false;

        if (guild->GetMembersCount() > 1000)
            return false;

        if ((guild->GetRankRights(botMember->GetRankId()) & GR_RIGHT_INVITE) == 0)
            continue;

        if (player->GetGuildIdInvited())
            continue;

        PlayerbotAI* botAi = GET_PLAYERBOT_AI(player);

        if (!sPlayerbotAIConfig.RandomBotInvitePlayer && botAi && IsSelfBot(player))
            continue;

        if (botAi)
        {
            if (botAi->GetGuilderType() == GuilderType::SOLO && !botAi->HasGameClientMaster()) // Do not invite solo players.
                continue;

            if (IsRealPlayer(botAi->GetMaster()) && !sRandomPlayerbotMgr.IsRandomBot(player)) // Do not invite bots that belong to an active player.
                continue;
        }

        bool sameGroup = bot->GetGroup() && bot->GetGroup()->IsMember(player->GetGUID());

        if (!sameGroup && ServerFacade::instance().GetDistance2d(bot, player) > sPlayerbotAIConfig.SpellDistance)
            continue;

        if (sPlayerbotAIConfig.InviteChat && (sRandomPlayerbotMgr.IsRandomBot(bot) || !IsRealPlayer(botAI->GetMaster())))
        {
            /* std::map<std::string, std::string> placeholders;
            placeholders["%name"] = player->GetName();
            placeholders["%members"] = std::to_string(guild->GetMemberSize());
            placeholders["%guildname"] = guild->GetName();
            AreaTableEntry const* current_area = botAI->GetCurrentArea();
            AreaTableEntry const* current_zone = botAI->GetCurrentZone();
            placeholders["%area_name"] = current_area ? current_area->AreaName[BroadcastHelper::GetLocale()] : PlayerbotTextMgr::instance().GetBotText("string_unknown_area");
            placeholders["%zone_name"] = current_zone ? current_zone->AreaName[BroadcastHelper::GetLocale()] : PlayerbotTextMgr::instance().GetBotText("string_unknown_area");

            std::vector<std::string> lines;

            //TODO - Move these hardcoded texts to sql!
            switch ((urand(0, 10) * urand(0, 10)) / 10)
            {
            case 0:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("Hey %name do you want to join my guild?", placeholders));
                break;
            case 1:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("Hey man you wanna join my guild %name?", placeholders));
                break;
            case 2:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("I think you would be a good contribution to %guildname. Would you like to join %name?", placeholders));
                break;
            case 3:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("My guild %guildname has %members quality members. Would you like to make it 1 more %name?", placeholders));
                break;
            case 4:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("Hey %name do you want to join %guildname? We have %members members and looking to become number 1 of the server.", placeholders));
                break;
            case 5:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("I'm not really good at smalltalk. Do you wanna join my guild %name/r?", placeholders));
                break;
            case 6:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("Welcome to %zone_name.... do you want to join my guild %name?", placeholders));
                break;
            case 7:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("%name, you should join my guild!", placeholders));
                break;
            case 8:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("%name, I got this guild....", placeholders));
                break;
            case 9:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("You are actually going to join my guild %name?", placeholders));
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("Haha.. you are the man! We are going to raid Molten...", placeholders));
                break;
            case 10:
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("Hey Hey! do you guys wanna join my gild????", placeholders));
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("We've got a bunch of high levels and we are really super friendly..", placeholders));
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("..and watch your dog and do your homework...", placeholders));
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("..and we raid once a week and are working on MC raids...", placeholders));
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("..and we have more members than just me...", placeholders));
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("..and please stop I'm lonenly and we can get a ride the whole time...", placeholders));
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("..and it's really beautifull and I feel like crying...", placeholders));
                lines.push_back(PlayerbotTextMgr::instance().GetBotText("So what do you guys say are you going to join are you going to join?", placeholders));
                break;
            }

            for (auto line : lines)
                if (sameGroup)
                {
                    WorldPacket data;
                    PlayerbotChat::BuildChatPacket(data, bot->GetGroup()->isRaidGroup() ? CHAT_MSG_RAID : CHAT_MSG_PARTY, line.c_str(), LANG_UNIVERSAL, CHAT_FLAG_NONE, bot->GetGUID(), bot->GetName());
                    bot->GetGroup()->BroadcastPacket(&data, true);
                }
                else
                    bot->Say(line, (bot->GetTeamId() == TEAM_ALLIANCE ? LANG_COMMON : LANG_ORCISH));*/
        }

        if (botAI->DoSpecificAction("guild invite", Event("guild management", guid), true))
        {
            if (sPlayerbotAIConfig.InviteChat)
                return true;
            found++;
        }
    }

    return found > 0;
}

bool GuildManageNearbyAction::isUseful()
{
    if (!bot->GetGuildId())
        return false;

    Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    Guild::Member const* botMember = FindGuildMember(guild, bot->GetGUID());
    if (!botMember)
        return false;

    return guild->GetRankRights(botMember->GetRankId()) & (GR_RIGHT_DEMOTE | GR_RIGHT_PROMOTE | GR_RIGHT_INVITE);
}

bool GuildLeaveAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (owner && !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, owner, true))
    {
        botAI->TellError("Sorry, I am happy in my guild :)");
        return false;
    }

    WorldPacket raw(CMSG_GUILD_LEAVE);
    WorldPackets::Guild::GuildLeave packet(std::move(raw));
    bot->GetSession()->HandleGuildLeave(packet);
    return true;
}

bool GuildLeaveAction::isUseful() { return bot->GetGuildId(); }
