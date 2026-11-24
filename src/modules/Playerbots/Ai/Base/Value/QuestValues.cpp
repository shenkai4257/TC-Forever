/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QuestValues.h"
#include "QuestHelper.h"

#include "MapManager.h"
#include "Playerbots.h"
#include "SharedValueContext.h"
#include <array>

// What kind of a relation does this entry have with this quest.
entryQuestRelationMap EntryQuestRelationMapValue::Calculate()
{
    entryQuestRelationMap rMap;

    // 12.x only exposes the entry -> quest maps directly for the starter relations; the
    // involved (ender) side is reachable quest -> entry through the reverse bounds.
    for (auto const& relation : *sObjectMgr->GetCreatureQuestRelationMapHACK())
        rMap[relation.first][relation.second] |= (int)QuestRelationFlag::questGiver;

    for (auto const& relation : *sObjectMgr->GetGOQuestRelationMapHACK())
        rMap[-(int32)relation.first][relation.second] |= (int)QuestRelationFlag::questGiver;

    auto const& questMap = sObjectMgr->GetQuestTemplates();

    for (auto& questItr : questMap)
    {
        uint32 questId = questItr.first;
        Quest const* quest = questItr.second.get();

        for (auto const& relation : sObjectMgr->GetCreatureQuestInvolvedRelationReverseBounds(questId))
            rMap[relation.second][questId] |= (int)QuestRelationFlag::questTaker;

        for (auto const& relation : sObjectMgr->GetGOQuestInvolvedRelationReverseBounds(questId))
            rMap[-(int32)relation.second][questId] |= (int)QuestRelationFlag::questGiver;

        // 12.x objectives carry their 3.3.5 slot index in StorageIndex; the relation map
        // keys on that index so the flags stay comparable with the quest guidp map.
        for (QuestObjective const& objective : quest->GetObjectives())
        {
            if (objective.StorageIndex < 0)
                continue;

            uint32 relationFlag = 1u << uint8(objective.StorageIndex);

            // Kill objective
            if (objective.Type == QUEST_OBJECTIVE_MONSTER && objective.ObjectID)
                rMap[objective.ObjectID][questId] |= relationFlag;

            // Loot objective
            if (objective.Type == QUEST_OBJECTIVE_ITEM && objective.ObjectID)
            {
                for (auto& entry : GAI_VALUE2(std::vector<int32>, "item drop list", objective.ObjectID))
                    rMap[entry][questId] |= relationFlag;
            }
        }
    }

    return rMap;
}

// Get all the objective entries for a specific quest.
void FindQuestObjectData::GetObjectiveEntries()
{
    relationMap = GAI_VALUE(entryQuestRelationMap, "entry quest relation");
}

// Every entry a spawn point can hold: the base from `creature`, plus up to two
// `creature_multispawn` alternates rolled between on respawn. 0 = no alternate.
static std::array<uint32, 3> SpawnEntries(CreatureData const& creData)
{
    // 12.x has no creature_multispawn alternates: a spawn point holds a single entry.
    return { creData.id, 0, 0 };
}

// Data worker. Checks for a specific creature what quest they are needed for and puts them in the proper place in the
// quest map.
void FindQuestObjectData::operator()(CreatureData const& creData)
{
    // The GuidPosition keeps the spawn's base entry, which can disagree with the alternate it
    // is filed under. It resolves by spawnId, and consumers read the map key.
    for (uint32 entry : SpawnEntries(creData))
    {
        if (!entry)
            continue;

        auto relations = relationMap.find(int32(entry));
        if (relations == relationMap.end())
            continue;

        for (auto& relation : relations->second)
        {
            uint32 questId = relation.first;
            uint32 flag = relation.second;
            data[questId][flag][entry].push_back(GuidPosition(creData));
        }
    }
}

// GameObject data worker. Checks for a specific gameObject what quest they are needed for and puts them in the proper
// place in the quest map.
void FindQuestObjectData::operator()(GameObjectData const& goData)
{
    int32 entry = goData.id * -1;

    for (auto& relation : relationMap[entry])
    {
        uint32 questId = relation.first;
        uint32 flag = relation.second;
        data[questId][flag][entry].push_back(GuidPosition(goData));
    }
}

// Goes past all creatures and gameobjects and creatures the full quest guid map.
questGuidpMap QuestGuidpMapValue::Calculate()
{
    FindQuestObjectData worker;
    for (auto const& itr : sObjectMgr->GetAllCreatureData())
        worker(itr.second);
    for (auto const& itr : sObjectMgr->GetAllGameObjectData())
        worker(itr.second);

    return worker.GetResult();
}

std::vector<GuidPosition> ActiveQuestTakersValue::Calculate()
{
    questGuidpMap questMap = GAI_VALUE(questGuidpMap, "quest guidp map");

    std::vector<GuidPosition> retQuestTakers;

    QuestStatusMap& questStatusMap = bot->getQuestStatusMap();

    for (auto& questStatus : questStatusMap)
    {
        uint32 questId = questStatus.first;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);

        if (!quest)
        {
            continue;
        }

        QuestStatus status = questStatus.second.Status;
        if ((status != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId)) &&
            (!PlayerbotQuest::IsQuestAutoComplete(quest) || !bot->CanTakeQuest(quest, false)))
            continue;

        auto q = questMap.find(questId);

        if (q == questMap.end())
            continue;

        auto qt = q->second.find((int)QuestRelationFlag::questTaker);

        if (qt == q->second.end())
            continue;

        for (auto& entry : qt->second)
        {
            if (entry.first > 0)
            {
                if (CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(entry.first))
                {
                    FactionTemplateEntry const* giverFaction = sFactionTemplateStore.LookupEntry(info->faction);
                    if (bot->GetFactionTemplateEntry()->IsHostileTo(giverFaction))
                        continue;
                }
            }

            for (auto& guidp : entry.second)
            {
                if (!guidp.IsCreatureOrGOAccessible())
                    continue;

                retQuestTakers.push_back(guidp);
            }
        }
    }

    return retQuestTakers;
}

std::vector<GuidPosition> ActiveQuestObjectivesValue::Calculate()
{
    questGuidpMap questMap = GAI_VALUE(questGuidpMap, "quest guidp map");

    std::vector<GuidPosition> retQuestObjectives;

    QuestStatusMap& questStatusMap = bot->getQuestStatusMap();

    for (auto& questStatus : questStatusMap)
    {
        uint32 questId = questStatus.first;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            continue;
        }

        if (questStatus.second.Status != QUEST_STATUS_INCOMPLETE)
            continue;

        for (QuestObjective const& objective : quest->GetObjectives())
        {
            if (objective.StorageIndex < 0 || objective.Amount <= 0)
                continue;

            if (PlayerbotQuest::GetObjectiveProgress(bot, quest, objective) >= objective.Amount)
                continue;

            auto q = questMap.find(questId);

            if (q == questMap.end())
                continue;

            auto qt = q->second.find((int)QuestRelationFlag(1u << uint8(objective.StorageIndex)));

            if (qt == q->second.end())
                continue;

            for (auto& entry : qt->second)
            {
                for (auto& guidp : entry.second)
                {
                    if (!guidp.IsCreatureOrGOAccessible())
                        continue;

                    retQuestObjectives.push_back(guidp);
                }
            }
        }
    }

    return retQuestObjectives;
}

uint8 FreeQuestLogSlotValue::Calculate()
{
    uint8 numQuest = 0;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);

        if (!questId)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        numQuest++;
    }

    return MAX_QUEST_LOG_SIZE - numQuest;
}

uint32 DialogStatusValue::getDialogStatus(Player* bot, int32 questgiver, uint32 questId)
{
    uint32 dialogStatus = DIALOG_STATUS_NONE;

    QuestRelationResult rbounds;   // QuestRelations (quest-giver)
    QuestRelationResult irbounds;  // InvolvedRelations (quest-finisher)

    if (questgiver > 0)
    {
        rbounds = sObjectMgr->GetCreatureQuestRelations(questgiver);
        irbounds = sObjectMgr->GetCreatureQuestInvolvedRelations(questgiver);
    }
    else
    {
        rbounds = sObjectMgr->GetGOQuestRelations(questgiver * -1);
        irbounds = sObjectMgr->GetGOQuestInvolvedRelations(questgiver * -1);
    }

    // Check markings for quest-finisher
    for (uint32 involvedQuestId : irbounds)
    {
        if (questId && involvedQuestId != questId)
            continue;

        Quest const* pQuest = sObjectMgr->GetQuestTemplate(involvedQuestId);
        if (!pQuest)
        {
            continue;
        }

        uint32 dialogStatusNew = DIALOG_STATUS_NONE;

        QuestStatus status = bot->GetQuestStatus(involvedQuestId);

        if ((status == QUEST_STATUS_COMPLETE && !bot->GetQuestRewardStatus(involvedQuestId)) ||
            (PlayerbotQuest::IsQuestAutoComplete(pQuest) && bot->CanTakeQuest(pQuest, false)))
        {
            if (PlayerbotQuest::IsQuestAutoComplete(pQuest) && pQuest->IsRepeatable())
            {
                dialogStatusNew = DIALOG_STATUS_REWARD_REP;
            }
            else
            {
                dialogStatusNew = DIALOG_STATUS_REWARD2;
            }
        }
        else if (status == QUEST_STATUS_INCOMPLETE)
        {
            dialogStatusNew = DIALOG_STATUS_INCOMPLETE;
        }

        if (dialogStatusNew > dialogStatus)
        {
            dialogStatus = dialogStatusNew;
        }
    }

    // check markings for quest-giver
    for (uint32 giverQuestId : rbounds)
    {
        if (questId && giverQuestId != questId)
            continue;

        Quest const* pQuest = sObjectMgr->GetQuestTemplate(giverQuestId);
        if (!pQuest)
        {
            continue;
        }

        uint32 dialogStatusNew = DIALOG_STATUS_NONE;

        QuestStatus status = bot->GetQuestStatus(giverQuestId);

        if (status == QUEST_STATUS_NONE)  // For all other cases the mark is handled either at some place else, or with
                                          // involved-relations already
        {
            if (bot->CanSeeStartQuest(pQuest))
            {
                if (bot->SatisfyQuestLevel(pQuest, false))
                {
                    int32 lowLevelDiff = sWorld->getIntConfig(CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF);
                    if (PlayerbotQuest::IsQuestAutoComplete(pQuest) ||
                        (pQuest->IsRepeatable() &&
                         bot->getQuestStatusMap()[giverQuestId].Status == QUEST_STATUS_REWARDED))
                    {
                        dialogStatusNew = DIALOG_STATUS_REWARD_REP;
                    }
                    else if (lowLevelDiff < 0 || bot->GetLevel() <= bot->GetQuestLevel(pQuest) + uint32(lowLevelDiff))
                    {
                        dialogStatusNew = DIALOG_STATUS_AVAILABLE;
                    }
                    else
                    {
                        dialogStatusNew = DIALOG_STATUS_LOW_LEVEL_AVAILABLE;
                    }
                }
                else
                {
                    dialogStatusNew = DIALOG_STATUS_UNAVAILABLE;
                }
            }
        }

        if (dialogStatusNew > dialogStatus)
        {
            dialogStatus = dialogStatusNew;
        }
    }

    return dialogStatus;
}

uint32 DialogStatusValue::Calculate() { return getDialogStatus(bot, stoi(getQualifier())); }

uint32 DialogStatusQuestValue::Calculate()
{
    return getDialogStatus(bot, getMultiQualifier(getQualifier(), 0), getMultiQualifier(getQualifier(), 1));
}

bool CanAcceptQuestValue::Calculate()
{
    return AI_VALUE2(uint32, "dialog status", getQualifier()) == DIALOG_STATUS_AVAILABLE;
};

bool CanAcceptQuestLowLevelValue::Calculate()
{
    uint32 dialogStatus = AI_VALUE2(uint32, "dialog status", getQualifier());
    return dialogStatus == DIALOG_STATUS_LOW_LEVEL_AVAILABLE;
};

bool CanTurnInQuestValue::Calculate()
{
    uint32 dialogStatus = AI_VALUE2(uint32, "dialog status", getQualifier());
    return dialogStatus == DIALOG_STATUS_REWARD2 || dialogStatus == DIALOG_STATUS_REWARD ||
           dialogStatus == DIALOG_STATUS_REWARD_REP;
};
