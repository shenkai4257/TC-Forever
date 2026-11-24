/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WarriorTriggers.h"
#include "SpellHistory.h"
#include "Playerbots.h"

namespace
{
constexpr uint32 SPELL_COMMANDING_PRESENCE_RANKS[] = { 12318, 12857, 12858, 12860, 12861 };
}

bool BloodrageBuffTrigger::IsActive()
{
    return AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.MediumHealth &&
           AI_VALUE2(uint8, "rage", "self target") < 20;
}

bool BattleShoutTrigger::IsActive()
{
    if (!BuffTrigger::IsActive())
        return false;

    uint32 battleShoutSpellId = AI_VALUE2(uint32, "spell id", "battle shout");
    if (!battleShoutSpellId)
        return false;

    SpellInfo const* bsInfo = sSpellMgr->GetSpellInfo(battleShoutSpellId, DIFFICULTY_NONE);
    if (!bsInfo)
        return false;

    int32 bsApValue = 0;
    for (uint8 eff = 0; eff < bsInfo->GetEffects().size(); ++eff)
    {
        if (bsInfo->GetEffectOrDefault(SpellEffIndex(eff)).ApplyAuraName == SPELL_AURA_MOD_ATTACK_POWER)
        {
            bsApValue = bsInfo->GetEffectOrDefault(SpellEffIndex(eff)).BasePoints + 1;
            break;
        }
    }
    if (!bsApValue)
        return false;

    static const float commandingPresenceBonus[]   = {
        0.05f, 0.10f, 0.15f, 0.20f, 0.25f };

    float cpBonus = 0.0f;
    for (int rank = 4; rank >= 0; --rank)
    {
        if (bot->HasAura(SPELL_COMMANDING_PRESENCE_RANKS[rank]))
        {
            cpBonus = commandingPresenceBonus[rank];
            break;
        }
    }
    int32 effectiveBsAp = int32(bsApValue * (1.0f + cpBonus));

    static char const* blessingNames[] = {
        "blessing of might", "greater blessing of might", nullptr
    };
    for (int i = 0; blessingNames[i] != nullptr; ++i)
    {
        Aura* bom = botAI->GetAura(blessingNames[i], bot);
        if (!bom)
            continue;

        SpellInfo const* bomInfo = bom->GetSpellInfo();
        if (!bomInfo)
            continue;

        for (uint8 eff = 0; eff < bomInfo->GetEffects().size(); ++eff)
        {
            if (bomInfo->GetEffectOrDefault(SpellEffIndex(eff)).ApplyAuraName == SPELL_AURA_MOD_ATTACK_POWER)
            {
                int32 bomApValue = bomInfo->GetEffectOrDefault(SpellEffIndex(eff)).BasePoints + 1;
                if (bomApValue >= effectiveBsAp)
                    return false;
                break;
            }
        }
    }

    return true;
}
