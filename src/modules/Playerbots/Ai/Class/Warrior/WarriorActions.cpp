/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WarriorActions.h"
#include "SpellHistory.h"
#include "AiFactory.h"
#include "Playerbots.h"

namespace
{
constexpr uint32 SPELL_RETALIATION = 20230;
}

bool CastBerserkerRageAction::isPossible()
{
    if (botAI->IsInVehicle() && !botAI->IsInVehicle(false, false, true))
        return false;

    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId)
        return false;

    if (!bot->HasSpell(spellId))
        return false;

    if (bot->GetSpellHistory()->HasCooldown(spellId))
        return false;

    return true;
}

bool CastBerserkerRageAction::isUseful()
{
    return (bot->HasAuraType(SPELL_AURA_MOD_FEAR) ||
           bot->HasAuraWithMechanic(1 << MECHANIC_SLEEP) ||
           bot->HasAuraWithMechanic(1 << MECHANIC_SAPPED))
        && !botAI->HasAura("berserker rage", bot)
        && CastSpellAction::isUseful();
}

bool CastSunderArmorAction::isUseful()
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    if (!PlayerbotAI::IsTank(bot, false))
    {
        for (GroupReference const& ref : group->GetMembers())
        {
            Player* member = ref.GetSource();
            if (!member || member == bot || !member->IsAlive() || !member->IsInWorld() ||
                member->GetMapId() != bot->GetMapId())
            {
                continue;
            }

            if (member->GetClass() == CLASS_WARRIOR &&
                PlayerbotAI::IsTank(member, false))
                return false;
        }
    }

    Aura* aura = botAI->GetAura("sunder armor", GetTarget(), false, true);
    return !aura || aura->GetStackAmount() < 5 || aura->GetDuration() <= 6000;
}

bool CastRetaliationAction::isUseful()
{
    if (!bot->HasSpell(SPELL_RETALIATION) || bot->GetSpellHistory()->HasCooldown(SPELL_RETALIATION) ||
        bot->HasAura(SPELL_RETALIATION))
    {
        return false;
    }

    uint8 meleeAttackers = 0;
    GuidVector attackers = AI_VALUE(GuidVector, "attackers");

    for (ObjectGuid const& guid : attackers)
    {
        Unit* attacker = botAI->GetUnit(guid);
        if (!attacker || !attacker->IsAlive() || attacker->GetVictim() != bot)
            continue;

        // Check if the attacker is melee-based using unit_class
        if (attacker->IsCreature())
        {
            Creature* creature = attacker->ToCreature();
            // 12.x removed Creature::IsClass(); the unit data carries the class id.
            if (creature && (creature->GetClass() == CLASS_WARRIOR
                || creature->GetClass() == CLASS_ROGUE
                || creature->GetClass() == CLASS_PALADIN))
            {
                ++meleeAttackers;
            }
        }
        else if (attacker->IsPlayer())
        {
            Player* playerAttacker = attacker->ToPlayer();
            if (playerAttacker && PlayerbotAI::IsMelee(playerAttacker)) // Reuse existing Player melee check
            {
                ++meleeAttackers;
            }
        }

        // Early exit if we already have enough melee attackers
        if (meleeAttackers >= 2)
            break;
    }

    // Only cast Retaliation if there are at least 2 melee attackers
    return meleeAttackers >= 2;
}
