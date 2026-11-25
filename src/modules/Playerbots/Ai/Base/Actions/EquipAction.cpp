/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Bag.h"
#include "TradeData.h"
#include "EquipAction.h"
#include "Event.h"
#include "ItemCountValue.h"
#include "ItemPackets.h"
#include "ItemUsageValue.h"
#include "ItemVisitors.h"
#include "PacketOperators.h"
#include "Playerbots.h"
#include "StatsWeightCalculator.h"
#include <utility>

bool EquipAction::Execute(Event event)
{
    std::string const text = event.getParam();
    ItemIds ids = chat->parseItems(text);
    EquipItems(ids);
    return true;
}

void EquipAction::EquipItems(ItemIds ids)
{
    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        EquipItem(&visitor);
    }
}

// Return bagslot with smalest bag.
uint8 EquipAction::GetSmallestBagSlot()
{
    int8 curBag = 0;
    uint32 curSlots = 0;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag const* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
        {
            if (curBag > 0 && curSlots < pBag->GetBagSize())
                continue;

            curBag = bag;
            curSlots = pBag->GetBagSize();
        }
        else
            return bag;
    }

    return curBag;
}

void EquipAction::EquipItem(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    std::vector<Item*> items = visitor->GetResult();
    if (!items.empty())
        EquipItem(*items.begin());
}

void EquipAction::EquipItem(Item* item)
{
    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    ItemTemplate const* itemProto = item->GetTemplate();
    uint32 itemId = itemProto->GetId();
    uint8 invType = itemProto->GetInventoryType();

    // Handle ammunition separately
    if (invType == INVTYPE_AMMO)
    {
        bot->SetAmmo(itemId);
        std::ostringstream out;
        out << "equipping " << chat->FormatItem(itemProto);
        botAI->TellMaster(out);
        return;
    }

    // Handle bags first
    bool equippedBag = false;
    if (itemProto->GetClass() == ITEM_CLASS_CONTAINER)
    {
        // Attempt to equip as a bag
        uint8 newBagSlot = GetSmallestBagSlot();

        if (newBagSlot > 0)
        {
            uint16 src = ((bagIndex << 8) | slot);
            uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | newBagSlot);
            bot->SwapItem(src, dst);
            equippedBag = true;
        }
    }

    // If we didn't equip as a bag, try to equip as gear
    if (!equippedBag)
    {
        // Ranged weapons aren't handled by the rest of the weapon equip logic
        // Handle them early here to avoid issues.
        if (invType == INVTYPE_RANGED || invType == INVTYPE_THROWN || invType == INVTYPE_RANGEDRIGHT)
        {
            WorldPackets::Item::AutoEquipItemSlot packet(WorldPacket(CMSG_AUTO_EQUIP_ITEM_SLOT, 0));
            packet.Item = item->GetGUID();
            packet.ItemDstSlot = uint8(EQUIPMENT_SLOT_RANGED);
            packet.Inv.Items.push_back({ item->GetBagSlot(), item->GetSlot() });
            bot->GetSession()->HandleAutoEquipItemSlotOpcode(packet);

            std::ostringstream out;
            out << "Equipping " << chat->FormatItem(itemProto) << " in ranged slot";
            botAI->TellMaster(out);
            return;
        }

        uint8 dstSlot = botAI->FindEquipSlot(itemProto, NULL_SLOT, true);

        // Check if the item is a weapon and whether the bot can dual wield or use Titan Grip
        bool isWeapon = (itemProto->GetClass() == ITEM_CLASS_WEAPON);
        bool canTitanGrip = false;  // 1.6x has no Titan Grip: it is a WotLK warrior talent
        bool canDualWield = bot->CanDualWield();

        bool isTwoHander = (invType == INVTYPE_2HWEAPON);
        bool isValidTGWeapon = false;
        if (canTitanGrip && isTwoHander)
        {
            // Titan Grip-valid 2H weapon subclasses: Axe2, Mace2, Sword2
            isValidTGWeapon = (itemProto->GetSubClass() == ITEM_SUBCLASS_WEAPON_AXE2 ||
                               itemProto->GetSubClass() == ITEM_SUBCLASS_WEAPON_MACE2 ||
                               itemProto->GetSubClass() == ITEM_SUBCLASS_WEAPON_SWORD2);
        }

        // Check if the main hand currently has a 2H weapon equipped
        Item* currentMHItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        bool have2HWeaponEquipped = (currentMHItem && currentMHItem->GetTemplate()->GetInventoryType() == INVTYPE_2HWEAPON);

        // bool canDualWieldOrTG = (canDualWield || (canTitanGrip && isTwoHander));
        bool canDualWieldOrTG = (canDualWield || isTwoHander);

        // If this is a weapon and we can dual wield or Titan Grip, check if we can improve main/off-hand setup
        if (isWeapon && canDualWieldOrTG)
        {
            // Fetch current main hand and offhand items
            Item* mainHandItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            Item* offHandItem  = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);

            // Set up the stats calculator once and reuse results for performance
            StatsWeightCalculator calculator(bot);
            calculator.SetItemSetBonus(false);
            calculator.SetOverflowPenalty(false);

            // Calculate item scores once and store them
            float newItemScore = calculator.CalculateItem(itemId, 0);
            float mainHandScore = mainHandItem
                ? calculator.CalculateItem(mainHandItem->GetTemplate()->GetId(), 0) : 0.0f;
            float offHandScore = offHandItem
                ? calculator.CalculateItem(offHandItem->GetTemplate()->GetId(), 0) : 0.0f;

            // Determine where this weapon can go
            bool canGoMain = (invType == INVTYPE_WEAPON ||
                              invType == INVTYPE_WEAPONMAINHAND ||
                              isTwoHander);

            bool canTGOff = false;
            if (canTitanGrip && isTwoHander && isValidTGWeapon)
                canTGOff = true;

            bool canGoOff = (invType == INVTYPE_WEAPON ||
                             invType == INVTYPE_WEAPONOFFHAND ||
                             canTGOff);

            // Check if the main hand item can go to offhand if needed
            bool mainHandCanGoOff = false;
            if (mainHandItem)
            {
                ItemTemplate const* mhProto = mainHandItem->GetTemplate();
                bool mhIsValidTG = false;
                if (canTitanGrip && mhProto->GetInventoryType() == INVTYPE_2HWEAPON)
                {
                    mhIsValidTG = (mhProto->GetSubClass() == ITEM_SUBCLASS_WEAPON_AXE2 ||
                                   mhProto->GetSubClass() == ITEM_SUBCLASS_WEAPON_MACE2 ||
                                   mhProto->GetSubClass() == ITEM_SUBCLASS_WEAPON_SWORD2);
                }

                mainHandCanGoOff = (mhProto->GetInventoryType() == INVTYPE_WEAPON ||
                                    mhProto->GetInventoryType() == INVTYPE_WEAPONOFFHAND ||
                                    (mhProto->GetInventoryType() == INVTYPE_2HWEAPON && mhIsValidTG));
            }

            // Priority 1: Replace main hand if the new weapon is strictly better
            // and if conditions allow (e.g. no conflicting 2H logic)
            bool betterThanMH = (newItemScore > mainHandScore);
            // If a one-handed weapon is better, we can still use it instead of a two-handed weapon
            bool mhConditionOK = (invType != INVTYPE_2HWEAPON ||
                      (isTwoHander && !canTitanGrip) ||
                      (canTitanGrip && isValidTGWeapon));

            if (canGoMain && betterThanMH && mhConditionOK)
            {
                // Equip new weapon in main hand
                {
                    WorldPackets::Item::AutoEquipItemSlot packet(WorldPacket(CMSG_AUTO_EQUIP_ITEM_SLOT, 0));
                    packet.Item = item->GetGUID();
                    packet.ItemDstSlot = uint8(EQUIPMENT_SLOT_MAINHAND);
                    packet.Inv.Items.push_back({ item->GetBagSlot(), item->GetSlot() });
                    bot->GetSession()->HandleAutoEquipItemSlotOpcode(packet);
                }

                // Try moving old main hand weapon to offhand if beneficial
                if (mainHandItem && mainHandCanGoOff && (!offHandItem || mainHandScore > offHandScore))
                {
                    ItemTemplate const* oldMHProto = mainHandItem->GetTemplate();

                    WorldPackets::Item::AutoEquipItemSlot packet(WorldPacket(CMSG_AUTO_EQUIP_ITEM_SLOT, 0));
                    packet.Item = mainHandItem->GetGUID();
                    packet.ItemDstSlot = uint8(EQUIPMENT_SLOT_OFFHAND);
                    packet.Inv.Items.push_back({ mainHandItem->GetBagSlot(), mainHandItem->GetSlot() });
                    bot->GetSession()->HandleAutoEquipItemSlotOpcode(packet);

                    std::ostringstream moveMsg;
                    moveMsg << "Main hand upgrade found. Moving " << chat->FormatItem(oldMHProto) << " to offhand";
                    botAI->TellMaster(moveMsg);
                }

                std::ostringstream out;
                out << "Equipping " << chat->FormatItem(itemProto) << " in main hand";
                botAI->TellMaster(out);
                return;
            }

            // Priority 2: If not better than main hand, check if better than offhand
            else if (canGoOff && newItemScore > offHandScore)
            {
                // Equip in offhand
                WorldPackets::Item::AutoEquipItemSlot packet(WorldPacket(CMSG_AUTO_EQUIP_ITEM_SLOT, 0));
                packet.Item = item->GetGUID();
                packet.ItemDstSlot = uint8(EQUIPMENT_SLOT_OFFHAND);
                packet.Inv.Items.push_back({ item->GetBagSlot(), item->GetSlot() });
                bot->GetSession()->HandleAutoEquipItemSlotOpcode(packet);

                std::ostringstream out;
                out << "Equipping " << chat->FormatItem(itemProto) << " in offhand";
                botAI->TellMaster(out);
                return;
            }
            else
            {
                // No improvement, do nothing
                return;
            }
        }

        // If not a special dual-wield/TG scenario or no improvement found, fall back to original logic
        if (dstSlot == EQUIPMENT_SLOT_FINGER1 ||
            dstSlot == EQUIPMENT_SLOT_TRINKET1 ||
            (dstSlot == EQUIPMENT_SLOT_MAINHAND && canDualWield &&
                ((invType != INVTYPE_2HWEAPON && !have2HWeaponEquipped) || (canTitanGrip && isValidTGWeapon))))
        {
            // Handle ring/trinket dual-slot logic
            Item* const equippedItems[2] = {
                bot->GetItemByPos(INVENTORY_SLOT_BAG_0, dstSlot),
                bot->GetItemByPos(INVENTORY_SLOT_BAG_0, dstSlot + 1)
            };

            if (equippedItems[0])
            {
                if (equippedItems[1])
                {
                    // Both slots are full - pick the worst item to replace, but only if new item is better
                    StatsWeightCalculator calc(bot);
                    calc.SetItemSetBonus(false);
                    calc.SetOverflowPenalty(false);

                    // Calculate new item score with random properties
                    int32 newItemRandomProp = 0;
                    float newItemScore = calc.CalculateItem(itemId, newItemRandomProp);

                    // Calculate equipped items scores with random properties
                    // 12.x dropped ItemRandomProperties (RandomBonusList replaced it),
                    // so the equipped items are scored without a suffix bonus.
                    int32 firstRandomProp = 0;
                    int32 secondRandomProp = 0;
                    float firstItemScore = calc.CalculateItem(equippedItems[0]->GetTemplate()->GetId(), firstRandomProp);
                    float secondItemScore = calc.CalculateItem(equippedItems[1]->GetTemplate()->GetId(), secondRandomProp);

                    // Determine which slot (if any) should be replaced
                    bool betterThanFirst = newItemScore > firstItemScore;
                    bool betterThanSecond = newItemScore > secondItemScore;

                    // Early return if new item is not better than either equipped item
                    if (!betterThanFirst && !betterThanSecond)
                        return;

                    if (betterThanFirst && betterThanSecond)
                    {
                        // New item is better than both - replace the worse of the two equipped items
                        if (firstItemScore > secondItemScore)
                            dstSlot++; // Replace second slot (worse)
                        // else: keep dstSlot as-is (replace first slot)
                    }
                    else if (betterThanSecond)
                        dstSlot++; // Only better than second slot - replace it
                }
                else
                {
                    // Second slot empty, use it
                    dstSlot++;
                }
            }
        }

        // Equip the item in the chosen slot
        {
            WorldPackets::Item::AutoEquipItemSlot packet(WorldPacket(CMSG_AUTO_EQUIP_ITEM_SLOT, 0));
            packet.Item = item->GetGUID();
            packet.ItemDstSlot = dstSlot;
            packet.Inv.Items.push_back({ item->GetBagSlot(), item->GetSlot() });
            bot->GetSession()->HandleAutoEquipItemSlotOpcode(packet);
        }
    }

    std::ostringstream out;
    out << "Equipping " << chat->FormatItem(itemProto);
    botAI->TellMaster(out);
}

ItemIds EquipAction::SelectInventoryItemsToEquip()
{
    CollectItemsVisitor visitor;
    IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);

    ItemIds items;
    for (auto i = visitor.items.begin(); i != visitor.items.end(); ++i)
    {
        Item* item = *i;
        if (!item)
            continue;

        ItemTemplate const* itemTemplate = item->GetTemplate();
        if (!itemTemplate)
            continue;

        //Pre-filter non-equipable items
        if (itemTemplate->GetInventoryType() == INVTYPE_NON_EQUIP)
            continue;

        int32 randomProperty = 0;
        uint32 itemId = item->GetTemplate()->GetId();
        std::string itemUsageParam;
        if (randomProperty != 0)
            itemUsageParam = std::to_string(itemId) + "," + std::to_string(randomProperty);
        else
            itemUsageParam = std::to_string(itemId);

        ItemUsage usage = AI_VALUE2(ItemUsage, "item upgrade", itemUsageParam);
        if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
            items.insert(itemId);
    }
    return items;
}

bool EquipUpgradesPacketAction::Execute(Event event)
{
    if (!sPlayerbotAIConfig.AutoEquipUpgradeLoot && !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;
    std::string const source = event.GetSource();
    if (source == "trade status")
    {
        // 12.x packs the header: 1 bit bnet-account flag, then 5 bit status
        WorldPacket p(event.getPacket());
        p.rpos(0);
        uint8 header = 0;
        p >> WorldPackets::Bits<6>(header);
        uint8 const status = header & 0x1F;

        // the goods only change hands once the trade completes
        if (status != TRADE_STATUS_COMPLETE)
            return false;
    }

    else if (source == "item push result")
    {
        // 12.x layout (WorldPackets::Item::ItemPushResult::Write): player, slot, slot in bag, the item
        // instance, ...
        WorldPacket p(event.getPacket());
        p.rpos(0);
        ObjectGuid playerGuid;
        p >> playerGuid;
        p.read_skip<uint8_t>();          // Slot
        p.read_skip<int32_t>();          // SlotInBag
        WorldPackets::Item::ItemInstance itemInstance;
        p >> itemInstance;

        ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemInstance.ItemID);
        if (!item || item->GetInventoryType() == INVTYPE_NON_EQUIP)
            return false;
    }

    ItemIds items = SelectInventoryItemsToEquip();
    EquipItems(items);
    return true;
}

bool EquipUpgradeAction::Execute(Event /*event*/)
{
    ItemIds items = SelectInventoryItemsToEquip();
    EquipItems(items);
    return true;
}
