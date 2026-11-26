/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GuildAcceptAction.h"
#include "Event.h"
#include "GuildPackets.h"
#include "PacketOperators.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"

bool GuildAcceptAction::Execute(Event event)
{
    // 12.x layout (WorldPackets::Guild::GuildInvite::Write): 6 bit inviter name length, 9 bit inviter
    // surname, 7 bit guild name, 7 bit old guild name, a fixed-width part, then the four name strings.
    WorldPacket p(event.getPacket());
    p.rpos(0);
    uint32 nameLengths = 0;
    p >> WorldPackets::Bits<29>(nameLengths);
    p.ResetBitPos();
    uint8 const inviterNameLength = uint8(nameLengths >> 23);

    ObjectGuid guildGuid;
    ObjectGuid oldGuildGuid;
    p.read_skip<uint32_t>();         // InviterVirtualRealmAddress
    p.read_skip<uint32_t>();         // GuildVirtualRealmAddress
    p >> guildGuid;
    p.read_skip<uint32_t>();         // OldGuildVirtualRealmAddress
    p >> oldGuildGuid;
    p.read_skip(7 * sizeof(uint32_t));  // emblem (5), AchievementPoints

    std::string Invitedname;
    if (inviterNameLength)
        Invitedname = p.ReadString(inviterNameLength);

    Player* inviter = nullptr;

    if (normalizePlayerName(Invitedname))
        inviter = ObjectAccessor::FindPlayerByName(Invitedname.c_str());

    if (!inviter)
        return false;

    bool accept = true;
    uint32 guildId = inviter->GetGuildId();
    if (!guildId)
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "guild_accept_inviter_not_in_guild", "You are not in a guild!", {}));
        accept = false;
    }
    else if (bot->GetGuildId())
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "guild_accept_already_in_guild", "Sorry, I am in a guild already", {}));
        accept = false;
    }
    else if (!botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter, true))
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "guild_accept_declined", "Sorry, I don't want to join your guild :(", {}));
        accept = false;
    }

    if (accept)
    {
        botAI->RunOnWorldThread("guild invite accept", [](Player* bot) {
            WorldPacket payload(CMSG_ACCEPT_GUILD_INVITE, 0);
            WorldPackets::Guild::AcceptGuildInvite data(std::move(payload));
            bot->GetSession()->HandleGuildAcceptInvite(data);
        });
    }
    else
    {
        botAI->RunOnWorldThread("guild invite decline", [](Player* bot) {
            WorldPacket payload(CMSG_GUILD_DECLINE_INVITATION, 0);
            WorldPackets::Guild::GuildDeclineInvitation data(std::move(payload));
            bot->GetSession()->HandleGuildDeclineInvitation(data);
        });
    }

    return true;
}
