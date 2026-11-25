/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "InventoryChangeFailureAction.h"
#include "Event.h"
#include "Playerbots.h"

std::map<InventoryResult, std::string> InventoryChangeFailureAction::messages;

bool InventoryChangeFailureAction::Execute(Event event)
{
    if (!botAI->GetMaster())
        return false;

    if (messages.empty())
    {
        messages[EQUIP_ERR_CANT_EQUIP_LEVEL_I] = "My level is too low";
        messages[EQUIP_ERR_CANT_EQUIP_SKILL] = "My skill level is too low";
        messages[EQUIP_ERR_WRONG_SLOT] = "Invalid slot";
        messages[EQUIP_ERR_BAG_FULL] = "My bags are full";
        messages[EQUIP_ERR_BAG_IN_BAG] = "This bag is not empty";
        messages[EQUIP_ERR_TRADE_EQUIPPED_BAG] = "Cannot trade equipped bags";
        messages[EQUIP_ERR_AMMO_ONLY] = "Invalid slot (only ammo is required)";
        messages[EQUIP_ERR_PROFICIENCY_NEEDED] = "I don't have necessary skill";
        messages[EQUIP_ERR_NO_SLOT_AVAILABLE] = "No equipment slot available";
        messages[EQUIP_ERR_CANT_EQUIP_EVER] = "I will never be able to use this";
        messages[EQUIP_ERR_CANT_EQUIP_EVER_2] = "I will never be able to use this";
        messages[EQUIP_ERR_NO_SLOT_AVAILABLE_2] = messages[EQUIP_ERR_NO_SLOT_AVAILABLE];
        messages[EQUIP_ERR_2HANDED_EQUIPPED] = "Cannot equip with two-handed weapon equipped";
        messages[EQUIP_ERR_2HSKILLNOTFOUND] = "I cannot dual-wield";
        messages[EQUIP_ERR_WRONG_BAG_TYPE] = "This item cannot go in this bag";
        messages[EQUIP_ERR_WRONG_BAG_TYPE_2] = "This item cannot go in this bag";
        messages[EQUIP_ERR_ITEM_MAX_COUNT] = "I can't carry anymore of those";
        messages[EQUIP_ERR_NO_SLOT_AVAILABLE_3] = messages[EQUIP_ERR_NO_SLOT_AVAILABLE];
        messages[EQUIP_ERR_CANT_STACK] = "Item cannot stack";
        messages[EQUIP_ERR_NOT_EQUIPPABLE] = "Item cannot be equipped";
        messages[EQUIP_ERR_CANT_SWAP] = "Cannot swap these items";
        messages[EQUIP_ERR_SLOT_EMPTY] = "Nothing to equip";
        messages[EQUIP_ERR_ITEM_NOT_FOUND] = "Cannot find the item";
        messages[EQUIP_ERR_DROP_BOUND_ITEM] = "Cannot drop soulbound items";
        messages[EQUIP_ERR_OUT_OF_RANGE] = "I am out of range";
        messages[EQUIP_ERR_TOO_FEW_TO_SPLIT] = "Invalid split number";
        messages[EQUIP_ERR_SPLIT_FAILED] = "Cannot split this";
        messages[EQUIP_ERR_SPELL_FAILED_REAGENTS_GENERIC] = "Missing reagents";
        messages[EQUIP_ERR_CANT_TRADE_GOLD] = "Not enough money";
        messages[EQUIP_ERR_NOT_ENOUGH_MONEY] = "This is not a bag";
        messages[EQUIP_ERR_NOT_A_BAG] = "The bag is not empty";
        messages[EQUIP_ERR_DESTROY_NONEMPTY_BAG] = "This is not my item";
        messages[EQUIP_ERR_NOT_OWNER] = "Only quiver can be equipped";
        messages[EQUIP_ERR_ONLY_ONE_QUIVER] = "I must purchase the slot before";
        messages[EQUIP_ERR_NO_BANK_SLOT] = "I am too far away from bank";
        messages[EQUIP_ERR_NO_BANK_HERE] = "This item is locked";
        messages[EQUIP_ERR_ITEM_LOCKED] = "I am stunned";
        messages[EQUIP_ERR_GENERIC_STUNNED] = "I am dead";
        messages[EQUIP_ERR_PLAYER_DEAD] = "I can't do this right now";
        messages[EQUIP_ERR_CLIENT_LOCKED_OUT] = "Internal error";
        messages[EQUIP_ERR_INTERNAL_BAG_ERROR] = "Only bolts are allowed";
        messages[EQUIP_ERR_ONLY_ONE_BOLT] = "Ammo poach is allowed";
        messages[EQUIP_ERR_ONLY_ONE_AMMO] = "Item can't be wrapped";
        messages[EQUIP_ERR_CANT_WRAP_STACKABLE] = messages[EQUIP_ERR_ONLY_ONE_AMMO];
        messages[EQUIP_ERR_CANT_WRAP_EQUIPPED] = messages[EQUIP_ERR_ONLY_ONE_AMMO];
        messages[EQUIP_ERR_CANT_WRAP_WRAPPED] = messages[EQUIP_ERR_ONLY_ONE_AMMO];
        messages[EQUIP_ERR_CANT_WRAP_BOUND] = messages[EQUIP_ERR_ONLY_ONE_AMMO];
        messages[EQUIP_ERR_CANT_WRAP_UNIQUE] = messages[EQUIP_ERR_ONLY_ONE_AMMO];
        messages[EQUIP_ERR_CANT_WRAP_BAGS] = "Already looted";
        messages[EQUIP_ERR_LOOT_GONE] = "My inventory is full";
        messages[EQUIP_ERR_INV_FULL] = "My bank is full";
        messages[EQUIP_ERR_BANK_FULL] = "Item item is sold out";
        messages[EQUIP_ERR_VENDOR_SOLD_OUT] = messages[EQUIP_ERR_INV_FULL];
        messages[EQUIP_ERR_BAG_FULL_2] = messages[EQUIP_ERR_ITEM_NOT_FOUND];
        messages[EQUIP_ERR_ITEM_NOT_FOUND_2] = messages[EQUIP_ERR_CANT_STACK];
        messages[EQUIP_ERR_CANT_STACK_2] = messages[EQUIP_ERR_BAG_FULL];
        messages[EQUIP_ERR_BAG_FULL_3] = messages[EQUIP_ERR_BANK_FULL];
        messages[EQUIP_ERR_VENDOR_SOLD_OUT_2] = "This object is busy";
        messages[EQUIP_ERR_CANT_BE_DISENCHANTED] = "I am in combat";
        messages[EQUIP_ERR_NOT_IN_COMBAT] = "Cannot do while disarmed";
        messages[EQUIP_ERR_NOT_WHILE_DISARMED] = messages[EQUIP_ERR_BAG_FULL];
        messages[EQUIP_ERR_BAG_FULL_4] = "Not enough rank";
        messages[EQUIP_ERR_CANT_EQUIP_RANK] = "Not enough reputation";
        messages[EQUIP_ERR_CANT_EQUIP_REPUTATION] = "Too many special bags";
        messages[EQUIP_ERR_TOO_MANY_SPECIAL_BAGS] = "Cannot loot this right now";
    }

    WorldPacket p(event.getPacket());
    p.rpos(0);
    int32 err;     // 12.x sends the result as an int32 (3.3.5 used a byte)
    p >> err;
    if (err == EQUIP_ERR_OK)
        return false;

    std::string const msg = messages[(InventoryResult)err];
    if (!msg.empty())
    {
        botAI->TellError(msg);
        return true;
    }

    return false;
}
