/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ItemVisitors.h"
#include "ItemTemplateHelper.h"
#include "Playerbots.h"

bool FindUsableItemVisitor::Visit(Item* item)
{
    if (bot->CanUseItem(item->GetTemplate()) == EQUIP_ERR_OK)
        return FindItemVisitor::Visit(item);

    return true;
}

bool FindStartQuestItemVisitor::Visit(Item* item)
{
    if (!item->GetTemplate()->GetStartQuest())
        return true;

    return FindUsableItemVisitor::Visit(item);
}

bool FindPotionVisitor::Accept(ItemTemplate const* proto)
{
    if (proto->GetClass() == ITEM_CLASS_CONSUMABLE &&
        (proto->GetSubClass() == ITEM_SUBCLASS_POTION || proto->GetSubClass() == ITEM_SUBCLASS_FLASK))
    {
        for (ItemEffectEntry const* itemEffect : proto->Effects)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemEffect ? itemEffect->SpellID : 0, DIFFICULTY_NONE);
            if (!spellInfo)
                return false;

            for (uint8 i = 0; i < 3; i++)
            {
                if (GetEffectOrDefault(spellInfo, SpellEffIndex(i)).Effect == effectId)
                    return true;
            }
        }
    }

    return false;
}

bool FindMountVisitor::Accept(ItemTemplate const* proto)
{
    for (ItemEffectEntry const* itemEffect : proto->Effects)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemEffect ? itemEffect->SpellID : 0, DIFFICULTY_NONE);
        if (!spellInfo)
            return false;

        for (uint8 i = 0; i < 3; i++)
        {
            if (GetEffectOrDefault(spellInfo, SpellEffIndex(i)).ApplyAuraName == SPELL_AURA_MOUNTED)
                return true;
        }
    }

    return false;
}

bool FindPetVisitor::Accept(ItemTemplate const* proto)
{
    if (proto->GetClass() == ITEM_CLASS_MISCELLANEOUS)
    {
        for (ItemEffectEntry const* itemEffect : proto->Effects)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemEffect ? itemEffect->SpellID : 0, DIFFICULTY_NONE);
            if (!spellInfo)
                return false;

            for (uint8 i = 0; i < 3; i++)
            {
                if (GetEffectOrDefault(spellInfo, SpellEffIndex(i)).Effect == SPELL_EFFECT_SUMMON_PET)
                    return true;
            }
        }
    }

    return false;
}

FindItemUsageVisitor::FindItemUsageVisitor(Player* bot, ItemUsage usage) : FindUsableItemVisitor(bot), usage(usage)
{
    context = GET_PLAYERBOT_AI(bot)->GetAiObjectContext();
};

bool FindItemUsageVisitor::Accept(ItemTemplate const* proto)
{
    if (AI_VALUE2(ItemUsage, "item usage", proto->GetId()) == usage)
        return true;

    return false;
}

bool FindUsableNamedItemVisitor::Accept(ItemTemplate const* proto)
{
    if (!proto)
        return false;

    char const* itemName = proto->GetName(LocaleConstant(LOCALE_enUS));
    return itemName && *itemName && strstri(itemName, name.c_str());
}
