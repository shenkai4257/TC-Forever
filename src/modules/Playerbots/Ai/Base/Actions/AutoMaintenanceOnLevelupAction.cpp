/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AutoMaintenanceOnLevelupAction.h"
#include "BroadcastHelper.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotCompat.h"
#include "PlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "SpellMgr.h"

bool AutoMaintenanceOnLevelupAction::Execute(Event /*event*/)
{
    AutoPickTalents();
    AutoLearnSpell();
    AutoTeleportForLevel();
    AutoUpgradeEquip();

    return true;
}

void AutoMaintenanceOnLevelupAction::AutoTeleportForLevel()
{
    if (!sPlayerbotAIConfig.AutoTeleportForLevel || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return;

    if (botAI->HasGameClientMaster())
        return;

    sRandomPlayerbotMgr.RandomTeleportForLevel(bot);
    return;
}

void AutoMaintenanceOnLevelupAction::AutoPickTalents()
{
    bool const isRandomBot = sRandomPlayerbotMgr.IsRandomBot(bot);
    if (!(isRandomBot ? sPlayerbotAIConfig.AutoPickTalents
                      : sPlayerbotAIConfig.AutoPickTalentsForPlayerControlledBots))
        return;

    // 12.x has no free talent point counter (talents are trait configs); the first
    // point is earned at level 10, and the factory no-ops when there is nothing to spend.
    if (bot->GetLevel() < 10)
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitTalentsTree(true, true, isRandomBot);
    factory.InitPetTalents();
}

void AutoMaintenanceOnLevelupAction::AutoLearnSpell()
{
    std::ostringstream out;
    LearnSpells(&out);

    if (!out.str().empty())
    {
        std::string const temp = out.str();
        out.seekp(0);
        out << "Learned spells: ";
        out << temp;
        out.seekp(-2, out.cur);
        out << ".";
        botAI->TellMaster(out);
    }
    return;
}

void AutoMaintenanceOnLevelupAction::LearnSpells(std::ostringstream* out)
{
    BroadcastHelper::BroadcastLevelup(botAI, bot);
    if (sPlayerbotAIConfig.AutoLearnTrainerSpells && sRandomPlayerbotMgr.IsRandomBot(bot))
        LearnTrainerSpells(out);

    if (sPlayerbotAIConfig.AutoLearnQuestSpells && sRandomPlayerbotMgr.IsRandomBot(bot))
        LearnQuestSpells(out);
}

void AutoMaintenanceOnLevelupAction::LearnTrainerSpells(std::ostringstream* /*out*/)
{
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitSkills();
    factory.InitClassSpells();
    factory.InitAvailableSpells();
    factory.InitPet();
    // The trainer passes learn the riding line's own spells, which rewrite skill 762 through AddSpell
    // from the 3.3.5 skill_tiers table; the tier is re-asserted last.
    factory.InitRiding();
}

void AutoMaintenanceOnLevelupAction::LearnQuestSpells(std::ostringstream* out)
{
    ObjectMgr::QuestContainer const& questTemplates = sObjectMgr->GetQuestTemplates();
    for (ObjectMgr::QuestContainer::const_iterator i = questTemplates.begin(); i != questTemplates.end(); ++i)
    {
        Quest const* quest = i->second.get();

        if (!quest->GetAllowableClasses() || quest->IsRepeatable() || quest->GetClassicMinLevel() < 10 ||
            quest->GetClassicMinLevel() > bot->GetLevel())
        {
            continue;
        }

        if (!bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false) ||
            !bot->SatisfyQuestSkill(quest, false))
        {
            continue;
        }

        int32 spellId = quest->GetRewSpell();
        if (!spellId)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!spellInfo)
            continue;

        bool found = false;
        for (uint8 i = 0; i < spellInfo->GetEffects().size(); ++i)
        {
            if (GetEffectOrDefault(spellInfo, SpellEffIndex(i)).Effect == SPELL_EFFECT_LEARN_SPELL && GetEffectOrDefault(spellInfo, SpellEffIndex(i)).TriggerSpell &&
                !bot->HasSpell(GetEffectOrDefault(spellInfo, SpellEffIndex(i)).TriggerSpell))
            {
                if (SpellInfo const* triggeredInfo = sSpellMgr->GetSpellInfo(GetEffectOrDefault(spellInfo, SpellEffIndex(i)).TriggerSpell, DIFFICULTY_NONE))
                    if (!triggeredInfo->GetEffects().empty() && GetEffectOrDefault(triggeredInfo, EFFECT_0).Effect == SPELL_EFFECT_TRADE_SKILL)
                        break;

                found = true;
                break;
            }
        }

        if (!found)
            continue;

        bot->CastSpell(bot, spellId, true);

        uint32 rewSpellId = quest->GetRewSpell();
        if (rewSpellId)
        {
            if (SpellInfo const* rewSpellInfo = sSpellMgr->GetSpellInfo(rewSpellId, DIFFICULTY_NONE))
            {
                *out << FormatSpell(rewSpellInfo) << ", ";
                continue;
            }
        }

        *out << FormatSpell(spellInfo) << ", ";
    }
}

std::string const AutoMaintenanceOnLevelupAction::FormatSpell(SpellInfo const* sInfo)
{
    std::ostringstream out;
    uint8 const rankNum = sInfo->GetRank();
    std::string const rank = rankNum > 1 ? std::to_string(rankNum) : std::string();

    if (rank.empty())
        out << "|cffffffff|Hspell:" << sInfo->Id << "|h[" << sInfo->SpellName->Str[LOCALE_enUS] << "]|h|r";
    else
        out << "|cffffffff|Hspell:" << sInfo->Id << "|h[" << sInfo->SpellName->Str[LOCALE_enUS] << " " << rank << "]|h|r";

    return out.str();
}

void AutoMaintenanceOnLevelupAction::AutoUpgradeEquip()
{
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());

    factory.CleanupConsumables();

    factory.InitAmmo();
    factory.InitReagents();
    factory.InitFood();
    factory.InitConsumables();
    factory.InitPotions();

    if (sPlayerbotAIConfig.AutoUpgradeEquip)
        factory.InitEquipment(true);
}
