/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QueryQuestAction.h"
#include "QuestHelper.h"
#include "ChatHelper.h"
#include "Event.h"
#include "PlayerbotAI.h"

void QueryQuestAction::TellObjective(std::string const name, uint32 available, uint32 required)
{
    botAI->TellMaster(chat->FormatQuestObjective(name, available, required));
}

bool QueryQuestAction::Execute(Event event)
{
    Player* bot = botAI->GetBot();
    WorldPosition botPos(bot);
    WorldPosition* ptr_botpos = &botPos;
    std::string text = event.getParam();
    bool travel = false;

    if (text.find("travel") != std::string::npos)
    {
        travel = true;
        chat->eraseAllSubStr(text, " travel");
    }

    PlayerbotChatHandler ch(bot);
    uint32 questId = ch.extractQuestId(text);
    if (!questId)
    {
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 logQuest = bot->GetQuestSlotQuestId(slot);

            Quest const* quest = sObjectMgr->GetQuestTemplate(logQuest);
            if (!quest)
                continue;

            if (text.find(quest->GetLogTitle()) != std::string::npos)
            {
                questId = quest->GetQuestId();
                break;
            }
        }
    }

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        if (questId != bot->GetQuestSlotQuestId(slot))
            continue;

        std::ostringstream out;
        out << "--- " << chat->FormatQuest(sObjectMgr->GetQuestTemplate(questId)) << " ";

        if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
        {
            out << "|c0000FF00completed|r ---";
            botAI->TellMaster(out);
        }
        else
        {
            out << "|c00FF0000not completed|r ---";
            botAI->TellMaster(out);
            TellObjectives(questId);
        }

        if (travel)
        {
            uint32 limit = 0;
            std::vector<TravelDestination*> allDestinations =
                TravelMgr::instance().getQuestTravelDestinations(bot, questId, true, true, -1);

            std::sort(allDestinations.begin(), allDestinations.end(), [ptr_botpos](TravelDestination* i, TravelDestination* j) {return i->distanceTo(ptr_botpos) < j->distanceTo(ptr_botpos); });
            for (auto dest : allDestinations)
            {
                if (limit > 50)
                    continue;

                std::ostringstream out;

                uint32 tpoints = dest->getPoints(true).size();
                uint32 apoints = dest->getPoints().size();

                out << round(dest->distanceTo(&botPos));
                out << " to " << dest->getTitle();
                out << " " << apoints;

                if (apoints < tpoints)
                    out << "/" << tpoints;

                out << " points.";

                if (!dest->isActive(bot))
                    out << " not active";

                botAI->TellMaster(out);

                limit++;
            }
        }

        return true;
    }

    return false;
}

void QueryQuestAction::TellObjectives(uint32 questId)
{
    Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);

    // Checks if the questTemplate is valid
    if (!questTemplate)
    {
        botAI->TellMaster("Quest template not found.");
        return;
    }

    for (QuestObjective const& objective : questTemplate->GetObjectives())
    {
        // Checks for objective text
        if (!objective.Description.empty())
            botAI->TellMaster(objective.Description);

        uint32 const required = uint32(std::max(0, objective.Amount));
        if (!required)
            continue;

        uint32 const available = uint32(std::max(0, PlayerbotQuest::GetObjectiveProgress(bot, questTemplate, objective)));

        // Checks for required items
        if (objective.Type == QUEST_OBJECTIVE_ITEM && objective.ObjectID > 0)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(uint32(objective.ObjectID));
            if (proto)
                TellObjective(chat->FormatItem(proto), available, required);
        }
        // Checks for required NPCs or GOs
        else if (objective.Type == QUEST_OBJECTIVE_MONSTER && objective.ObjectID > 0)
        {
            if (CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(uint32(objective.ObjectID)))
                TellObjective(info->Name, available, required);
        }
        else if (objective.Type == QUEST_OBJECTIVE_GAMEOBJECT && objective.ObjectID > 0)
        {
            if (GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(uint32(objective.ObjectID)))
                TellObjective(info->name, available, required);
        }
    }
}
