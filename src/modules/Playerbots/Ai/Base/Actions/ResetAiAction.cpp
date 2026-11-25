/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ResetAiAction.h"
#include "ClassicOpcodes.h"
#include "Event.h"
#include "Group.h"
#include "ObjectGuid.h"
#include "PlayerbotRepository.h"
#include "Playerbots.h"
#include "WorldPacket.h"

namespace
{
    // Member count of a 12.x SMSG_PARTY_UPDATE (WorldPackets::Party::PartyUpdate::Write): the header is
    // fixed width but carries two extra fields from client build 70291 on, and the count that follows is
    // the size of the variable-length member list.
    void ReadPartyUpdateMemberCount(WorldPacket& packet, uint32& membersCount)
    {
        ObjectGuid partyGuid;
        ObjectGuid leaderGuid;
        packet.read_skip<uint16_t>();      // PartyFlags
        packet.read_skip<uint8_t>();       // PartyIndex
        packet.read_skip<uint8_t>();       // PartyType
        packet.read_skip<int32_t>();       // MyIndex
        packet >> partyGuid;               // PartyGUID
        packet.read_skip<uint32_t>();      // SequenceNum

        bool const build70291OrLater = ClassicOpcodes::IsBuild70291OrLater();
        if (build70291OrLater)
            packet.read_skip<int8_t>();    // Classic70291_BeforeLeader

        packet >> leaderGuid;              // LeaderGUID
        packet.read_skip<uint8_t>();       // LeaderFactionGroup
        packet.read_skip<int32_t>();       // PingRestriction

        if (build70291OrLater)
            packet.read_skip<uint64_t>();  // Classic70291_BeforeMemberCount

        packet >> membersCount;            // Size<uint32>(PlayerList)
    }
}

bool ResetAiAction::Execute(Event event)
{
    if (!event.getPacket().empty())
    {
        WorldPacket packet = event.getPacket();
        packet.rpos(0);
        if (packet.GetOpcode() == SMSG_PARTY_UPDATE)
        {
            uint32 membersCount = 0;
            ReadPartyUpdateMemberCount(packet, membersCount);
            if (membersCount != 0)
            {
                return false;
            }
        }
    }
    if (Player* master = botAI->GetMaster())
    {
        Group* botGroup = bot->GetGroup();
        Group* masterGroup = master->GetGroup();
        if (botGroup && (!masterGroup || masterGroup != botGroup))
            botAI->SetMaster(nullptr);
    }
    if (sRandomPlayerbotMgr.IsRandomBot(bot) && !bot->InBattleground())
    {
        if (bot->GetGroup() && (!botAI->GetMaster() || GET_PLAYERBOT_AI(botAI->GetMaster())))
        {
            if (Player* newMaster = botAI->FindNewMaster())
                botAI->SetMaster(newMaster);
        }
    }
    PlayerbotRepository::instance().Reset(botAI);
    botAI->ResetStrategies(false);
    botAI->TellMaster("AI was reset to defaults");
    return true;
}
