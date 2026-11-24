/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BattleGroundJoinAction.h"
#include "BattlegroundPackets.h"
#include "LFGPacketsCommon.h"
#include "SpellPackets.h"

namespace
{
    // 3.3.5 constant: the client shows "leave battleground" once the match is over.
    constexpr uint32 TIME_TO_AUTOREMOVE = 60000;
}
#include "BattlegroundMgr.h"
#include "Event.h"
#include "GameTime.h"
#include "GroupMgr.h"
#include "PlayerbotAI.h"
#include "PlayerbotOperations.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "RandomPlayerbotFactory.h"

namespace
{
    constexpr uint32 BG_JOIN_LOGIN_DELAY_MS = 120 * IN_MILLISECONDS;
}

bool BGJoinAction::Execute(Event /*event*/)
{
    uint32 queueType = AI_VALUE(uint32, "bg type");
    if (!queueType)  // force join to fill bg
    {
        if (bgList.empty())
            return false;

        BattlegroundQueueTypeId queueTypeId = (BattlegroundQueueTypeId)bgList[urand(0, bgList.size() - 1)];
        BattlegroundTypeId bgTypeId = BattlegroundTypeId(queueTypeId.BattlemasterListId);
        bool isRated = false;

        BattlegroundTemplate const* bg = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(bgTypeId);
        if (!bg)
            return false;

        uint32 mapId = uint32(bg->MapIDs.front());
        PVPDifficultyEntry const* pvpDiff = DB2Manager::GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
        if (!pvpDiff)
            return false;

        // 1.60 has no arena queues.
        // set bg type and bm guid
        // botAI->GetAiObjectContext()->GetValue<ObjectGuid>("bg master")->Set(bmGUID);
        botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(uint32(queueTypeId.GetPacked()));
        queueType = uint32(queueTypeId.GetPacked());
    }

    return JoinQueue(queueType);
}

bool BGJoinAction::canJoinBg(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId)
{
    // check if bot can join this bracket for the specific Battleground/Arena type
    BattlegroundTypeId bgTypeId = BattlegroundTypeId(queueTypeId.BattlemasterListId);

    // check if already in queue
    if (bot->InBattlegroundQueueForBattlegroundQueueType(queueTypeId))
        return false;

    // check too low/high level
    if (!bot->GetBGAccessByLevel(bgTypeId))
        return false;

    // check if the bracket exists for the bot's level for the specific Battleground/Arena type
    BattlegroundTemplate const* bg = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(bgTypeId);
    uint32 mapId = uint32(bg->MapIDs.front());
    PVPDifficultyEntry const* pvpDiff = DB2Manager::GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
    if (!pvpDiff)
        return false;

    BattlegroundBracketId bracket_temp = pvpDiff->GetBracketId();

    if (bracket_temp != bracketId)
        return false;

    return true;
}

bool BGJoinAction::shouldJoinBg(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId)
{
    BattlegroundTypeId bgTypeId = BattlegroundTypeId(queueTypeId.BattlemasterListId);
    BattlegroundTemplate const* bg = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(bgTypeId);
    if (!bg)
        return false;

    TeamId teamId = bot->GetTeamId();
    uint32 BracketSize = bg->BattlemasterEntry->MaxPlayers * 2;
    uint32 TeamSize = bg->BattlemasterEntry->MaxPlayers;

    // If the bot is in a group, only the leader can queue
    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetGUID()))
        return false;

    // Check if bots should join Battleground
    uint32 bgAllianceBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgAllianceBotCount;
    uint32 bgAlliancePlayerCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgAlliancePlayerCount;
    uint32 bgHordeBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgHordeBotCount;
    uint32 bgHordePlayerCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgHordePlayerCount;
    uint32 demandedSlots =
        sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].DemandedBgSlotsPerTeam(TeamSize, teamId);

    if (teamId == TEAM_ALLIANCE)
    {
        if ((bgAllianceBotCount + bgAlliancePlayerCount) < demandedSlots)
            return true;
    }
    else
    {
        if ((bgHordeBotCount + bgHordePlayerCount) < demandedSlots)
            return true;
    }

    return false;
}

bool BGJoinAction::isUseful()
{
    // do not try if BG bots disabled
    if (!sPlayerbotAIConfig.RandomBotJoinBG)
        return false;

    // can't queue while in BG/Arena
    if (bot->InBattleground())
        return false;

    // can't queue while in BG/Arena queue
    if (bot->InBattlegroundQueue())
        return false;

    // do not try right after login
    if (getMSTimeDiff(bot->GetInGameTime(), GameTime::GetGameTimeMS()) < BG_JOIN_LOGIN_DELAY_MS)
        return false;

    // check level
    if (bot->GetLevel() < 10)
        return false;

    // do not try if with player master
    if (IsRealPlayer(GET_PLAYERBOT_AI(bot)->GetMaster()))
        return false;

    // do not try if in group, if in group only leader can queue
    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetGUID()))
        return false;

    // do not try if in combat
    if (bot->IsInCombat())
        return false;

    // check Deserter debuff
    if (bot->IsDeserter())
        return false;

    // check if has free queue slots (pointless as already making sure not in queue)
    // keeping just in case.
    if (!bot->HasFreeBattlegroundQueueId())
        return false;

    // do not try if in dungeon
    // Map* map = bot->GetMap();
    // if (map && map->Instanceable())
    //     return false;

    bgList.clear();
    ratedList.clear();

    for (int bracket = BG_BRACKET_ID_FIRST; bracket < MAX_BATTLEGROUND_BRACKETS; ++bracket)
    {
        for (int queueType = BATTLEGROUND_QUEUE_AV; queueType < MAX_BATTLEGROUND_QUEUE_TYPES; ++queueType)
        {
            BattlegroundQueueTypeId queueTypeId = BattlegroundQueueTypeId(queueType);
            BattlegroundBracketId bracketId = BattlegroundBracketId(bracket);

            if (!canJoinBg(queueTypeId, bracketId))
                continue;

            if (shouldJoinBg(queueTypeId, bracketId))
                bgList.push_back(uint32(queueTypeId.GetPacked()));
        }
    }

    if (!bgList.empty())
        return true;

    return false;
}

bool BGJoinAction::JoinQueue(uint32 type)
{
    // ignore if player is already in BG, is logging out, or already being teleport
    if (!bot || (!bot->IsInWorld() && !bot->IsBeingTeleported()) || bot->InBattleground())
        return false;

    // get BG TypeId
    BattlegroundQueueTypeId queueTypeId = BattlegroundQueueTypeId(type);
    BattlegroundTypeId bgTypeId = BattlegroundTypeId(queueTypeId.BattlemasterListId);
    BattlegroundBracketId bracketId;

    BattlegroundTemplate const* bg = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(bgTypeId);
    if (!bg)
        return false;

    uint32 mapId = uint32(bg->MapIDs.front());
    PVPDifficultyEntry const* pvpDiff = DB2Manager::GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
    if (!pvpDiff)
        return false;

    bracketId = pvpDiff->GetBracketId();

    TeamId teamId = bot->GetTeamId();

    // check if already in queue
    if (bot->InBattlegroundQueueForBattlegroundQueueType(queueTypeId))
        return false;

    // check bg req level
    if (!bot->GetBGAccessByLevel(bgTypeId))
        return false;

    // get BG MapId
    uint32 bgTypeId_ = bgTypeId;
    uint32 instanceId = 0;  // 0 = First Available

    // bool isPremade = false; //not used, line marked for removal.
    bool isArena = false;
    bool isRated = false;
    uint8 arenaslot = 0;
    uint8 asGroup = false;

    std::string _bgType;

    // get battlemaster
    // Unit* unit = botAI->GetUnit(AI_VALUE2(CreatureData const*, "bg master", bgTypeId));
    Unit* unit = botAI->GetUnit(sRandomPlayerbotMgr.GetBattleMasterGUID(bot, bgTypeId));
    if (!unit && isArena)
    {
        botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
        TC_LOG_DEBUG("playerbots", "Bot {} could not find Battlemaster to join", bot->GetGUID().ToString().c_str());
        return false;
    }

    // This breaks groups as refresh includes a remove from group function call.
    // refresh food/regs
    // sRandomPlayerbotMgr.Refresh(bot);

    bool joinAsGroup = bot->GetGroup() && bot->GetGroup()->GetLeaderGUID() == bot->GetGUID();

    // in wotlk only arena requires battlemaster guid
    // ObjectGuid guid = isArena ? unit->GetGUID() : bot->GetGUID(); //not used, line marked for removal.

    switch (bgTypeId)
    {
        case BATTLEGROUND_AV:
            _bgType = "AV";
            break;
        case BATTLEGROUND_WS:
            _bgType = "WSG";
            break;
        case BATTLEGROUND_AB:
            _bgType = "AB";
            break;
        case BATTLEGROUND_RB:
            _bgType = "Random";
            break;
        default:
            break;
    }

    TC_LOG_INFO("playerbots", "Bot {} {}:{} <{}> queued {} {}", bot->GetGUID().ToString().c_str(),
             bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName().c_str(), _bgType.c_str(),
             isRated   ? "Rated Arena"
             : isArena ? "Arena"
                       : "");

    if (!joinAsGroup)
    {
        if (teamId == TEAM_ALLIANCE)
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgAllianceBotCount++;
        else
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgHordeBotCount++;
    }
    else
    {
        if (teamId == TEAM_ALLIANCE)
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgAllianceBotCount +=
                bot->GetGroup()->GetMembersCount();
        else
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgHordeBotCount +=
                bot->GetGroup()->GetMembersCount();
    }

    botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);

    // 12.x takes the structured battlemaster join packet (packed queue id).
    WorldPacket rawJoin(CMSG_BATTLEMASTER_JOIN);
    WorldPackets::Battleground::BattlemasterJoin joinPacket(std::move(rawJoin));
    joinPacket.QueueIDs[0] = queueTypeId.GetPacked();
    bot->GetSession()->HandleBattlemasterJoinOpcode(joinPacket);
    return true;
}

// Not sure if this has ever worked, but it should be similar to BGJoinAction::shouldJoinBg
bool FreeBGJoinAction::shouldJoinBg(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId)
{
    BattlegroundTypeId bgTypeId = BattlegroundTypeId(queueTypeId.BattlemasterListId);
    BattlegroundTemplate const* bg = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(bgTypeId);
    if (!bg)
        return false;

    TeamId teamId = bot->GetTeamId();

    uint32 BracketSize = bg->BattlemasterEntry->MaxPlayers * 2;
    uint32 TeamSize = bg->BattlemasterEntry->MaxPlayers;

    // If the bot is in a group, only the leader can queue
    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetGUID()))
        return false;

    // Check if bots should join Battleground
    uint32 bgAllianceBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgAllianceBotCount;
    uint32 bgAlliancePlayerCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgAlliancePlayerCount;
    uint32 bgHordeBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgHordeBotCount;
    uint32 bgHordePlayerCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].bgHordePlayerCount;
    uint32 demandedSlots =
        sRandomPlayerbotMgr.BattlegroundData[queueTypeId.GetPacked()][bracketId].DemandedBgSlotsPerTeam(TeamSize, teamId);

    if (teamId == TEAM_ALLIANCE)
    {
        if ((bgAllianceBotCount + bgAlliancePlayerCount) < demandedSlots)
            return true;
    }
    else
    {
        if ((bgHordeBotCount + bgHordePlayerCount) < demandedSlots)
            return true;
    }

    return false;
}

bool BGLeaveAction::Execute(Event /*event*/)
{
    if (!(bot->InBattlegroundQueue() || bot->InBattleground()))
        return false;

    // botAI->ChangeStrategy("-bg", BOT_STATE_NON_COMBAT);

    if (BGStatusAction::LeaveBG(botAI))
        return true;

    // leave queue if not in BG
    BattlegroundQueueTypeId queueTypeId = bot->GetBattlegroundQueueTypeId(0);
    BattlegroundTypeId _bgTypeId = BattlegroundTypeId(queueTypeId.BattlemasterListId);
    uint8 type = false;
    uint16 unk = 0x1F90;
    uint8 unk2 = 0x0;
    bool isArena = false;
    bool IsRandomBot = sRandomPlayerbotMgr.IsRandomBot(bot);

    uint32 queueType = AI_VALUE(uint32, "bg type");
    if (!queueType)
        return false;

    TC_LOG_INFO("playerbots", "Bot {} {}:{} <{}> leaves {} queue", bot->GetGUID().ToString().c_str(),
             bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName().c_str(),
             isArena ? "Arena" : "BG");

    WorldPacket rawPort(CMSG_BATTLEFIELD_PORT);
    WorldPackets::Battleground::BattlefieldPort port(std::move(rawPort));
    port.Ticket.RequesterGuid = bot->GetGUID();
    port.Ticket.Id = uint32(queueTypeId.GetPacked());
    port.Ticket.Type = WorldPackets::LFG::RideType::Battlegrounds;
    port.AcceptedInvite = false;
    bot->GetSession()->HandleBattleFieldPortOpcode(port);

    if (IsRandomBot)
        botAI->SetMaster(nullptr);

    botAI->ResetStrategies(!IsRandomBot);
    botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
    botAI->GetAiObjectContext()->GetValue<uint32>("bg role")->Set(0);
    botAI->GetAiObjectContext()->GetValue<uint32>("arena type")->Set(0);

    return true;
}

bool BGStatusAction::LeaveBG(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;
    bool isArena = bg->isArena();
    bool isRandomBot = sRandomPlayerbotMgr.IsRandomBot(bot);

    if (isRandomBot)
        botAI->SetMaster(nullptr);

    botAI->ChangeStrategy("-warsong", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-warsong", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-arathi", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-arathi", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-eye", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-eye", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-isle", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-isle", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-Battleground", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-Battleground", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-arena", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-arena", BOT_STATE_NON_COMBAT);

    TC_LOG_INFO("playerbots", "Bot {} {}:{} <{}> leaves {}", bot->GetGUID().ToString().c_str(),
             bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(),
             isArena ? "Arena" : "BG");

    WorldPacket rawLeave(CMSG_BATTLEFIELD_LEAVE);
    WorldPackets::Battleground::BattlefieldLeave leave(std::move(rawLeave));
    bot->GetSession()->HandleBattlefieldLeaveOpcode(leave);

    botAI->ResetStrategies(!isRandomBot);
    botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
    botAI->GetAiObjectContext()->GetValue<uint32>("bg role")->Set(0);
    botAI->GetAiObjectContext()->GetValue<uint32>("arena type")->Set(0);
    PositionMap& posMap = botAI->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
    PositionInfo pos = botAI->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get()["bg objective"];
    pos.Reset();
    posMap["bg objective"] = pos;
    return true;
}

bool BGStatusAction::isUseful() { return bot->InBattlegroundQueue(); }

bool BGStatusAction::Execute(Event /*event*/)
{
    // 12.x delivers battleground status through structured packets that do not carry the
    // 3.3.5 layout. Accept an outstanding invite directly from the queue state instead.
    if (!bot || !bot->InBattlegroundQueue())
        return false;

    for (uint8 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
    {
        BattlegroundQueueTypeId queueTypeId = bot->GetBattlegroundQueueTypeId(slot);
        if (queueTypeId == BATTLEGROUND_QUEUE_NONE || bot->InBattleground())
            continue;

        WorldPacket rawPort(CMSG_BATTLEFIELD_PORT);
        WorldPackets::Battleground::BattlefieldPort port(std::move(rawPort));
        port.Ticket.RequesterGuid = bot->GetGUID();
        port.Ticket.Id = uint32(queueTypeId.GetPacked());
        port.Ticket.Type = WorldPackets::LFG::RideType::Battlegrounds;
        port.AcceptedInvite = true;
        bot->GetSession()->HandleBattleFieldPortOpcode(port);
        return true;
    }

    return false;
}

bool BGStatusCheckAction::Execute(Event /*event*/)
{
    if (bot->IsBeingTeleported())
        return false;

    // 12.x pushes status to the client; nothing can be queried, so a queued random bot that
    // is not being invited simply leaves the queue.
    if (!bot->InBattleground() && bot->InBattlegroundQueue())
        return BGStatusAction::LeaveBG(botAI);

    return true;
}

bool BGStatusCheckAction::isUseful() { return bot->InBattlegroundQueue(); }

bool BGStrategyCheckAction::Execute(Event /*event*/)
{
    bool inside_bg = bot->InBattleground() && bot->GetBattleground();
    ;
    if (!inside_bg && botAI->HasStrategy("battleground", BOT_STATE_NON_COMBAT))
    {
        botAI->ResetStrategies();
        return true;
    }
    if (inside_bg && !botAI->HasStrategy("battleground", BOT_STATE_NON_COMBAT))
    {
        botAI->ResetStrategies();
        return false;
    }
    return false;
}
