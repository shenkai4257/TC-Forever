/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "StatsValues.h"
#include "Bag.h"
#include "ItemTemplateHelper.h"
#include "AiObjectContext.h"
#include "Group.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "ServerFacade.h"

Unit* HealthValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

uint8 HealthValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 100;

    return (static_cast<float>(target->GetHealth()) / target->GetMaxHealth()) * 100;
}

Unit* IsDeadValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

bool IsDeadValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    return target->getDeathState() != ALIVE;
}

bool PetIsDeadValue::Calculate()
{
    if ((bot->GetLevel() < 10 && bot->GetClass() == CLASS_HUNTER) || bot->IsMounted())
    {
        return false;
    }

    if (!bot->GetPet())
    {
        uint32 ownerid = bot->GetGUID().GetCounter();
        QueryResult result = CharacterDatabase.PQuery("SELECT id FROM character_pet WHERE owner = {}", ownerid);
        if (!result)
            return false;

        return true;
    }

    if (!bot->GetPetGUID().IsEmpty() && !bot->GetPet())
        return true;

    return bot->GetPet() && bot->GetPet()->getDeathState() != ALIVE;
}

bool PetIsHappyValue::Calculate() { return !bot->GetPet() || bot->GetPet()->GetHappinessState() == Pet::HAPPY; }

Unit* RageValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

uint8 RageValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 0;

    return (target->GetPower(POWER_RAGE) / 10.0f);
}

Unit* EnergyValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

uint8 EnergyValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 0;

    return (static_cast<float>(target->GetPower(POWER_ENERGY)));
}

Unit* ManaValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

uint8 ManaValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 100;

    return (static_cast<float>(target->GetPower(POWER_MANA)) / target->GetMaxPower(POWER_MANA)) * 100;
}

Unit* HasManaValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

bool HasManaValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    constexpr uint32 PRIEST_SPIRIT_OF_REDEMPTION_SPELL_ID = 27827u;
    if (target->HasAura(PRIEST_SPIRIT_OF_REDEMPTION_SPELL_ID))
        return false;

    return target->GetPower(POWER_MANA);
}

Unit* ComboPointsValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

uint8 ComboPointsValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target || target->GetGUID() != bot->GetClassicComboTarget())
        return 0;

    return uint8(bot->GetPower(POWER_COMBO_POINTS));
}

Unit* IsMountedValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

bool IsMountedValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    return target->IsMounted();
}

Unit* IsInCombatValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

bool IsInCombatValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (target->IsInCombat())
        return true;

    if (target == bot)
    {
        if (Group* group = bot->GetGroup())
        {
            Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
            for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
            {
                Player* member = ObjectAccessor::FindPlayer(itr->guid);
                if (!member || member == bot)
                    continue;

                if (member->IsInCombat() &&
                    ServerFacade::instance().IsDistanceLessOrEqualThan(ServerFacade::instance().GetDistance2d(member, bot),
                                                             PlayerbotAIConfig::Instance().ReactDistance))
                    return true;
            }
        }
    }

    return false;
}

bool IsInCombatValue::EqualToLast(bool value) { return value == lastValue; }

uint8 BagSpaceValue::Calculate()
{
    uint32 totalused = 0, total = 16;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
    {
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++totalused;
    }

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag const* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
        {
            ItemTemplate const* pBagProto = pBag->GetTemplate();
            if (pBagProto->GetClass() == ITEM_CLASS_CONTAINER && pBagProto->GetSubClass() == ITEM_SUBCLASS_CONTAINER)
            {
                total += pBag->GetBagSize();
                totalused += pBag->GetBagSize() - pBag->GetFreeSlots();
            }
        }
    }

    return (static_cast<float>(totalused) / total) * 100;
}

uint8 DurabilityValue::Calculate()
{
    uint32 totalMax = 0, total = 0;

    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        uint16 pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        Item* item = bot->GetItemByPos(pos);

        if (!item)
            continue;

        uint32 maxDurability = PlayerbotItem::GetMaxDurability(item);
        if (!maxDurability)
            continue;

        totalMax += maxDurability;

        uint32 curDurability = PlayerbotItem::GetDurability(item);

        total += curDurability;
    }

    if (total == 0)
        return 0;

    return (static_cast<float>(total) / totalMax) * 100;
}

Unit* SpeedValue::GetTarget()
{
    AiObjectContext* ctx = AiObject::context;
    return ctx->GetValue<Unit*>(qualifier)->Get();
}

uint8 SpeedValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 100;

    return (uint8)(100.0f * target->GetSpeedRate(MOVE_RUN));
}

bool IsInGroupValue::Calculate() { return bot->GetGroup(); }

bool ExperienceValue::EqualToLast(uint32 value) { return value != lastValue; }

uint32 ExperienceValue::Calculate() { return bot->GetXP(); }
