/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QuestAction.h"
#include "QuestPackets.h"
#include "QuestHelper.h"
#include "GossipDef.h"
#include "BroadcastHelper.h"
#include "Chat.h"
#include "ChatHelper.h"
#include "Event.h"
#include "ItemPacketsCommon.h"
#include "ItemTemplate.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Playerbots.h"
#include "ReputationMgr.h"
#include "ServerFacade.h"
#include <algorithm>
#include <sstream>

bool QuestAction::Execute(Event event)
{
    ObjectGuid guid = event.getObject();
    Player* master = GetMaster();

    // Checks if the bot and botAI are valid
    if (!bot || !botAI)
        return false;

    // Sets guid based on bot or master target
    if (!guid)
    {
        if (!master)
        {
            guid = bot->GetTarget();
        }
        else
        {
            guid = master->GetTarget();
        }
    }

    if (!guid.IsEmpty())
    {
        return ProcessQuests(guid);
    }

    bool result = false;

    // Check the nearest NPCs
    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    for (auto const& npc : npcs)
    {
        Unit* unit = botAI->GetUnit(npc);
        if (unit && bot->GetDistance(unit) <= INTERACTION_DISTANCE)
        {
            result |= ProcessQuests(unit);
        }
    }

    // Checks the nearest game objects
    GuidVector gos = AI_VALUE(GuidVector, "nearest game objects");
    for (auto const& go : gos)
    {
        GameObject* gameobj = botAI->GetGameObject(go);
        if (gameobj && bot->GetDistance(gameobj) <= INTERACTION_DISTANCE)
        {
            result |= ProcessQuests(gameobj);
        }
    }

    return result;
}

bool QuestAction::CompleteQuest(Player* player, uint32 entry)
{
    Quest const* pQuest = sObjectMgr->GetQuestTemplate(entry);

    // If player doesn't have the quest
    if (!pQuest || player->GetQuestStatus(entry) == QUEST_STATUS_NONE)
    {
        return false;
    }

    // Add quest items for quests that require items
    for (QuestObjective const& objective : pQuest->GetObjectives())
    {
        if (objective.Type != QUEST_OBJECTIVE_ITEM || objective.ObjectID <= 0 || objective.Amount <= 0)
            continue;

        uint32 const id = uint32(objective.ObjectID);
        uint32 const count = uint32(objective.Amount);
        uint32 const curItemCount = player->GetItemCount(id, true);
        if (curItemCount >= count)
            continue;

        ItemPosCountVec dest;
        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, id, count - curItemCount);
        if (msg == EQUIP_ERR_OK)
        {
            Item* item = player->StoreNewItem(dest, id, true);
            player->SendNewItem(item, count - curItemCount, true, false);
        }
    }

    // All creature/GO slain/casted (not required, but otherwise it will display "Creature slain 0/10")
    for (QuestObjective const& objective : pQuest->GetObjectives())
    {
        if (objective.ObjectID <= 0 || objective.Amount <= 0)
            continue;

        if (objective.Type == QUEST_OBJECTIVE_MONSTER)
        {
            for (int32 z = 0; z < objective.Amount; ++z)
                player->KilledMonsterCredit(uint32(objective.ObjectID), ObjectGuid::Empty);
        }
        else if (objective.Type == QUEST_OBJECTIVE_GAMEOBJECT)
        {
            for (int32 z = 0; z < objective.Amount; ++z)
                player->KillCreditGO(uint32(objective.ObjectID));
        }
    }

    // If the quest requires reputation to complete (12.x stores it as a quest objective)
    for (QuestObjective const& objective : pQuest->GetObjectives())
    {
        if (objective.Type != QUEST_OBJECTIVE_MIN_REPUTATION || objective.ObjectID <= 0 || objective.Amount <= 0)
            continue;

        uint32 const repFaction = uint32(objective.ObjectID);
        uint32 const repValue = uint32(objective.Amount);
        uint32 const curRep = player->GetReputationMgr().GetReputation(repFaction);
        if (curRep < repValue)
            if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(repFaction))
                player->GetReputationMgr().SetReputation(factionEntry, repValue);
    }

    // 12.x has no "required money" quest field; money checks happen in the turn-in handler.

    const std::string text_quest = ChatHelper::FormatQuest(pQuest);
    if (botAI->HasStrategy("debug quest", BotState::BOT_STATE_NON_COMBAT) || botAI->HasStrategy("debug rpg", BotState::BOT_STATE_COMBAT))
    {
        TC_LOG_INFO("playerbots", "{} => Quest [ {} ] completed", bot->GetName(), pQuest->GetLogTitle());
        bot->Say("Quest [ " + text_quest + " ] completed", LANG_UNIVERSAL);
    }
    botAI->TellMasterNoFacing("Quest completed " + text_quest);

    player->CompleteQuest(entry);

    return true;
}

bool QuestAction::ProcessQuests(ObjectGuid questGiver)
{
    if (GameObject* gameObject = botAI->GetGameObject(questGiver))
        if (gameObject->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
            return ProcessQuests(gameObject);

    Creature* creature = botAI->GetCreature(questGiver);
    if (creature)
        return ProcessQuests(creature);

    return false;
}

bool QuestAction::ProcessQuests(WorldObject* questGiver)
{
    ObjectGuid guid = questGiver->GetGUID();

    if (bot->GetDistance(questGiver) > INTERACTION_DISTANCE && !sPlayerbotAIConfig.SyncQuestWithPlayer)
    {
        //if (botAI->HasStrategy("debug", BotState::BOT_STATE_COMBAT) || botAI->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))

        botAI->TellError("Cannot talk to quest giver");
        return false;
    }

    if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, questGiver, sPlayerbotAIConfig.SightDistance))
        bot->SetFacingToObject(questGiver);

    bot->SetTarget(guid);
    bot->PrepareQuestMenu(guid);

    QuestMenu& questMenu = bot->PlayerTalkClass->GetQuestMenu();
    for (uint32 i = 0; i < questMenu.GetMenuItemCount(); ++i)
    {
        QuestMenuItem const& menuItem = questMenu.GetItem(i);
        uint32 questID = menuItem.QuestId;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questID);
        if (!quest)
            continue;

        ProcessQuest(quest, questGiver);
    }

    return true;
}

bool QuestAction::CanAcceptQuest(Player* bot, Quest const* quest)
{
    if (!bot || !quest)
        return false;

    // Core eligibility: level/race/class/reputation/chain/conditions/...
    if (!bot->CanTakeQuest(quest, false))
        return false;

    // Never accept grey quests (well below the bot's level), same band as QuestValues.cpp.
    // Player::GetQuestLevel falls back to the bot's own level for level-less quests
    // (QuestLevel <= 0), so those are never treated as grey — like the core's dialog status.
    if ((int32)bot->GetLevel() > bot->GetQuestLevel(quest) + GREY_QUEST_LEVEL_BAND)
        return false;

    return true;
}

bool QuestAction::AcceptQuest(Quest const* quest, ObjectGuid questGiver)
{
    std::ostringstream out;

    uint32 questId = quest->GetQuestId();

    if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
        out << "Already completed";
    else if (!bot->CanTakeQuest(quest, false))
    {
        if (!bot->SatisfyQuestStatus(quest, false))
            out << "Already on";
        else
            out << "Can't take";
    }
    else if (!bot->SatisfyQuestLog(false))
        out << "Quest log is full";
    else if (!bot->CanAddQuest(quest, false))
        out << "Bags are full";
    else
    {
        WorldPacket raw(CMSG_QUEST_GIVER_ACCEPT_QUEST);
        WorldPackets::Quest::QuestGiverAcceptQuest accept(std::move(raw));
        accept.QuestGiverGUID = questGiver;
        accept.QuestID = int32(questId);
        bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(accept);

        if (bot->GetQuestStatus(questId) == QUEST_STATUS_NONE && sPlayerbotAIConfig.SyncQuestWithPlayer)
        {
            Object* pObject = ObjectAccessor::GetObjectByTypeMask(*bot, questGiver,
                                                                  TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT | TYPEMASK_ITEM);
            bot->AddQuest(quest, pObject);
        }

        if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE && bot->GetQuestStatus(questId) != QUEST_STATUS_REWARDED)
        {
            BroadcastHelper::BroadcastQuestAccepted(botAI, bot, quest);
            out << "Accepted " << chat->FormatQuest(quest);
            botAI->TellMaster(out);
            return true;
        }
        out << "Cannot accept";
    }

    out << " " << chat->FormatQuest(quest);
    botAI->TellMaster(out);

    return false;
}

bool QuestUpdateCompleteAction::Execute(Event event)
{
    // the action can hardly be triggered
    WorldPacket p(event.getPacket());
    p.rpos(0);

    uint32 questId = 0;
    p >> questId;

    Quest const* qInfo = sObjectMgr->GetQuestTemplate(questId);
    if (qInfo)
    {
        const auto format = ChatHelper::FormatQuest(qInfo);
        if (botAI->GetMaster())
            botAI->TellMasterNoFacing("Quest completed " + format);
        BroadcastHelper::BroadcastQuestUpdateComplete(botAI, bot, qInfo);
        // TC_LOG_DEBUG("playerbots", "[New rpg] {} complete quest {}", bot->GetName(), qInfo->GetQuestId());
        // botAI->rpgStatistic.questCompleted++;
    }

    return true;
}

/*
* For creature or gameobject
*/
bool QuestUpdateAddKillAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);

    // 12.x layout (WorldPackets::Quest::QuestUpdateAddCredit::Write): victim, quest id, object id,
    // count, required, objective type. The object type is its own field now (3.3.5 set bit 31 of the id).
    ObjectGuid victim;
    int32 questId, entry;
    uint16 count, required;
    uint32 objectiveType;
    p >> victim >> questId >> entry >> count >> required >> objectiveType;
    uint32 const available = count;
    uint32 const requiredCount = required;

    Quest const* qInfo = sObjectMgr->GetQuestTemplate(questId);
    if (qInfo && objectiveType == QUEST_OBJECTIVE_GAMEOBJECT)
    {
        GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(uint32(entry));
        if (info)
        {
            std::string infoName = PlayerbotAI::GetLocalizedGameObjectName(uint32(entry));
            BroadcastHelper::BroadcastQuestUpdateAddKill(botAI, bot, qInfo, available, requiredCount, infoName);
            if (botAI->GetMaster())
            {
                std::ostringstream out;
                out << infoName << " " << available << "/" << requiredCount << " " << ChatHelper::FormatQuest(qInfo);
                botAI->TellMasterNoFacing(out.str());
            }
        }
    }
    else if (qInfo)
    {
        CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(uint32(entry));
        if (info)
        {
            std::string infoName = PlayerbotAI::GetLocalizedCreatureName(uint32(entry));
            BroadcastHelper::BroadcastQuestUpdateAddKill(botAI, bot, qInfo, available, requiredCount, infoName);
            if (botAI->GetMaster())
            {
                std::ostringstream out;
                out << infoName << " " << available << "/" << requiredCount << " " << ChatHelper::FormatQuest(qInfo);
                botAI->TellMasterNoFacing(out.str());
            }
        }
    }
    return false;
}

bool QuestItemPushResultAction::Execute(Event event)
{
    WorldPacket packet = event.getPacket();
    packet.rpos(0);

    // 12.x layout (WorldPackets::Item::ItemPushResult::Write): player, slot, slot in bag, the item
    // instance, proxy item id, quantity pushed, quantity in the inventory, ...
    ObjectGuid guid;
    packet >> guid;
    packet.read_skip<uint8_t>();        // Slot
    packet.read_skip<int32_t>();        // SlotInBag
    WorldPackets::Item::ItemInstance item;
    packet >> item;
    packet.read_skip<int32_t>();        // ProxyItemID
    int32 count;
    int32 itemCount;
    packet >> count >> itemCount;

    uint32 const itemEntry = item.ItemID;

    if (guid != bot->GetGUID())
        return false;

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto)
        return false;

    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            return false;

        uint32 const required = PlayerbotQuest::GetRequiredItemAmount(quest, itemEntry);
        if (required)
        {
            int32 const previousCount = itemCount - count;
            if (uint32(previousCount) < required && botAI->GetMaster())
            {
                std::string itemLink = ChatHelper::FormatItem(proto);
                std::ostringstream out;
                int32 const available = std::min((int32)itemCount, int32(required));
                out << itemLink << " " << available << "/" << required << " " << ChatHelper::FormatQuest(quest);
                botAI->TellMasterNoFacing(out.str());
            }
        }
    }

    return false;
}

bool QuestUpdateFailedTimerAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);

    uint32 questId;
    p >> questId;

    Quest const* qInfo = sObjectMgr->GetQuestTemplate(questId);

    if (qInfo)
    {
        std::map<std::string, std::string> placeholders;
        placeholders["%quest_link"] = botAI->GetChatHelper()->FormatQuest(qInfo);
        botAI->TellMaster(PlayerbotTextMgr::instance().GetBotText("Failed timer for %quest_link, abandoning", placeholders));
        BroadcastHelper::BroadcastQuestUpdateFailedTimer(botAI, bot, qInfo);
    }
    else
    {
        botAI->TellMaster("Failed timer for " + std::to_string(questId));
    }

    //drop quest
    bot->AbandonQuest(questId);

    return false;
}
