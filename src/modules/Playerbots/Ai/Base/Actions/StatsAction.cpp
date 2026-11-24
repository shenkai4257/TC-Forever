/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "StatsAction.h"
#include "Bag.h"
#include "ChatHelper.h"
#include "Event.h"
#include "PlayerbotAI.h"
#include "RestMgr.h"

bool StatsAction::Execute(Event /*event*/)
{
    std::ostringstream out;

    ListGold(out);

    out << ", ";
    ListBagSlots(out);

    out << ", ";
    ListRepairCost(out);

    if (bot->GetXPForNextLevel())
    {
        out << ", ";
        ListXP(out);
    }

    botAI->TellMaster(out);
    return true;
}

void StatsAction::ListGold(std::ostringstream& out) { out << chat->formatMoney(bot->GetMoney()); }

void StatsAction::ListBagSlots(std::ostringstream& out)
{
    uint32 totalused = 0, total = 16;

    // list out items in main backpack
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
    {
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            ++totalused;
        }
    }

    uint32 totalfree = 16 - totalused;

    // list out items in other removable backpacks
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        if (Bag const* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag))
        {
            ItemTemplate const* pBagProto = pBag->GetTemplate();
            if (pBagProto->GetClass() == ITEM_CLASS_CONTAINER && pBagProto->GetSubClass() == ITEM_SUBCLASS_CONTAINER)
            {
                total += pBag->GetBagSize();
                totalfree += pBag->GetFreeSlots();
            }
        }
    }

    std::string color = "ff00ff00";
    if (totalfree < total / 2)
        color = "ffffff00";

    if (totalfree < total / 4)
        color = "ffff0000";

    out << "|h|c" << color << totalfree << "/" << total << "|h|cffffffff Bag";
}

void StatsAction::ListXP(std::ostringstream& out)
{
    uint32 curXP = bot->GetXP();
    uint32 nextLevelXP = bot->GetXPForNextLevel();
    uint32 restXP = uint32(bot->GetRestMgr().GetRestBonus(REST_TYPE_XP));
    uint32 xpPercent = 0;

    if (nextLevelXP)
        xpPercent = 100 * curXP / nextLevelXP;

    uint32 restPercent = 0;
    if (restXP && nextLevelXP)
        restPercent = 2 * (100 * restXP / nextLevelXP);

    out << "|cff00ff00" << xpPercent << "|cffffd333/|cff00ff00" << restPercent << "%|cffffffff XP";
}

void StatsAction::ListRepairCost(std::ostringstream& out)
{
    uint32 totalCost = 0;
    double repairPercent = 0;
    double repairCount = 0;

    for (uint32 i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        uint16 pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        totalCost += EstRepair(pos);
        double repair = RepairPercent(pos);
        if (repair < 100)
        {
            repairPercent += repair;
            ++repairCount;
        }
    }

    repairPercent /= repairCount;

    std::string color = "ff00ff00";
    if (repairPercent < 50)
        color = "ffffff00";

    if (repairPercent < 25)
        color = "ffff0000";

    out << "|c" << color << (uint32)ceil(repairPercent) << "% (" << chat->formatMoney(totalCost) << ")|cffffffff Dur";
}

uint32 StatsAction::EstRepair(uint16 pos)
{
    Item* item = bot->GetItemByPos(pos);
    if (!item)
        return 0;

    // 12.x keeps durability and the repair cost maths in the core (DurabilityCosts/Quality
    // stores); item-level cost formulas from 3.3.5 no longer apply.
    return uint32(item->CalculateDurabilityRepairCost(1.0f));
}

double StatsAction::RepairPercent(uint16 pos)
{
    Item* item = bot->GetItemByPos(pos);
    if (!item)
        return 100;

    uint32 maxDurability = *item->m_itemData->MaxDurability;
    if (!maxDurability)
        return 100;

    uint32 curDurability = *item->m_itemData->Durability;
    if (!curDurability)
        return 0;

    return curDurability * 100.0 / maxDurability;
}
