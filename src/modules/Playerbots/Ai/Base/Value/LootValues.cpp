/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LootValues.h"
#include "Item.h"
#include "CreatureHelper.h"
#include "Playerbots.h"
#include "SharedValueContext.h"

LootTemplateAccess const* DropMapValue::GetLootTemplate(ObjectGuid guid, LootType type)
{
    LootTemplate const* lTemplate = nullptr;

    if (guid.IsCreature())
    {
        CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(guid.GetEntry());

        if (info)
        {
            if (type == LOOT_CORPSE)
                lTemplate = LootTemplates_Creature.GetLootFor(PlayerbotCreature::GetLootId(info));
            else if (type == LOOT_PICKPOCKETING && PlayerbotCreature::GetPickPocketLootId(info))
                lTemplate = LootTemplates_Pickpocketing.GetLootFor(PlayerbotCreature::GetPickPocketLootId(info));
            else if (type == LOOT_SKINNING && PlayerbotCreature::GetSkinLootId(info))
                lTemplate = LootTemplates_Skinning.GetLootFor(PlayerbotCreature::GetSkinLootId(info));
        }
    }
    else if (guid.IsGameObject())
    {
        GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(guid.GetEntry());
        if (info && info->GetLootId() != 0)
        {
            if (type == LOOT_CORPSE)
                lTemplate = LootTemplates_Gameobject.GetLootFor(info->GetLootId());
            else if (type == LOOT_FISHINGHOLE)
                lTemplate = LootTemplates_Fishing.GetLootFor(info->GetLootId());
        }
    }
    else if (guid.IsItem())
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(guid.GetEntry());

        if (proto)
        {
            if (type == LOOT_CORPSE)
                lTemplate = LootTemplates_Item.GetLootFor(proto->GetId());
            else if (type == LOOT_DISENCHANTING)
            {
                // 12.x resolves the disenchant loot entry from the item's quality and level.
                if (ItemDisenchantLootEntry const* disenchant =
                        Item::GetBaseDisenchantLoot(proto, proto->GetQuality(), proto->GetBaseItemLevel()))
                    lTemplate = LootTemplates_Disenchant.GetLootFor(disenchant->ID);
            }
            if (type == LOOT_MILLING)
                lTemplate = LootTemplates_Milling.GetLootFor(proto->GetId());
            if (type == LOOT_PROSPECTING)
                lTemplate = LootTemplates_Prospecting.GetLootFor(proto->GetId());
        }
    }

    LootTemplateAccess const* lTemplateA = reinterpret_cast<LootTemplateAccess const*>(lTemplate);

    return lTemplateA;
}

DropMap* DropMapValue::Calculate()
{
    DropMap* dropMap = new DropMap;

    int32 sEntry = 0;

    {
        CreatureTemplateContainer const& creatures = sObjectMgr->GetCreatureTemplates();
        for (CreatureTemplateContainer::const_iterator itr = creatures.begin(); itr != creatures.end(); ++itr)
        {
            sEntry = itr->first;

            if (LootTemplateAccess const* lTemplateA =
                    GetLootTemplate(ObjectGuid::Create<HighGuid::Creature>(0, uint32(sEntry), 1), LOOT_CORPSE))
                for (auto const& lItem : lTemplateA->Entries)
                    dropMap->insert(std::make_pair(lItem->itemid, sEntry));
        }
    }

    {
        GameObjectTemplateContainer const& gameobjects = sObjectMgr->GetGameObjectTemplates();
        for (auto const& itr : gameobjects)
        {
            sEntry = itr.first;

            if (LootTemplateAccess const* lTemplateA =
                    GetLootTemplate(ObjectGuid::Create<HighGuid::GameObject>(0, uint32(sEntry), 1), LOOT_CORPSE))
                for (auto const& lItem : lTemplateA->Entries)
                    dropMap->insert(std::make_pair(lItem->itemid, -sEntry));
        }
    }

    return dropMap;
}

// What items does this entry have in its loot list?
std::vector<int32> ItemDropListValue::Calculate()
{
    uint32 itemId = stoi(getQualifier());

    DropMap* dropMap = GAI_VALUE(DropMap*, "drop map");

    std::vector<int32> entries;

    auto range = dropMap->equal_range(itemId);

    for (auto itr = range.first; itr != range.second; ++itr)
        entries.push_back(itr->second);

    return entries;
}

// What items does this entry have in its loot list?
std::vector<uint32> EntryLootListValue::Calculate()
{
    int32 entry = stoi(getQualifier());

    std::vector<uint32> items;

    LootTemplateAccess const* lTemplateA;

    if (entry > 0)
        lTemplateA = DropMapValue::GetLootTemplate(ObjectGuid::Create<HighGuid::Creature>(0, uint32(entry), 1), LOOT_CORPSE);
    else
        lTemplateA =
            DropMapValue::GetLootTemplate(ObjectGuid::Create<HighGuid::GameObject>(0, uint32(entry), 1), LOOT_CORPSE);

    if (lTemplateA)
        for (auto const& lItem : lTemplateA->Entries)
            items.push_back(lItem->itemid);

    return items;
}

itemUsageMap EntryLootUsageValue::Calculate()
{
    itemUsageMap items;

    for (auto itemId : GAI_VALUE2(std::vector<uint32>, "entry loot list", getQualifier()))
    {
        items[AI_VALUE2(ItemUsage, "item usage", itemId)].push_back(itemId);
    }

    return items;
};

bool HasUpgradeValue::Calculate()
{
    itemUsageMap uMap = AI_VALUE2(itemUsageMap, "entry loot usage", getQualifier());
    return uMap.find(ITEM_USAGE_EQUIP) != uMap.end() || uMap.find(ITEM_USAGE_REPLACE) != uMap.end();
}
