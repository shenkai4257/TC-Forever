/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_QUESTHELPER_H
#define PLAYERBOTS_QUESTHELPER_H

#include "Player.h"
#include "QuestDef.h"

// AC kept a quest's objectives as flat fixed-size arrays (RequiredNpcOrGo[4],
// RequiredItemId[6], ...) and the per-objective progress in QuestStatusData
// (CreatureOrGOCount/ItemCount). 12.x keeps objectives as a structured
// QuestObjectives list and the progress in the quest log, keyed by objective.
// These helpers give the ported call sites the same "does the bot still need
// this?" answer without hand-rolling the log walk every time.
// 3.3.5 QuestGiverStatus values. 12.x replaced the enum with a different bit-flag set,
// but the ported dialog-status ranking only orders these numbers, so keep the old scale.
enum PlayerbotDialogStatus : uint32
{
    DIALOG_STATUS_NONE                     = 0,
    DIALOG_STATUS_UNAVAILABLE              = 1,
    DIALOG_STATUS_LOW_LEVEL_AVAILABLE      = 2,
    DIALOG_STATUS_LOW_LEVEL_REWARD_REP     = 3,
    DIALOG_STATUS_LOW_LEVEL_AVAILABLE_REP  = 4,
    DIALOG_STATUS_INCOMPLETE               = 5,
    DIALOG_STATUS_REWARD_REP               = 6,
    DIALOG_STATUS_AVAILABLE_REP            = 7,
    DIALOG_STATUS_AVAILABLE                = 8,
    DIALOG_STATUS_REWARD2                  = 9,   // no yellow dot on minimap
    DIALOG_STATUS_REWARD                   = 10,  // yellow dot on minimap

    DIALOG_STATUS_SCRIPTED_NO_STATUS       = 0x1000,
};

namespace PlayerbotQuest
{
    // Progress the bot holds for one objective, 0 when the quest is not in the log.
    inline int32 GetObjectiveProgress(Player const* bot, Quest const* quest, QuestObjective const& objective)
    {
        if (!bot || !quest || bot->GetQuestStatus(quest->GetQuestId()) != QUEST_STATUS_INCOMPLETE)
            return 0;

        uint16 const slot = bot->FindQuestSlot(quest->GetQuestId());
        if (slot >= MAX_QUEST_LOG_SIZE)
            return 0;

        return bot->GetQuestSlotObjectiveData(slot, objective);
    }

    // 3.3.5 Quest::IsAutoComplete(): the quest has no objectives to work on.
    inline bool IsQuestAutoComplete(Quest const* quest)
    {
        if (!quest)
            return false;

        return quest->HasFlag(QUEST_FLAGS_AUTO_COMPLETE);
    }

    // 3.3.5 Quest::RequiredItemId[]/RequiredItemCount[] lookup: required amount of an
    // item objective, 0 when the quest does not require the item.
    inline uint32 GetRequiredItemAmount(Quest const* quest, uint32 itemId)
    {
        if (!quest)
            return 0;

        for (QuestObjective const& objective : quest->GetObjectives())
            if (objective.Type == QUEST_OBJECTIVE_ITEM && objective.ObjectID == int32(itemId) && objective.Amount > 0)
                return uint32(objective.Amount);

        return 0;
    }

    // True when the bot is on the quest and still short of every objective of
    // this type whose ObjectID matches (0 matches any object of that type).
    inline bool NeedsObjective(Player const* bot, Quest const* quest, QuestObjectiveType type, int32 objectId = 0)
    {
        if (!bot || !quest || bot->GetQuestStatus(quest->GetQuestId()) != QUEST_STATUS_INCOMPLETE)
            return false;

        uint16 const slot = bot->FindQuestSlot(quest->GetQuestId());
        if (slot >= MAX_QUEST_LOG_SIZE)
            return false;

        for (QuestObjective const& objective : quest->GetObjectives())
        {
            if (objective.Type != type)
                continue;
            if (objectId && objective.ObjectID != objectId)
                continue;
            if (objective.Amount <= 0)
                continue;
            if (bot->GetQuestSlotObjectiveData(slot, objective) < objective.Amount)
                return true;
        }

        return false;
    }
}

#endif
