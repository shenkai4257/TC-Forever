/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RogueActions.h"
#include "Event.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

namespace
{
constexpr uint32 SPELL_MASTER_POISONER_RANK_3 = 58410;
}

bool CastStealthAction::isUseful()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target && bot->GetDistance(target) >= sPlayerbotAIConfig.SpellDistance)
        return false;
    return true;
}

bool CastStealthAction::isPossible()
{
    return true;
}

bool UnstealthAction::Execute(Event /*event*/)
{
    botAI->RemoveAura("stealth");
    // botAI->ChangeStrategy("+combat,-stealthed", BOT_STATE_COMBAT);

    return true;
}

bool CheckStealthAction::Execute(Event /*event*/)
{
    if (botAI->HasAura("stealth", bot))
    {
        botAI->ChangeStrategy("-combat,+stealthed", BOT_STATE_COMBAT);
    }
    else
    {
        botAI->ChangeStrategy("+combat,-stealthed", BOT_STATE_COMBAT);
    }

    return true;
}

bool CastVanishAction::isUseful()
{
    return true;
}



bool UseDeadlyPoisonAction::Execute(Event /*event*/)
{
    std::vector<Item*> const items =
        AI_VALUE2(std::vector<Item*>, "inventory items", "Deadly Poison");
    for (Item* const item : items)
    {
        // This check is needed only if there is a name match. I think the only one is Handbook
        // of Deadly Poison V, which might be restored by IP. Keeping this here just in case.
        if (item->GetTemplate()->GetClass() != ITEM_CLASS_CONSUMABLE)
            continue;

        Item* const itemForSpell = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        return UseItem(item, ObjectGuid::Empty, itemForSpell);
    }

    return false;
}

bool UseInstantPoisonAction::Execute(Event /*event*/)
{
    std::vector<Item*> const items =
        AI_VALUE2(std::vector<Item*>, "inventory items", "Instant Poison");
    for (Item* const item : items)
    {
        // Not necessary for instant but keeping for symmetry and to guide a potential refactor.
        if (item->GetTemplate()->GetClass() != ITEM_CLASS_CONSUMABLE)
            continue;

        Item* const itemForSpell = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        return UseItem(item, ObjectGuid::Empty, itemForSpell);
    }

    return false;
}

bool UseInstantPoisonOffHandAction::Execute(Event /*event*/)
{
    std::vector<Item*> const items =
        AI_VALUE2(std::vector<Item*>, "inventory items", "Instant Poison");
    for (Item* const item : items)
    {
        // Not necessary for instant but keeping for symmetry and to guide a potential refactor.
        if (item->GetTemplate()->GetClass() != ITEM_CLASS_CONSUMABLE)
            continue;

        Item* const itemForSpell = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        return UseItem(item, ObjectGuid::Empty, itemForSpell);
    }

    return false;
}
