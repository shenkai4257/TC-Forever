/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GrindTargetValue.h"
#include "CreatureHelper.h"
#include "QuestHelper.h"
#include "NewRpgInfo.h"
#include "Playerbots.h"
#include "ReputationMgr.h"
#include "ServerFacade.h"
#include "SharedDefines.h"

Unit* GrindTargetValue::Calculate()
{
    uint32 memberCount = 1;
    Group* group = bot->GetGroup();
    if (group)
        memberCount = group->GetMembersCount();

    Unit* target = nullptr;
    uint32 assistCount = 0;
    while (!target && assistCount < memberCount)
    {
        target = FindTargetForGrinding(assistCount++);
    }

    return target;
}

Unit* GrindTargetValue::FindTargetForGrinding(uint32 assistCount)
{
    Group* group = bot->GetGroup();
    Player* master = GetMaster();

    if (master && (master == bot || master->GetMapId() != bot->GetMapId() || master->IsBeingTeleported() ||
                   !GET_PLAYERBOT_AI(master)))
        master = nullptr;

    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;

        return unit;
    }

    GuidVector targets = *context->GetValue<GuidVector>("possible targets");
    if (targets.empty())
    {
        TC_LOG_DEBUG("playerbots", "[grind-diag] {}: no possible targets at all", bot->GetName());
        return nullptr;
    }

    float distance = 0;
    Unit* result = nullptr;
    std::unordered_map<uint32, bool> needForQuestMap;
    uint32 rejLootId = 0, rejNpcFlag = 0, rejXp = 0, rejZ = 0, rejAssist = 0, rejFarMaster = 0, rejLevel = 0,
           rejElite = 0, rejLos = 0, rejAggro = 0;

    for (ObjectGuid const guid : targets)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (!unit->IsInWorld() || unit->IsDuringRemoveFromWorld())
            continue;

        if (unit->ToCreature() && !PlayerbotCreature::GetLootId(unit->ToCreature()->GetCreatureTemplate()) &&
            bot->GetReactionTo(unit) >= REP_NEUTRAL)
        {
            ++rejLootId;
            continue;
        }

        if (!bot->IsHostileTo(unit) && unit->GetNpcFlags() != UNIT_NPC_FLAG_NONE)
        {
            ++rejNpcFlag;
            continue;
        }

        if (!bot->isHonorOrXPTarget(unit))
        {
            ++rejXp;
            continue;
        }

        if (abs(bot->GetPositionZ() - unit->GetPositionZ()) > INTERACTION_DISTANCE)
        {
            ++rejZ;
            continue;
        }

        if (!bot->InBattleground() && GetTargetingPlayerCount(unit) > assistCount)
        {
            ++rejAssist;
            continue;
        }

        // if (!bot->InBattleground() && master && master->GetDistance(unit) >= sPlayerbotAIConfig.GrindDistance &&
        // !sRandomPlayerbotMgr.IsRandomBot(bot)) continue;

        // Bots in bot-groups no have a more limited range to look for grind target
        if (!bot->InBattleground() && master && botAI->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) &&
            ServerFacade::instance().GetDistance2d(master, unit) > sPlayerbotAIConfig.LootDistance)
        {
            ++rejFarMaster;
            if (botAI->HasStrategy("debug grind", BotState::BOT_STATE_NON_COMBAT))
                botAI->TellMaster(chat->FormatWorldobject(unit) + " ignored (far from master).");
            continue;
        }

        if (!bot->InBattleground() && (int)unit->GetLevel() - (int)bot->GetLevel() > 4 && !unit->GetGUID().IsPlayer())
        {
            ++rejLevel;
            continue;
        }

        if (Creature* creature = unit->ToCreature())
            if (CreatureTemplate const* CreatureTemplate = creature->GetCreatureTemplate())
                if (PlayerbotCreature::GetRank(CreatureTemplate) > CREATURE_ELITE_NORMAL && !AI_VALUE(bool, "can fight elite"))
                {
                    ++rejElite;
                    continue;
                }

        if (!bot->IsWithinLOSInMap(unit))
        {
            ++rejLos;
            continue;
        }

        bool inactiveGrindStatus = botAI->rpgInfo.GetStatus() != RPG_WANDER_RANDOM && botAI->rpgInfo.GetStatus() != RPG_IDLE;

        float aggroRange = 30.0f;
        if (unit->ToCreature())
            aggroRange = std::min(30.0f, unit->ToCreature()->GetAggroRange(bot) + 10.0f);
        bool outOfAggro = unit->ToCreature() && bot->GetDistance(unit) > aggroRange;
        if (inactiveGrindStatus && outOfAggro)
        {
            if (needForQuestMap.find(unit->GetEntry()) == needForQuestMap.end())
                needForQuestMap[unit->GetEntry()] = needForQuest(unit);

            if (!needForQuestMap[unit->GetEntry()])
            {
                ++rejAggro;
                continue;
            }
        }

        if (group)
        {
            Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
            for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
            {
                Player* member = ObjectAccessor::FindPlayer(itr->guid);
                if (!member || !member->IsAlive())
                    continue;

                float d = member->GetDistance(unit);
                if (!result || d < distance)
                {
                    distance = d;
                    result = unit;
                }
            }
        }
        else
        {
            float newdistance = bot->GetDistance(unit);
            if (!result || (newdistance < distance))
            {
                distance = newdistance;
                result = unit;
            }
        }
    }

    if (!result)
        TC_LOG_DEBUG("playerbots",
                     "[grind-diag] {} rejected all {} targets: lootid={} npcflag={} xp={} z={} assist={} farMaster={} "
                     "lvl={} elite={} los={} aggro={}",
                     bot->GetName(), targets.size(), rejLootId, rejNpcFlag, rejXp, rejZ, rejAssist, rejFarMaster,
                     rejLevel, rejElite, rejLos, rejAggro);

    return result;
}

bool GrindTargetValue::needForQuest(Unit* target)
{
    QuestStatusMap& questMap = bot->getQuestStatusMap();
    for (auto& quest : questMap)
    {
        Quest const* questTemplate = sObjectMgr->GetQuestTemplate(quest.first);
        if (!questTemplate)
            continue;

        uint32 questId = questTemplate->GetQuestId();
        if (!questId)
            continue;

        QuestStatus status = bot->GetQuestStatus(questId);

        if (status == QUEST_STATUS_INCOMPLETE)
        {
            QuestStatusData const* questStatus = &bot->getQuestStatusMap()[questId];

            if (questTemplate->GetClassicQuestLevel() > bot->GetLevel() + 5)
                continue;

            for (QuestObjective const& objective : questTemplate->GetObjectives())
            {
                if (objective.Type != QUEST_OBJECTIVE_MONSTER || objective.ObjectID <= 0)
                    continue;

                if (objective.Amount <= 0 || target->GetEntry() != uint32(objective.ObjectID))
                    continue;

                if (PlayerbotQuest::GetObjectiveProgress(bot, questTemplate, objective) < objective.Amount)
                    return true;
            }
        }
    }

    if (CreatureTemplate const* data = sObjectMgr->GetCreatureTemplate(target->GetEntry()))
    {
        if (uint32 lootId = PlayerbotCreature::GetLootId(data))
        {
            if (LootTemplates_Creature.HaveQuestLootForPlayer(lootId, bot))
            {
                return true;
            }
        }
    }

    return false;
}

uint32 GrindTargetValue::GetTargetingPlayerCount(Unit* unit)
{
    Group* group = bot->GetGroup();
    if (!group)
        return 0;

    uint32 count = 0;
    Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
    for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
    {
        Player* member = ObjectAccessor::FindPlayer(itr->guid);
        if (!member || !member->IsAlive() || member == bot)
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(member);
        if ((botAI && *botAI->GetAiObjectContext()->GetValue<Unit*>("current target") == unit) ||
            (!botAI && member->GetTarget() == unit->GetGUID()))
            ++count;
    }

    return count;
}
