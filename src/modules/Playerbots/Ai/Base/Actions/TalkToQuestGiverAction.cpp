/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TalkToQuestGiverAction.h"
#include "BroadcastHelper.h"
#include "ByteBuffer.h"
#include "ChatHelper.h"
#include "Event.h"
#include "ItemPacketsCommon.h"
#include "ItemUsageValue.h"
#include "Object.h"
#include "PacketOperators.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "StatsWeightCalculator.h"
#include "WorldPacket.h"

namespace
{
    // Skips the reward block of SMSG_QUEST_GIVER_OFFER_REWARD_MESSAGE so the quest giver guid and quest id
    // that follow it can be read (WorldPackets::Quest::QuestGiverOfferReward::operator<<). Every optional
    // context flag is one bit padded to a byte.
    void SkipQuestRewards(WorldPacket& p)
    {
        auto skipContextFlag = [&p]() -> bool
        {
            bool hasContext = false;
            p >> WorldPackets::Bits<1>(hasContext);
            p.ResetBitPos();
            return hasContext;
        };

        for (int32 i = 0; i < QUEST_REWARD_ITEM_COUNT; ++i)
        {
            p.read_skip(2 * sizeof(int32_t));            // ItemID, ItemQty
            if (skipContextFlag())
                p.read_skip<int32_t>();
        }

        for (int32 i = 0; i < QUEST_REWARD_CURRENCY_COUNT; ++i)
        {
            p.read_skip(3 * sizeof(int32_t));            // CurrencyID, CurrencyQty, BonusQty
            if (skipContextFlag())
                p.read_skip<int32_t>();
        }

        p.read_skip<int32_t>();                          // ChoiceItemCount
        for (int32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            uint8 choiceBits = 0;                        // 2 bit loot item type, then the context flag
            p >> WorldPackets::Bits<3>(choiceBits);
            bool const hasContext = (choiceBits & 1) != 0;
            WorldPackets::Item::ItemInstance item;       // its first read resets the bit position
            p >> item;
            p.read_skip<int32_t>();                      // Quantity
            if (hasContext)
                p.read_skip<int32_t>();
        }

        p.read_skip(3 * sizeof(int32_t));                // ItemCount, Money, XP
        p.read_skip<int64_t>();                          // ArtifactXP
        p.read_skip(4 * sizeof(int32_t));                // ArtifactCategoryID, Honor, Title, FactionFlags
        p.read_skip(QUEST_REWARD_REPUTATIONS_COUNT * 4 * sizeof(int32_t));
        p.read_skip(QUEST_REWARD_DISPLAY_SPELL_COUNT * sizeof(int32_t));
        p.read_skip(3 * sizeof(int32_t));                // SpellCompletionID, SkillLineID, NumSkillUps

        uint32 pickerCount = 0;
        p >> pickerCount;
        p.read_skip(pickerCount * sizeof(int32_t));
        p.read_skip<uint8_t>();                          // IsBoostSpell bit + padding
    }

    // Reads the quest giver guid and quest id out of SMSG_QUEST_GIVER_OFFER_REWARD_MESSAGE.
    bool ReadOfferRewardQuest(WorldPacket& p, ObjectGuid& questGiver, int32& questId)
    {
        try
        {
            p.rpos(0);
            SkipQuestRewards(p);

            int32 emoteCount = 0;
            p >> emoteCount >> questGiver;
            p.read_skip(5 * sizeof(int32_t));            // QuestFlags[4], QuestGiverCreatureID
            p >> questId;
            return true;
        }
        catch (ByteBufferException const&)
        {
            return false;
        }
    }
}

bool TalkToQuestGiverAction::ProcessQuest(Quest const* quest, Object* questGiver)
{
    bool isCompleted = false;
    std::ostringstream out;

    QuestStatus status = bot->GetQuestStatus(quest->GetQuestId());
    Player* master = GetMaster();

    if (sPlayerbotAIConfig.SyncQuestForPlayer && master)
    {
        PlayerbotAI* masterBotAI = GET_PLAYERBOT_AI(master);
        if (!masterBotAI || IsSelfBot(master))
        {
            QuestStatus masterStatus = master->GetQuestStatus(quest->GetQuestId());
            if (masterStatus == QUEST_STATUS_INCOMPLETE || masterStatus == QUEST_STATUS_FAILED)
                isCompleted |= CompleteQuest(master, quest->GetQuestId());
        }
    }

    if (sPlayerbotAIConfig.SyncQuestWithPlayer)
    {
        if (master && master->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_COMPLETE &&
            (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_FAILED))
        {
            isCompleted |= CompleteQuest(bot, quest->GetQuestId());
            status = bot->GetQuestStatus(quest->GetQuestId());
        }
    }

    // The status words used to be hard coded English with chat colour codes, so every bot said
    // "Quest Available: ..." whatever the client language; the sentences now come from the text
    // table (Chinese included).
    std::map<std::string, std::string> placeholders;
    placeholders["%quest"] = chat->FormatQuest(quest);

    switch (status)
    {
    case QUEST_STATUS_COMPLETE:
        out << "Quest ";
        isCompleted |= TurnInQuest(quest, questGiver, out);
        out << ": " << chat->FormatQuest(quest);
        break;
    case QUEST_STATUS_INCOMPLETE:
        out << PlayerbotTextMgr::instance().GetBotTextOrDefault("quest_status_incomplete",
                                                                 "I have not completed the quest %quest", placeholders);
        break;
    case QUEST_STATUS_NONE:
        out << PlayerbotTextMgr::instance().GetBotTextOrDefault("quest_status_available",
                                                                 "Quest %quest available", placeholders);
        break;
    case QUEST_STATUS_FAILED:
        out << PlayerbotTextMgr::instance().GetBotTextOrDefault("quest_status_failed",
                                                                 "I have failed the quest %quest", placeholders);
        break;
    default:
        break;
    }

    botAI->TellMaster(out);

    return isCompleted;
}

bool TalkToQuestGiverAction::TurnInQuest(Quest const* quest, Object* questGiver, std::ostringstream& out)
{
    uint32 questID = quest->GetQuestId();

    if (bot->GetQuestRewardStatus(questID))
        return false;

    bot->PlayDistanceSound(621);

    if (quest->GetRewChoiceItemsCount() == 0)
        RewardNoItem(quest, questGiver, out);
    else if (quest->GetRewChoiceItemsCount() == 1)
        RewardSingleItem(quest, questGiver, out);
    else
    {
        RewardMultipleItem(quest, questGiver, out);
    }

    if (botAI->HasStrategy("debug quest", BotState::BOT_STATE_NON_COMBAT) || botAI->HasStrategy("debug rpg", BotState::BOT_STATE_COMBAT))
    {
        Quest const* pQuest = sObjectMgr->GetQuestTemplate(questID);
        const std::string text_quest = ChatHelper::FormatQuest(pQuest);
        TC_LOG_INFO("playerbots", "{} => Quest [ {} ] completed", bot->GetName(), pQuest->GetLogTitle());
        bot->Say("Quest [ " + text_quest + " ] completed", LANG_UNIVERSAL);
    }

    return true;
}

void TalkToQuestGiverAction::RewardNoItem(Quest const* quest, Object* questGiver, std::ostringstream& out)
{
    std::map<std::string, std::string> args;
    args["%quest"] = chat->FormatQuest(quest);

    if (bot->CanRewardQuest(quest, false))
    {
        out << PlayerbotTextMgr::instance().GetBotText("quest_status_completed", args);
        BroadcastHelper::BroadcastQuestTurnedIn(botAI, bot, quest);

        bot->RewardQuest(quest, LootItemType::Item, 0, questGiver, false);
    }
    else
    {
        out << PlayerbotTextMgr::instance().GetBotText("quest_status_unable_to_complete", args);
    }
}

void TalkToQuestGiverAction::RewardSingleItem(Quest const* quest, Object* questGiver, std::ostringstream& out)
{
    int index = 0;
    ItemTemplate const* item = sObjectMgr->GetItemTemplate(quest->RewardChoiceItemId[index]);
    // RewardChoiceItemId can be 0 for quests without a choice reward; FormatItem would deref null.
    if (!item)
        return;
    std::map<std::string, std::string> args;
    args["%quest"] = chat->FormatQuest(quest);
    args["%item"] = chat->FormatItem(item);

    if (bot->CanRewardQuest(quest, LootItemType::Item, index, false))
    {
        out << PlayerbotTextMgr::instance().GetBotText("quest_status_complete_single_reward", args);
        BroadcastHelper::BroadcastQuestTurnedIn(botAI, bot, quest);
        bot->RewardQuest(quest, LootItemType::Item, index, questGiver, true);
    }
    else
    {
        out << PlayerbotTextMgr::instance().GetBotText("quest_status_unable_to_complete", args);
    }
}

ItemIds TalkToQuestGiverAction::BestRewards(Quest const* quest)
{
    ItemIds returnIds;
    ItemUsage bestUsage = ITEM_USAGE_NONE;
    if (quest->GetRewChoiceItemsCount() == 0)
        return returnIds;
    else if (quest->GetRewChoiceItemsCount() == 1)
        return {0};
    else
    {
        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewardChoiceItemId[i]);
            if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE)
                bestUsage = ITEM_USAGE_EQUIP;
            else if (usage == ITEM_USAGE_BAD_EQUIP && bestUsage != ITEM_USAGE_EQUIP)
                bestUsage = usage;
            else if (usage != ITEM_USAGE_NONE && bestUsage == ITEM_USAGE_NONE)
                bestUsage = usage;
        }
        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewardChoiceItemId[i]);
            if (usage == bestUsage || usage == ITEM_USAGE_REPLACE)
                returnIds.insert(i);
        }
        return returnIds;
    }
}

void TalkToQuestGiverAction::RewardMultipleItem(Quest const* quest, Object* questGiver, std::ostringstream& out)
{
    std::set<uint32> bestIds;

    std::ostringstream outid;
    if (!botAI->IsAltBot() || sPlayerbotAIConfig.AutoPickReward == "yes")
    {
        bestIds = BestRewards(quest);
        if (!bestIds.empty())
        {
            StatsWeightCalculator calc(bot);
            uint32 best = 0;
            float bestScore = 0;
            for (uint32 id : bestIds)
            {
                uint32 const itemId = quest->RewardChoiceItemId[id];
                ItemTemplate const* rewardProto = sObjectMgr->GetItemTemplate(itemId);
                uint8 rewardSlot = NULL_SLOT;
                if (rewardProto)
                    rewardSlot = botAI->FindEquipSlot(rewardProto, NULL_SLOT, true);
                float score = calc.CalculateItem(itemId, 0, rewardSlot);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = id;
                }
            }
            ItemTemplate const* item = sObjectMgr->GetItemTemplate(quest->RewardChoiceItemId[best]);
            bot->RewardQuest(quest, LootItemType::Item, best, questGiver, true);
            if (!item)
            {
                out << PlayerbotTextMgr::instance().GetBotText("quest_no_suitable_reward", {});
                return;
            }
            out << PlayerbotTextMgr::instance().GetBotText("quest_reward_received",
                                                             {{"%item", ChatHelper::FormatItem(item)}});
        }
        else
        {
            out << PlayerbotTextMgr::instance().GetBotText("quest_no_suitable_reward", {});
            AskToSelectReward(quest, out, true);
        }
    }
    else if (sPlayerbotAIConfig.AutoPickReward == "no")
    {
        // Old functionality, list rewards.
        AskToSelectReward(quest, out, false);
    }
    else
    {
        // Try to pick the usable item. If multiple, list usable rewards.
        bestIds = BestRewards(quest);

        if (bestIds.size() > 1)
            AskToSelectReward(quest, out, true);

        else if (!bestIds.empty())
        {
            // Pick the first item
            uint32 firstId = *bestIds.begin();
            ItemTemplate const* item = sObjectMgr->GetItemTemplate(quest->RewardChoiceItemId[firstId]);
            bot->RewardQuest(quest, LootItemType::Item, firstId, questGiver, true);
            if (!item)
            {
                out << PlayerbotTextMgr::instance().GetBotText("quest_no_suitable_reward", {});
                return;
            }

            out << PlayerbotTextMgr::instance().GetBotText("quest_reward_received",
                                                             {{"%item", ChatHelper::FormatItem(item)}});
        }
    }
}

void TalkToQuestGiverAction::AskToSelectReward(Quest const* quest, std::ostringstream& out, bool forEquip)
{
    std::ostringstream msg;
    msg << "Choose reward: ";

    for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
    {
        ItemTemplate const* item = sObjectMgr->GetItemTemplate(quest->RewardChoiceItemId[i]);
        if (!item)
            continue;

        if (!forEquip || BestRewards(quest).count(i) > 0)
        {
            msg << chat->FormatItem(item);
        }
    }

    botAI->TellMaster(msg);
    out << "Reward pending";
}

bool TurnInQueryQuestAction::Execute(Event event)
{
    ObjectGuid guid;
    int32 questId = 0;
    if (!ReadOfferRewardQuest(event.getPacket(), guid, questId))
        return false;

    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return false;

    Object* object =
        ObjectAccessor::GetObjectByTypeMask(*bot, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT | TYPEMASK_ITEM);
    if (!object || (!object->hasQuest(questId) && !object->hasInvolvedQuest(questId)))
    {
        return false;
    }
    QuestStatus status = bot->GetQuestStatus(quest->GetQuestId());
    Player* master = GetMaster();

    if (sPlayerbotAIConfig.SyncQuestForPlayer && master)
    {
        PlayerbotAI* masterBotAI = GET_PLAYERBOT_AI(master);
        if (!masterBotAI || IsSelfBot(master))
        {
            QuestStatus masterStatus = master->GetQuestStatus(quest->GetQuestId());
            if (masterStatus == QUEST_STATUS_INCOMPLETE || masterStatus == QUEST_STATUS_FAILED)
                CompleteQuest(master, quest->GetQuestId());
        }
    }

    if (sPlayerbotAIConfig.SyncQuestWithPlayer)
    {
        if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_FAILED)
        {
            CompleteQuest(bot, quest->GetQuestId());
            status = bot->GetQuestStatus(quest->GetQuestId());
        }
    }
    std::ostringstream out;

    // Same as ProcessQuest: the status sentence comes from the text table instead of hard coded
    // English so bot chat follows the client language.
    std::map<std::string, std::string> placeholders;
    placeholders["%quest"] = chat->FormatQuest(quest);

    switch (status)
    {
    case QUEST_STATUS_COMPLETE:
        out << "Quest ";
        TurnInQuest(quest, object, out);
        out << ": " << chat->FormatQuest(quest);
        break;
    case QUEST_STATUS_INCOMPLETE:
        out << PlayerbotTextMgr::instance().GetBotTextOrDefault("quest_status_incomplete",
                                                                 "I have not completed the quest %quest", placeholders);
        break;
    case QUEST_STATUS_NONE:
        out << PlayerbotTextMgr::instance().GetBotTextOrDefault("quest_status_available",
                                                                 "Quest %quest available", placeholders);
        break;
    case QUEST_STATUS_FAILED:
        out << PlayerbotTextMgr::instance().GetBotTextOrDefault("quest_status_failed",
                                                                 "I have failed the quest %quest", placeholders);
        break;
    case QUEST_STATUS_REWARDED:
        out << PlayerbotTextMgr::instance().GetBotTextOrDefault("quest_status_completed",
                                                                 "I have completed the quest %quest", placeholders);
        break;
    default:
        break;
    }

    botAI->TellMaster(out);
    return true;
}
