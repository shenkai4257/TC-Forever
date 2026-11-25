/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LeaveGroupAction.h"
#include "Event.h"
#include "PacketOperators.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "PartyPackets.h"

bool LeaveGroupAction::Execute(Event event)
{
    Player* player = event.getOwner();
    if (player == botAI->GetMaster())
        return Leave();

    return false;
}

bool PartyCommandAction::Execute(Event event)
{
    // 12.x layout (WorldPackets::Party::PartyCommandResult::Write): 6 bit name length, 9 bit surname
    // length, 4 bit command, 6 bit result, then result data, result guid and both names.
    WorldPacket& p = event.getPacket();
    p.rpos(0);
    uint16 nameAndSurnameLength = 0;    // 6 bit name length, then 9 bit surname length
    uint8 command = 0;
    p >> WorldPackets::Bits<15>(nameAndSurnameLength) >> WorldPackets::Bits<4>(command);
    p.ResetBitPos();

    ObjectGuid resultGuid;
    p.read_skip<uint32_t>();     // ResultData
    p >> resultGuid;

    uint8 const nameLength = uint8(nameAndSurnameLength >> 9);
    std::string member;
    if (nameLength)
        member = p.ReadString(nameLength);

    if (command != PARTY_OP_LEAVE)
        return false;
    // Only leave if master has left the party, and randombot cannot set new master.
    Player* master = GetMaster();
    if (master && member == master->GetName())
    {
        if (sRandomPlayerbotMgr.IsRandomBot(bot))
        {
            Player* newMaster = botAI->FindNewMaster();
            if (newMaster || bot->InBattleground())
            {
                botAI->SetMaster(newMaster);
                return false;
            }
        }
        return Leave();
    }
    return false;
}

bool UninviteAction::Execute(Event event)
{
    WorldPacket& p = event.getPacket();
    // 12.x merged both 3.3.5 uninvite opcodes into the structured CMSG_PARTY_UNINVITE.
    if (p.GetOpcode() != CMSG_PARTY_UNINVITE)
        return false;

    WorldPacket rawUninvite(p);
    WorldPackets::Party::PartyUninvite uninvite(std::move(rawUninvite));
    uninvite.Read();   // the structured packet only fills TargetGUID from the raw bytes here

    if (bot->GetGUID() == uninvite.TargetGUID)
        return Leave();

    return false;
}

bool LeaveGroupAction::Leave()
{
    if (!botAI)
        return false;

    Player* master = botAI -> GetMaster();
    if (master)
        botAI->TellMaster(
            PlayerbotTextMgr::instance().GetBotTextOrDefault("goodbye", "Goodbye!", {}),
            PLAYERBOT_SECURITY_TALK);

    botAI->LeaveOrDisbandGroup();
    return true;
}

bool LeaveFarAwayAction::Execute(Event /*event*/)
{
    // allow bot to leave party when they want
    return Leave();
}

bool LeaveFarAwayAction::isUseful()
{
    if (bot->InBattleground())
        return false;

    if (bot->InBattlegroundQueue())
        return false;

    if (!bot->GetGroup())
        return false;

    Player* groupLeader = botAI->GetGroupLeader();
    Player* trueMaster = botAI->GetMaster();
    if (!groupLeader || (bot == groupLeader && !IsSelfBot(bot)))
        return false;

    PlayerbotAI* groupLeaderBotAI = nullptr;
    if (groupLeader)
        groupLeaderBotAI = GET_PLAYERBOT_AI(groupLeader);
    if (groupLeader && !groupLeaderBotAI)
        return false;

    if (trueMaster && !GET_PLAYERBOT_AI(trueMaster))
        return false;

    if (botAI->IsAltBot() &&
        (!groupLeaderBotAI || IsSelfBot(groupLeader)))  // Don't leave when an altbot is grouped under a regular real player or a SelfBot.
        return false;

    if (botAI->GetGrouperType() == GrouperType::SOLO)
        return true;

    uint32 dCount = AI_VALUE(uint32, "death count");

    if (dCount > 9)
        return true;

    if (dCount > 4 && !botAI->HasGameClientMaster())
        return true;

    if (bot->GetGuildId() == groupLeader->GetGuildId())
    {
        if (bot->GetLevel() > groupLeader->GetLevel() + 5)
        {
            if (AI_VALUE(bool, "should get money"))
                return false;
        }
    }

    if (abs(int32(groupLeader->GetLevel() - bot->GetLevel())) > 4)
        return true;

    if (bot->GetMapId() != groupLeader->GetMapId() || bot->GetDistance2d(groupLeader) >= 2 * sPlayerbotAIConfig.RpgDistance)
    {
        return true;
    }

    return false;
}
