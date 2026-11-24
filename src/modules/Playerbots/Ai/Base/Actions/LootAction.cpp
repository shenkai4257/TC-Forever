/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LootAction.h"
#include "LootPackets.h"
#include "PacketOperators.h"
#include "ItemPacketsCommon.h"

#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "Event.h"
#include "GuildMgr.h"
#include "GuildTaskMgr.h"
#include "ItemUsageValue.h"
#include "LootObjectStack.h"
#include "LootStrategyValue.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotSpellRepository.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "UseItemAction.h"

bool LootAction::Execute(Event /*event*/)
{
    if (AI_VALUE(LootObjectStack*, "available loot")->IsLootPending() || !bot->GetLootGUID().IsEmpty() ||
        bot->IsNonMeleeSpellCast(false))
        return false;

    LootObject lootObject = AI_VALUE(LootObject, "nearest loot");
    if (lootObject.IsEmpty() || !AI_VALUE(LootObjectStack*, "available loot")->CanAttemptLoot(lootObject.guid))
        return false;

    LootObject prevLoot = AI_VALUE(LootObject, "loot target");
    if (!prevLoot.IsEmpty() && prevLoot.guid != lootObject.guid)
    {
        // 12.x handlers take structured packets; the bot runs on the world
        // thread, so the AC "queue a forged CMSG" dance is not needed.
        WorldPacket payload(CMSG_LOOT_RELEASE, 16);
        WorldPackets::Loot::LootRelease release(std::move(payload));
        release.Unit = prevLoot.guid;
        bot->GetSession()->HandleLootReleaseOpcode(release);
    }

    context->GetValue<LootObject>("loot target")->Set(lootObject);
    return true;
}

bool LootAction::isUseful()
{
    if (!sPlayerbotAIConfig.FreeMethodLoot && bot->GetGroup() && bot->GetGroup()->GetLootMethod() == FREE_FOR_ALL &&
        !IsSelfBot(bot))
        return false;

    LootObject lootTarget = AI_VALUE(LootObject, "loot target");
    LootObject nearest = AI_VALUE(LootObject, "nearest loot");
    if (lootTarget.IsEmpty() || nearest.IsEmpty() || nearest.guid == lootTarget.guid ||
        !AI_VALUE(LootObjectStack*, "available loot")->CanAttemptLoot(lootTarget.guid))
        return true;

    // Plain lookups: LootObject::GetWorldObject re-runs Refresh, which IsLootPossible below already pays.
    WorldObject* nearestObject = botAI->GetWorldObject(nearest.guid);
    WorldObject* currentObject = botAI->GetWorldObject(lootTarget.guid);
    if (!nearestObject || !currentObject)
        return true;

    // Leave a valid target only for a clearly closer one, so two equidistant corpses on either side
    // of an obstacle do not swap places on every tick.
    constexpr float lootSwitchMargin = 3.0f;
    if (bot->GetDistance(nearestObject) + lootSwitchMargin < bot->GetDistance(currentObject))
        return true;

    return !lootTarget.IsLootPossible(bot);
}

enum ProfessionSpells
{
    ALCHEMY = 2259,
    BLACKSMITHING = 2018,
    COOKING = 2550,
    ENCHANTING = 7411,
    ENGINEERING = 49383,
    FIRST_AID = 3273,
    FISHING = 7620,
    HERB_GATHERING = 2366,
    INSCRIPTION = 45357,
    JEWELCRAFTING = 25229,
    MINING = 2575,
    SKINNING = 8613,
    TAILORING = 3908
};

bool OpenLootAction::Execute(Event /*event*/)
{
    LootObjectStack* availableLoot = AI_VALUE(LootObjectStack*, "available loot");
    if (availableLoot->IsLootPending() || !bot->GetLootGUID().IsEmpty() || bot->IsNonMeleeSpellCast(false))
        return false;

    LootObject lootObject = AI_VALUE(LootObject, "loot target");
    if (!availableLoot->CanAttemptLoot(lootObject.guid))
        return false;

    // DoLoot stops the bot first, which means "try again", not a failure; deferring after that
    // stop would make the bot walk away and come back. Only a dry failure at rest defers.
    bool const wasMoving = bot->isMoving();
    bool const wasMounted = bot->IsMounted();

    bool const result = DoLoot(lootObject);
    if (result)
        availableLoot->BeginLoot(lootObject.guid);
    else if (!lootObject.IsEmpty() && !wasMoving && !wasMounted)
    {
        availableLoot->DeferLoot(lootObject.guid);
        if (AI_VALUE(LootObject, "loot target").guid == lootObject.guid)
            context->GetValue<LootObject>("loot target")->Set(LootObject());
    }

    return result;
}

bool OpenLootAction::DoLoot(LootObject& lootObject)
{
    if (lootObject.IsEmpty() || !lootObject.IsLootPossible(bot))
        return false;

    Creature* creature = botAI->GetCreature(lootObject.guid);
    if (creature && !bot->IsWithinDistInMap(creature, INTERACTION_DISTANCE))
        return false;

    if (bot->isMoving())
    {
        bot->StopMoving();
        botAI->SetNextCheckDelay(sPlayerbotAIConfig.LootDelay);
        return false;
    }

    if (bot->IsMounted())
    {
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        botAI->SetNextCheckDelay(sPlayerbotAIConfig.ReactDelay);
        return false;
    }

    if (creature && creature->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
    {
        WorldPacket payload(CMSG_LOOT_UNIT, 16);
        WorldPackets::Loot::LootUnit lootUnit(std::move(payload));
        lootUnit.Unit = lootObject.guid;
        bot->GetSession()->HandleLootOpcode(lootUnit);
        botAI->SetNextCheckDelay(sPlayerbotAIConfig.LootDelay);
        return true;
    }

    if (creature)
    {
        SkillType skill = creature->GetCreatureDifficulty()->GetRequiredLootSkill();
        if (!CanOpenLock(skill, lootObject.reqSkillValue))
            return false;

        switch (skill)
        {
            case SKILL_ENGINEERING:
                return botAI->HasSkill(SKILL_ENGINEERING) ? botAI->CastSpell(ENGINEERING, creature) : false;
            case SKILL_HERBALISM:
                return botAI->HasSkill(SKILL_HERBALISM) ? botAI->CastSpell(32605, creature) : false;
            case SKILL_MINING:
                return botAI->HasSkill(SKILL_MINING) ? botAI->CastSpell(32606, creature) : false;
            default:
                return botAI->HasSkill(SKILL_SKINNING) ? botAI->CastSpell(SKINNING, creature) : false;
        }
    }

    GameObject* go = botAI->GetGameObject(lootObject.guid);
    if (go && !go->IsAtInteractDistance(bot))
        return false;

    if (go && (go->GetGoState() != GO_STATE_READY))
        return false;

    // This prevents dungeon chests like Tribunal Chest (Halls of Stone) from being ninja'd by the bots.
    // Quest objects carry the same flag but are gated on quest state, which ActivateToQuest answers.
    if (go && go->HasFlag(GO_FLAG_INTERACT_COND) && !go->ActivateToQuest(bot))
        return false;

    // This prevents raid chests like Gunship Armory (ICC) from being ninja'd by the bots
    if (go && go->HasFlag(GO_FLAG_NOT_SELECTABLE))
        return false;

    if (lootObject.reqItem)
    {
        Item* key = bot->GetItemByEntry(lootObject.reqItem);
        if (!key)
            return false;

        UseItemAction useItem(botAI);
        return useItem.UseItemOnGameObject(key, lootObject.guid);
    }

    if (lootObject.skillId == SKILL_MINING)
        return botAI->HasSkill(SKILL_MINING) ? botAI->CastSpell(MINING, bot) : false;

    if (lootObject.skillId == SKILL_HERBALISM)
        return botAI->HasSkill(SKILL_HERBALISM) ? botAI->CastSpell(HERB_GATHERING, bot) : false;

    uint32 spellId = GetOpeningSpell(lootObject);
    if (!spellId)
        return false;

    return botAI->CastSpell(spellId, bot);
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject)
{
    if (GameObject* go = botAI->GetGameObject(lootObject.guid))
        if (go->isSpawned())
            return GetOpeningSpell(lootObject, go);

    return 0;
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject, GameObject* go)
{
    for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;

        if (itr->second.state == PLAYERSPELL_REMOVED || !itr->second.active)
            continue;

        if (spellId == MINING || spellId == HERB_GATHERING)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!spellInfo)
            continue;

        if (CanOpenLock(lootObject, spellInfo, go))
            return spellId;
    }

    for (uint32 spellId : PlayerbotSpellRepository::Instance().GetOpeningSpells(lootObject.GetLockType()))
    {
        if (spellId == MINING || spellId == HERB_GATHERING)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (spellInfo && CanOpenLock(lootObject, spellInfo, go))
            return spellId;
    }

    return sPlayerbotAIConfig.OpenGoSpell;
}

bool OpenLootAction::CanOpenLock(LootObject& lootObject, SpellInfo const* spellInfo, GameObject* /*go*/)
{
    if (!lootObject.GetLockType())
        return false;

    for (uint8 effIndex = 0; effIndex <= EFFECT_2; effIndex++)
    {
        if (spellInfo->GetEffectOrDefault(SpellEffIndex(effIndex)).Effect != SPELL_EFFECT_OPEN_LOCK &&
            spellInfo->GetEffectOrDefault(SpellEffIndex(effIndex)).Effect != SPELL_EFFECT_SKINNING)
            continue;

        if (uint32(spellInfo->GetEffectOrDefault(SpellEffIndex(effIndex)).MiscValue) == lootObject.GetLockType())
            return CanOpenLock(lootObject.skillId, lootObject.reqSkillValue);
    }

    return false;
}

bool OpenLootAction::CanOpenLock(uint32 skillId, uint32 reqSkillValue)
{
    if (skillId == SKILL_NONE)
        return true;

    uint32 skillValue = bot->GetSkillValue(skillId);
    return skillValue >= reqSkillValue || !reqSkillValue;
}

/*
uint32 StoreLootAction::RoundPrice(double price)
{
    if (price < 100)
    {
        return (uint32)price;
    }

    if (price < 10000)
    {
        return (uint32)(price / 100.0) * 100;
    }

    if (price < 100000)
    {
        return (uint32)(price / 1000.0) * 1000;
    }

    return (uint32)(price / 10000.0) * 10000;
}

bool StoreLootAction::AuctionItem(uint32 itemId)
{
    ItemTemplate const* proto = sItemStorage.LookupEntry<ItemTemplate>(itemId);
    if (!proto)
        return false;

    if (!proto || proto->GetBonding() == BIND_ON_ACQUIRE || proto->GetBonding() == BIND_QUEST)
        return false;

    Item* oldItem = bot->GetItemByEntry(itemId);
    if (!oldItem)
        return false;

    AuctionHouseEntry const* ahEntry = AuctionHouseMgr::GetAuctionHouseEntry(unit->getFaction());
    if (!ahEntry)
        return false;

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(ahEntry);

    uint32 price = oldItem->GetCount() * proto->GetBuyPrice() * sRandomPlayerbotMgr.GetBuyMultiplier(bot);

uint32 stackCount = urand(1, proto->GetMaxStackSize());
    if (!price || !stackCount)
        return false;

    if (!stackCount)
        stackCount = 1;

    if (urand(0, 100) <= sAhBotConfig.underPriceProbability * 100)
        price = price * 100 / urand(100, 200);

    uint32 bidPrice = RoundPrice(stackCount * price);
    uint32 buyoutPrice = RoundPrice(stackCount * urand(price, 4 * price / 3));

    Item* item = Item::CreateItem(proto->GetId(), stackCount);
    if (!item)
        return false;

    uint32 auction_time = uint32(urand(8, 24) * HOUR * sWorld->getConfig(CONFIG_FLOAT_RATE_AUCTION_TIME));

    AuctionEntry* auctionEntry = new AuctionEntry;
    auctionEntry->Id = sObjectMgr->GenerateAuctionID();
    auctionEntry->itemGuidLow = item->GetGUID().GetCounter();
    auctionEntry->itemTemplate = item->GetEntry();
    auctionEntry->itemCount = item->GetCount();
    auctionEntry->itemRandomPropertyId = item->GetItemRandomPropertyId();
    auctionEntry->owner = bot->GetGUID().GetCounter();
    auctionEntry->startbid = bidPrice;
    auctionEntry->bidder = 0;
    auctionEntry->bid = 0;
    auctionEntry->buyout = buyoutPrice;
    auctionEntry->expireTime = time(nullptr) + auction_time;
    //auctionEntry->moneyDeliveryTime = 0;
    auctionEntry->deposit = 0;
    auctionEntry->auctionHouseEntry = ahEntry;

    auctionHouse->AddAuction(auctionEntry);

    sAuctionMgr.AddAItem(item);

    item->SaveToDB();
    auctionEntry->SaveToDB();

    TC_LOG_ERROR("playerbots", "AhBot {} added {} of {} to auction {} for {}..{}", bot->GetName().c_str(), stackCount,
proto->GetName(LocaleConstant(LOCALE_enUS)).c_str(), 1, bidPrice, buyoutPrice);

    if (oldItem->GetCount() > stackCount)
        oldItem->SetCount(oldItem->GetCount() - stackCount);
    else
        bot->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);

    return true;
}
*/

bool StoreLootAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    ObjectGuid ownerGuid;
    ObjectGuid guid;  // LootObj from the 12.x response (corpse or gameobject)
    uint8 failureReason = 0;
    uint8 lootType = 0;
    uint8 lootMethod = 0;
    uint8 threshold = 0;
    uint32 gold = 0;
    uint32 items = 0;
    uint32 currencies = 0;
    uint8 autostored = 0;

    p.rpos(0);
    p >> ownerGuid;
    p >> guid;
    p >> failureReason;
    p >> lootType;
    p >> lootMethod;
    p >> threshold;
    p >> gold;
    p >> items;
    p >> currencies;

    // 12.x loot response: every item carries bit-packed UI fields, then quantity, slot type and the
    // item instance; currencies follow, then the Acquired bit. Read the whole packet first, because
    // the old 3.3.5 field order left the cursor misaligned and the bot never released the loot
    // window (it stayed in the looting pose) and never picked anything up.
    struct ParsedLootItem
    {
        uint32 itemId = 0;
        uint32 count = 0;
        uint8 slotType = 0;
        uint8 listId = 0;
    };

    std::vector<ParsedLootItem> parsedItems;
    parsedItems.reserve(items);
    for (uint32 i = 0; i < items; ++i)
    {
        uint8 itemType = 0;
        uint8 uiType = 0;
        bool canTradeToTapList = false;
        p >> WorldPackets::Bits<2>(itemType);
        p >> WorldPackets::Bits<3>(uiType);
        p >> WorldPackets::Bits<1>(canTradeToTapList);
        p.ResetBitPos();

        ParsedLootItem item;
        p >> item.count;
        p >> item.slotType;
        p >> item.listId;
        WorldPackets::Item::ItemInstance lootInstance;
        p >> lootInstance;
        item.itemId = uint32(lootInstance.ItemID);
        parsedItems.push_back(item);
    }

    for (uint32 i = 0; i < currencies; ++i)
    {
        uint32 currencyId = 0;
        uint32 currencyQuantity = 0;
        uint8 listId = 0;
        uint8 uiType = 0;
        p >> currencyId;
        p >> currencyQuantity;
        p >> listId;
        p >> WorldPackets::Bits<3>(uiType);
        p.ResetBitPos();
    }

    bool acquired = false;
    bool aeLooting = false;
    bool suppressError = false;
    p >> WorldPackets::Bits<1>(acquired);
    p >> WorldPackets::Bits<1>(aeLooting);
    p >> WorldPackets::Bits<1>(suppressError);

    LootObjectStack* availableLoot = AI_VALUE(LootObjectStack*, "available loot");
    ObjectGuid currentLoot = bot->GetLootGUID();
    if (!acquired)
    {
        availableLoot->RetryLoot(guid);
        if (AI_VALUE(LootObject, "loot target").guid == guid)
            context->GetValue<LootObject>("loot target")->Set(LootObject());

        // A window a previous attempt opened stays open otherwise and pins the looting pose.
        if (!currentLoot.IsEmpty() && currentLoot == guid)
        {
            WorldPacket releasePayload(CMSG_LOOT_RELEASE, 16);
            WorldPackets::Loot::LootRelease release(std::move(releasePayload));
            release.Unit = guid;
            bot->GetSession()->HandleLootReleaseOpcode(release);
        }

        return false;
    }

    if (!currentLoot.IsEmpty() && currentLoot != guid)
    {
        availableLoot->CancelLoot(guid);
        return false;
    }

    availableLoot->LootOpened(guid);

    if (!currentLoot)
        bot->SetLootGUID(guid);

    if (gold > 0)
    {
        WorldPacket payload(CMSG_LOOT_MONEY, 0);
        WorldPackets::Loot::LootMoney lootMoney(std::move(payload));
        bot->GetSession()->HandleLootMoneyOpcode(lootMoney);
        // bot->GetSession()->HandleLootMoneyOpcode(packet);
    }

    for (ParsedLootItem const& parsed : parsedItems)
    {
        uint32 itemid = parsed.itemId;
        uint32 itemcount = parsed.count;
        uint8 lootslot_type = parsed.slotType;
        uint8 itemindex = parsed.listId;

        if (lootslot_type != LOOT_SLOT_TYPE_ALLOW_LOOT && lootslot_type != LOOT_SLOT_TYPE_OWNER)
            continue;

        if (lootType != LOOT_SKINNING && !IsLootAllowed(itemid, botAI))
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemid);
        if (!proto)
            continue;

        // Skip loot that isn't useful when bags are nearly full.
        if (!IsRealPlayer(botAI->GetMaster()) && AI_VALUE(uint8, "bag space") > 80)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemid);
            bool const isUsefulItem = proto->GetStartQuest() ||
                                      (usage != ITEM_USAGE_NONE && usage != ITEM_USAGE_VENDOR && usage != ITEM_USAGE_AH);

            if (isUsefulItem)
            {
                ItemPosCountVec dest;
                if (bot->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, dest, itemid, itemcount) != EQUIP_ERR_OK)
                    continue;
            }
            else
            {
                uint32 maxStack = proto->GetMaxStackSize();
                if (maxStack == 1)
                    continue;

                std::vector<Item*> found = parseItems(chat->FormatItem(proto));
                bool hasFreeStack = false;
                for (auto stack : found)
                {
                    if (stack->GetCount() + itemcount < maxStack)
                    {
                        hasFreeStack = true;
                        break;
                    }
                }

                if (!hasFreeStack)
                    continue;
            }
        }

        Player* master = botAI->GetMaster();
        if (sRandomPlayerbotMgr.IsRandomBot(bot) && master)
        {
            uint32 price = itemcount * proto->GetBuyPrice() * sRandomPlayerbotMgr.GetBuyMultiplier(bot) + gold;
            if (price)
                sRandomPlayerbotMgr.AddTradeDiscount(bot, master, price);

            if (Group* group = bot->GetGroup())
                for (GroupReference const& ref : group->GetMembers())
                    if (ref.GetSource() != bot)
                        GuildTaskMgr::instance().CheckItemTask(itemid, itemcount, ref.GetSource(), bot);
        }

        WorldPacket payload(CMSG_LOOT_ITEM);
        WorldPackets::Loot::LootItem lootItem(std::move(payload));
        lootItem.Loot.resize(1);
        lootItem.Loot[0].Object = guid;
        lootItem.Loot[0].LootListID = uint8(itemindex);
        bot->GetSession()->HandleAutostoreLootItemOpcode(lootItem);
        botAI->SetNextCheckDelay(sPlayerbotAIConfig.LootDelay);
        ++autostored;

        if (proto->GetQuality() > ITEM_QUALITY_NORMAL && !urand(0, 50) &&
            botAI->HasStrategy("emote", BOT_STATE_NON_COMBAT) && sPlayerbotAIConfig.RandomBotEmote)
            botAI->PlayEmote(TEXT_EMOTE_CHEER);

        if (proto->GetQuality() >= ITEM_QUALITY_RARE && !urand(0, 1) && botAI->HasStrategy("emote", BOT_STATE_NON_COMBAT) &&
            sPlayerbotAIConfig.RandomBotEmote)
            botAI->PlayEmote(TEXT_EMOTE_CHEER);

        BroadcastHelper::BroadcastLootingItem(botAI, bot, proto);
    }

    // Nothing storable: defer instead of reopening the window in a loop.
    if (autostored == 0 && guid.IsGameObject())
        availableLoot->DeferLoot(guid);
    else
        availableLoot->Remove(guid);

    if (AI_VALUE(LootObject, "loot target").guid == guid)
        context->GetValue<LootObject>("loot target")->Set(LootObject());

    // release loot
    WorldPacket payload(CMSG_LOOT_RELEASE, 16);
    WorldPackets::Loot::LootRelease release(std::move(payload));
    release.Unit = guid;
    bot->GetSession()->HandleLootReleaseOpcode(release);
    return true;
}

bool StoreLootAction::IsLootAllowed(uint32 itemid, PlayerbotAI* botAI)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    LootStrategy* lootStrategy = AI_VALUE(LootStrategy*, "loot strategy");

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemid);
    if (!proto)
        return false;

    std::set<uint32>& lootItems = AI_VALUE(std::set<uint32>&, "always loot list");
    if (lootItems.find(itemid) != lootItems.end())
        return true;

    uint32 max = proto->GetMaxCount();
    if (max > 0 && botAI->GetBot()->HasItemCount(itemid, max, true))
        return false;

    if (proto->GetStartQuest())
    {
        return true;
    }

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 entry = botAI->GetBot()->GetQuestSlotQuestId(slot);
        Quest const* quest = sObjectMgr->GetQuestTemplate(entry);
        if (!quest)
            continue;

        // 12.x keeps quest objectives structured (see Util/QuestHelper.h).
        for (QuestObjective const& objective : quest->GetObjectives())
        {
            if (objective.Type == QUEST_OBJECTIVE_ITEM && objective.ObjectID == int32(itemid))
            {
                // if (AI_VALUE2(uint32, "item count", proto->GetName(LocaleConstant(LOCALE_enUS))) < quest->RequiredItemCount[i])
                // {
                //     if (botAI->GetMaster() && sPlayerbotAIConfig.SyncQuestWithPlayer)
                //         return false; //Quest is autocomplete for the bot so no item needed.
                // }

                return true;
            }
        }
    }

    // if (proto->GetBonding() == BIND_QUEST ||  //Still testing if it works ok without these lines.
    //     proto->GetBonding() == BIND_QUEST_ITEM1 || //Eventually this has to be removed.
    //     proto->GetClass() == ITEM_CLASS_QUEST)
    //{

    bool canLoot = lootStrategy->CanLoot(proto, context);
    // if (canLoot && proto->GetBonding() == BIND_ON_ACQUIRE && IsRealPlayer(botAI->GetMaster()))
    // canLoot = sPlayerbotAIConfig.IsInRandomAccountList(botAI->GetBot()->GetSession()->GetAccountId());

    return canLoot;
}

bool ReleaseLootAction::Execute(Event /*event*/)
{
    ObjectGuid guid = bot->GetLootGUID();
    if (guid.IsEmpty())
        return false;

    WorldPacket payload(CMSG_LOOT_RELEASE, 16);
    WorldPackets::Loot::LootRelease release(std::move(payload));
    release.Unit = guid;
    bot->GetSession()->HandleLootReleaseOpcode(release);
    return true;
}
