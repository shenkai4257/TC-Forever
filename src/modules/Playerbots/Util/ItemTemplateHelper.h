/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ITEMTEMPLATEHELPER_H
#define PLAYERBOTS_ITEMTEMPLATEHELPER_H

#include "Item.h"
#include "ItemTemplate.h"

// 3.3.5 item templates exposed fixed spell slots (ItemSpelltriggerEntry Spells[i]). 12.x keeps a
// compressed effect list where every entry carries the legacy slot index it came from, so the
// ported slot-based lookups go through these helpers.
namespace PlayerbotItem
{
    inline ItemEffectEntry const* GetEffectForSlot(ItemTemplate const* proto, uint32 slot)
    {
        if (!proto)
            return nullptr;

        for (ItemEffectEntry const* effect : proto->Effects)
            if (effect && uint32(effect->LegacySlotIndex) == slot)
                return effect;

        return nullptr;
    }

    inline uint32 GetSpellId(ItemTemplate const* proto, uint32 slot)
    {
        ItemEffectEntry const* effect = GetEffectForSlot(proto, slot);
        return effect ? uint32(effect->SpellID) : 0;
    }

    inline uint32 GetSpellTrigger(ItemTemplate const* proto, uint32 slot)
    {
        ItemEffectEntry const* effect = GetEffectForSlot(proto, slot);
        return effect ? uint32(effect->TriggerType) : 0;
    }

    inline int32 GetSpellCharges(ItemTemplate const* proto, uint32 slot)
    {
        ItemEffectEntry const* effect = GetEffectForSlot(proto, slot);
        return effect ? effect->Charges : 0;
    }

    inline int32 GetSpellCooldown(ItemTemplate const* proto, uint32 slot)
    {
        ItemEffectEntry const* effect = GetEffectForSlot(proto, slot);
        return effect ? effect->CoolDownMSec : 0;
    }

    // 3.3.5 exposed durability through ITEM_FIELD_DURABILITY/ITEM_FIELD_MAXDURABILITY;
    // 12.x keeps it in the item's update fields without public getters.
    inline uint32 GetDurability(Item const* item)
    {
        return item ? uint32(*item->m_itemData->Durability) : 0;
    }

    inline uint32 GetMaxDurability(Item const* item)
    {
        return item ? uint32(*item->m_itemData->MaxDurability) : 0;
    }

    inline int32 GetSpellCategory(ItemTemplate const* proto, uint32 slot)
    {
        ItemEffectEntry const* effect = GetEffectForSlot(proto, slot);
        return effect ? int32(effect->SpellCategoryID) : 0;
    }
}

#endif
