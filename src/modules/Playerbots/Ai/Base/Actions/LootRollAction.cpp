/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LootRollAction.h"
#include "Event.h"
#include "Group.h"
#include "ItemUsageValue.h"
#include "LootAction.h"
#include "Loot.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

bool LootRollAction::Execute(Event /*event*/)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    bool voted = false;
    // 12.x keeps the in-flight rolls on the player: LootRoll exposes the item and the
    // vote mask, and PlayerVote() refuses a second vote for the same roll.
    for (LootRoll* roll : bot->GetLootRolls())
    {
        if (!roll->IsStarted())
            continue;

        LootItem const* lootItem = roll->GetItem();
        if (!lootItem)
            continue;

        uint32 const itemId = lootItem->itemid;
        RollMask const voteMask = roll->GetVoteMask();

        RollVote vote = RollVote::Pass;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        // 12.x loot carries bonus list ids instead of 3.3.5 random property/suffix ids;
        // query the item usage by the plain item id.
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", std::to_string(itemId));

        // Armor Tokens are classed as MISC JUNK (Class 15, Subclass 0), luckily no other items I found have class bits and epic quality.
        if (proto->GetClass() == ITEM_CLASS_MISCELLANEOUS && proto->GetSubClass() == 0 &&
            proto->GetQuality() == ITEM_QUALITY_EPIC)
        {
            if (CanBotUseToken(proto, bot))
                vote = RollVote::Need;  // Eligible for "Need"
            else
                vote = RollVote::Greed; // Not eligible, so "Greed"
        }
        else if (usage == ITEM_USAGE_DISENCHANT)
            vote = sPlayerbotAIConfig.LootRollDisenchant ? RollVote::Disenchant : RollVote::Greed;
        else
        {
            switch (proto->GetClass())
            {
                case ITEM_CLASS_WEAPON:
                case ITEM_CLASS_ARMOR:
                    if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
                    {
                        if (!RollUniqueCheck(proto, bot))
                            vote = RollVote::Need;
                        else if (sPlayerbotAIConfig.LootRollDisenchant && CanBotDisenchant(proto, bot))
                            vote = RollVote::Disenchant;
                        else
                            vote = RollVote::Greed;
                    }
                    else if (usage != ITEM_USAGE_NONE)
                        vote = RollVote::Greed;
                    break;
                case ITEM_CLASS_RECIPE:
                    if (!sPlayerbotAIConfig.LootRollRecipe)
                        vote = RollVote::Pass;
                    else if (usage == ITEM_USAGE_SKILL)
                        vote = RollVote::Need;  // Bot can learn this recipe
                    else if (proto->GetBonding() != BIND_ON_ACQUIRE)
                        vote = RollVote::Greed; // BoE recipe bot can't learn - GREED for AH/trade
                    break;
                default:
                    if (StoreLootAction::IsLootAllowed(itemId, botAI))
                        vote = CalculateRollVote(proto, usage);
                    break;
            }
        }
        if (vote == RollVote::Need)
        {
            if (sPlayerbotAIConfig.LootNeedRollLevel == 0)
                vote = RollVote::Pass;
            else if (sPlayerbotAIConfig.LootNeedRollLevel == 1)
                vote = RollVote::Greed;
        }
        else if (vote == RollVote::Greed && !sPlayerbotAIConfig.LootGreedRollLevel)
            vote = RollVote::Pass;

        // In master loot and free-for-all the roll is not offered to the bots.
        switch (group->GetLootMethod())
        {
            case MASTER_LOOT:
            case FREE_FOR_ALL:
                vote = RollVote::Pass;
                break;
            default:
                break;
        }

        // Never send a vote the roll does not offer (PlayerVote would refuse it anyway).
        if (vote == RollVote::Need && !(voteMask & ROLL_FLAG_TYPE_NEED))
            vote = (voteMask & ROLL_FLAG_TYPE_GREED) ? RollVote::Greed : RollVote::Pass;
        if (vote == RollVote::Greed && !(voteMask & ROLL_FLAG_TYPE_GREED))
            vote = RollVote::Pass;
        if (vote == RollVote::Disenchant && !(voteMask & ROLL_FLAG_TYPE_DISENCHANT))
            vote = (voteMask & ROLL_FLAG_TYPE_GREED) ? RollVote::Greed : RollVote::Pass;

        if (roll->PlayerVote(bot, vote))
            voted = true;
    }

    return voted;
}

RollVote LootRollAction::CalculateRollVote(ItemTemplate const* proto, ItemUsage usage)
{
    if (usage == ITEM_USAGE_NONE)
    {
        std::ostringstream out;
        out << proto->GetId();
        usage = AI_VALUE2(ItemUsage, "item usage", out.str());
    }

    RollVote needVote = RollVote::Pass;
    switch (usage)
    {
        case ITEM_USAGE_EQUIP:
        case ITEM_USAGE_REPLACE:
        case ITEM_USAGE_GUILD_TASK:
        case ITEM_USAGE_BAD_EQUIP:
            needVote = RollVote::Need;
            break;
        case ITEM_USAGE_SKILL:
        case ITEM_USAGE_USE:
        case ITEM_USAGE_AH:
        case ITEM_USAGE_VENDOR:
            needVote = RollVote::Greed;
            break;
        case ITEM_USAGE_DISENCHANT:
            needVote = sPlayerbotAIConfig.LootRollDisenchant ? RollVote::Disenchant : RollVote::Greed;
            break;
        default:
            break;
    }

    return StoreLootAction::IsLootAllowed(proto->GetId(), GET_PLAYERBOT_AI(bot)) ? needVote : RollVote::Pass;
}

bool MasterLootRollAction::isUseful() { return !IsRealPlayer(botAI->GetMaster()); }

bool MasterLootRollAction::Execute(Event event)
{
    // 12.x announces a roll with a structured SMSG_LOOT_ROLL whose layout no longer
    // matches the 3.3.5 packet the AC code parsed; the live LootRoll already knows the item,
    // so vote from that state instead. Master loot is decided by the master afterwards.
    return LootRollAction::Execute(event);
}

bool CanBotUseToken(ItemTemplate const* proto, Player* bot)
{
    // Get the bitmask for the bot's class
    uint32 botClassMask = (1 << (bot->GetClass() - 1));

    // Check if the bot's class is allowed to use the token
    if (proto->GetAllowableClass() & botClassMask)
        return true; // Bot's class is eligible to use this token

    return false; // Bot's class cannot use this token
}

bool RollUniqueCheck(ItemTemplate const* proto, Player* bot)
{
    if (proto->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE) || proto->GetMaxCount() == 1)
        return (bot->GetItemCount(proto->GetId(), true) > 0);

    return false;
}

bool CanBotDisenchant(ItemTemplate const* proto, Player* bot)
{
    // 12.x has no DisenchantID/RequiredDisenchantSkill on the item template: resolve the
    // disenchant loot entry the same way the core does and compare its skill requirement.
    if (proto->GetBonding() != BIND_ON_ACQUIRE || !bot->HasSkill(SKILL_ENCHANTING))
        return false;

    ItemDisenchantLootEntry const* disenchant =
        Item::GetBaseDisenchantLoot(proto, proto->GetQuality(), proto->GetBaseItemLevel());
    return disenchant && bot->GetSkillValue(SKILL_ENCHANTING) >= disenchant->SkillRequired;
}

bool RollAction::Execute(Event event)
{
    std::string link = event.getParam();

    if (link.empty())
    {
        bot->DoRandomRoll(1, 100);
        return false;
    }
    ItemIds itemIds = chat->parseItems(link);
    if (itemIds.empty())
        return false;
    uint32 itemId = *itemIds.begin();
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;

    if (RollUniqueCheck(proto, bot))
        return false;

    std::string itemUsageParam;
    itemUsageParam = std::to_string(itemId);

    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemUsageParam);
    switch (proto->GetClass())
    {
        case ITEM_CLASS_WEAPON:
        case ITEM_CLASS_ARMOR:
        if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
            bot->DoRandomRoll(1, 100);
    }
    return true;
}
