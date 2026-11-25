/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotFactory.h"
#include "SpellHistory.h"
#include "CreatureHelper.h"
#include "Bag.h"
#include "ItemTemplateHelper.h"
#include "FieldHelper.h"
#include "DatabaseEnv.h"
#include "AccountMgr.h"
#include "AiFactory.h"
#include "AiObjectContext.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "GuildMgr.h"
#include "InventoryAction.h"
#include "Item.h"
#include "ItemPackets.h"
#include "ItemTemplate.h"
#include "ItemVisitors.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "PerfMonitor.h"
#include "PetDefines.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotRepository.h"
#include "PlayerbotSpellRepository.h"
#include "ClassicTalentsApply.h"
#include "ClassicTalentBuilds.h"
#include "ClassicSpec.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "RandomItemMgr.h"
#include "RandomPlayerbotFactory.h"
#include "ReputationMgr.h"
#include "SharedDefines.h"
#include "StatsWeightCalculator.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"
#include <array>
#include <unordered_set>
#include <utility>

#include <array>
#include <utility>

const uint64 diveMask = (1LL << 7) | (1LL << 44) | (1LL << 37) | (1LL << 38) | (1LL << 26) | (1LL << 30) | (1LL << 27) |
                        (1LL << 33) | (1LL << 24) | (1LL << 34);

static std::vector<uint32> initSlotsOrder = {EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2, EQUIPMENT_SLOT_MAINHAND,
    EQUIPMENT_SLOT_OFFHAND, EQUIPMENT_SLOT_RANGED, EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_SHOULDERS, EQUIPMENT_SLOT_CHEST,
    EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_HANDS, EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_BODY, EQUIPMENT_SLOT_WAIST,
    EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2, EQUIPMENT_SLOT_BACK};

uint32 PlayerbotFactory::tradeSkills[] = {SKILL_ALCHEMY,         SKILL_ENCHANTING,   SKILL_SKINNING,
                                          SKILL_TAILORING,       SKILL_LEATHERWORKING, SKILL_ENGINEERING,
                                          SKILL_HERBALISM,       SKILL_MINING,
                                          SKILL_BLACKSMITHING,   SKILL_COOKING,      SKILL_FIRST_AID,
                                          SKILL_FISHING};

std::list<uint32> PlayerbotFactory::classQuestIds;
std::list<uint32> PlayerbotFactory::specialQuestIds;
std::vector<uint32> PlayerbotFactory::enchantSpellIdCache;
std::unordered_map<uint32, std::vector<uint32>> PlayerbotFactory::trainerIdCache;
std::vector<uint32> PlayerbotFactory::ccBreakTrinketCache;

namespace
{
// 1.60 hands riding out through the skill line's own spells too (33388 Apprentice, 33391 Journeyman,
// MinSkillLineRank 1/150). Learning one of those goes through Player::AddSpell's learn-skill branch,
// which rewrites skill 762 from the world `skill_tiers` table (the 3.3.5 75/150/225 rows), so the
// factory must not learn any of them behind InitSkills' back: InitSkills is the only authority for
// the bot's riding tier and learns the line's spells itself, in a fixed order.
bool IsRidingSkillSpell(SpellInfo const* info)
{
    if (!info)
        return false;
    for (SpellEffectInfo const& effect : info->GetEffects())
        if (effect.IsEffect(SPELL_EFFECT_SKILL) && effect.MiscValue == SKILL_RIDING)
            return true;
    return false;
}

constexpr uint32 SPELL_DRUID_THICK_HIDE = 16931;
constexpr uint32 SPELL_OWLKIN_FRENZY = 48393;
constexpr uint32 SPELL_PRIMAL_TENACITY = 33957;
constexpr uint32 SPELL_IMPROVED_BARKSKIN = 63411;

constexpr uint32 SPELL_SECOND_WIND = 29838;
constexpr uint32 SPELL_BLOOD_CRAZE = 16492;
constexpr uint32 SPELL_GAG_ORDER = 12958;

constexpr uint32 SPELL_SACRED_CLEANSING = 53553;
constexpr uint32 SPELL_RECKONING = 20179;
constexpr uint32 SPELL_DIVINE_PURPOSE = 31872;

constexpr uint32 SPELL_HUNTER_THICK_HIDE = 19612;
constexpr uint32 SPELL_CONCUSSIVE_BARRAGE = 35102;
constexpr uint32 SPELL_ENTRAPMENT = 19388;

constexpr uint32 SPELL_DEADLY_BREW = 51626;
constexpr uint32 SPELL_THROWING_SPECIALIZATION = 51679;
constexpr uint32 SPELL_WAYLAY = 51696;

constexpr uint32 SPELL_IMPROVED_MANA_BURN = 14772;
constexpr uint32 SPELL_BODY_AND_SOUL = 64129;
constexpr uint32 SPELL_IMPROVED_VAMPIRIC_EMBRACE = 27840;

constexpr uint32 SPELL_ABOMINATIONS_MIGHT = 53138;
constexpr uint32 SPELL_IMPROVED_ICY_TALONS = 55610;
constexpr uint32 SPELL_SUDDEN_DOOM = 49529;
constexpr uint32 SPELL_ACCLIMATION = 50152;
constexpr uint32 SPELL_MAGIC_SUPPRESSION = 49611;

constexpr uint32 SPELL_SHAMAN_DUAL_WIELD = 30798;
constexpr uint32 SPELL_ASTRAL_SHIFT = 51479;
constexpr uint32 SPELL_EARTHEN_POWER = 51524;
constexpr uint32 SPELL_FOCUSED_MIND = 30866;

constexpr uint32 SPELL_BURNOUT = 44472;
constexpr uint32 SPELL_ICE_SHARDS = 15047;
constexpr uint32 SPELL_IMPROVED_BLINK = 31570;
constexpr uint32 SPELL_FIERY_PAYBACK = 64357;
constexpr uint32 SPELL_SHATTERED_BARRIER = 54787;

constexpr uint32 SPELL_IMPROVED_HOWL_OF_TERROR = 30057;
constexpr uint32 SPELL_NEMESIS = 63123;
constexpr uint32 SPELL_INTENSITY = 18136;
constexpr uint32 SPELL_NETHER_PROTECTION = 30302;

// Some creature_template rows are Blizzard developer leftovers or placeholders that carry the tameable
// flag but have no spawn in the world - e.g. 5596 "Twain Test Prop", a wolf family beast wearing a
// dragon whelp model. No player can ever tame those, so bots should not end up with them either.
bool HasCreatureSpawnRow(uint32 entry)
{
    static std::unordered_set<uint32> const spawnedEntries = []  // built once, at first use
    {
        std::unordered_set<uint32> entries;
        for (auto const& itr : sObjectMgr->GetAllCreatureData())
        {
            CreatureData const& data = itr.second;
            entries.insert(data.id);
        }
        return entries;
    }();

    return spawnedEntries.find(entry) != spawnedEntries.end();
}
}

bool PlayerbotFactory::IsPrimaryTradeSkill(uint16 skillId)
{
    SkillLineEntry const* skillLine = sSkillLineStore.LookupEntry(skillId);
    return skillLine && skillLine->CategoryID == SKILL_CATEGORY_PROFESSION;
}

bool PlayerbotFactory::IsSecondaryTradeSkill(uint16 skillId)
{
    switch (skillId)
    {
        case SKILL_COOKING:
        case SKILL_FIRST_AID:
        case SKILL_FISHING:
            return true;
        default:
            return false;
    }
}

uint16 PlayerbotFactory::GetTrainerSpellTradeSkill(Trainer::Spell const* trainerSpell)
{
    if (!trainerSpell)
        return 0;

    auto getSpellTradeSkill = [](uint32 spellId) -> uint16
    {
        if (SpellLearnSkillNode const* learnSkill = sSpellMgr->GetSpellLearnSkill(spellId))
        {
            if (IsPrimaryTradeSkill(learnSkill->skill) || IsSecondaryTradeSkill(learnSkill->skill))
                return learnSkill->skill;
        }

        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            uint16 const skillId = itr->second->SkillLine;
            if (IsPrimaryTradeSkill(skillId) || IsSecondaryTradeSkill(skillId))
                return skillId;
        }

        return 0;
    };

    uint16 const requiredSkill = static_cast<uint16>(trainerSpell->ReqSkillLine);
    if (IsPrimaryTradeSkill(requiredSkill) || IsSecondaryTradeSkill(requiredSkill))
        return requiredSkill;

    if (uint16 const skillId = getSpellTradeSkill(trainerSpell->SpellId))
        return skillId;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(trainerSpell->SpellId, DIFFICULTY_NONE);
    if (!spellInfo)
        return 0;

    for (uint8 effectIndex = 0; effectIndex < spellInfo->GetEffects().size(); ++effectIndex)
    {
        if (GetEffectOrDefault(spellInfo, SpellEffIndex(effectIndex)).Effect != SPELL_EFFECT_LEARN_SPELL)
            continue;

        uint32 const learnedSpellId = GetEffectOrDefault(spellInfo, SpellEffIndex(effectIndex)).TriggerSpell;
        if (!learnedSpellId)
            continue;

        if (uint16 const skillId = getSpellTradeSkill(learnedSpellId))
            return skillId;
    }

    return 0;
}

bool PlayerbotFactory::IsTrainerSpellAllowedForBot(Player* bot, Trainer::Trainer const* trainer,
                                                     Trainer::Spell const* trainerSpell)
{
    if (!bot || !trainer || !trainerSpell)
        return false;

    if (trainer->GetType() != Trainer::Type::Tradeskill || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return true;

    uint16 const skillId = GetTrainerSpellTradeSkill(trainerSpell);
    if (!skillId)
        return false;

    if (IsSecondaryTradeSkill(skillId))
        return true;

    if (!IsPrimaryTradeSkill(skillId))
        return false;

    uint16 const firstSkill = sRandomPlayerbotMgr.GetValue(bot, "firstSkill");
    uint16 const secondSkill = sRandomPlayerbotMgr.GetValue(bot, "secondSkill");

    if ((IsPrimaryTradeSkill(firstSkill) && skillId == firstSkill) ||
        (IsPrimaryTradeSkill(secondSkill) && skillId == secondSkill))
        return true;

    if (IsPrimaryTradeSkill(firstSkill) || IsPrimaryTradeSkill(secondSkill))
        return false;

    uint32 knownPrimarySkills = 0;
    for (uint32 tradeSkill : tradeSkills)
    {
        if (IsPrimaryTradeSkill(tradeSkill) && bot->HasSkill(tradeSkill))
            ++knownPrimarySkills;
    }

    uint32 const maxPrimaryTradeSkills =
        std::min<uint32>(2, sWorld->getIntConfig(CONFIG_MAX_PRIMARY_TRADE_SKILL));
    return knownPrimarySkills <= maxPrimaryTradeSkills && bot->HasSkill(skillId);
}

bool PlayerbotFactory::IsGatheringTradeSkill(uint16 skillId)
{
    switch (skillId)
    {
        case SKILL_HERBALISM:
        case SKILL_MINING:
        case SKILL_SKINNING:
            return true;
        default:
            return false;
    }
}

bool PlayerbotFactory::IsCraftingTradeSkill(uint16 skillId)
{
    return IsPrimaryTradeSkill(skillId) && !IsGatheringTradeSkill(skillId);
}

uint32 PlayerbotFactory::GetProfessionStarterSpell(uint16 skillId)
{
    static constexpr std::array<std::pair<uint16, uint32>, 14> ProfessionStarterSpells = {{
        {SKILL_ALCHEMY, 2259},
        {SKILL_BLACKSMITHING, 2018},
        {SKILL_COOKING, 2550},
        {SKILL_ENCHANTING, 7411},
        {SKILL_ENGINEERING, 4036},
        {SKILL_FIRST_AID, 3273},
        {SKILL_FISHING, 7620},
        {SKILL_HERBALISM, 2366},
        {SKILL_INSCRIPTION, 45357},
        {SKILL_LEATHERWORKING, 2108},
        {SKILL_MINING, 2575},
        {SKILL_SKINNING, 8613},
        {SKILL_TAILORING, 3908}
    }};

    for (auto const& [professionSkill, starterSpell] : ProfessionStarterSpells)
    {
        if (professionSkill == skillId)
            return starterSpell;
    }

    return 0;
}

std::vector<PlayerbotFactory::WeightedProfessionPair> PlayerbotFactory::GetClassProfessionPairs(Player* bot)
{
    switch (bot->GetClass())
    {
        case CLASS_WARRIOR:
            return {{SKILL_MINING, SKILL_BLACKSMITHING, 45},
                    {SKILL_MINING, SKILL_ENGINEERING, 30},
                    {SKILL_HERBALISM, SKILL_ALCHEMY, 10}};
        case CLASS_PALADIN:
            return {{SKILL_MINING, SKILL_BLACKSMITHING, 45},
                    {SKILL_MINING, SKILL_ENGINEERING, 15},
                    {SKILL_HERBALISM, SKILL_ALCHEMY, 10}};
        case CLASS_HUNTER:
            return {{SKILL_SKINNING, SKILL_LEATHERWORKING, 45},
                    {SKILL_MINING, SKILL_ENGINEERING, 35},
                    {SKILL_HERBALISM, SKILL_ALCHEMY, 10}};
        case CLASS_ROGUE:
            return {{SKILL_SKINNING, SKILL_LEATHERWORKING, 35},
                    {SKILL_HERBALISM, SKILL_ALCHEMY, 25},
                    {SKILL_MINING, SKILL_ENGINEERING, 25},
                    {SKILL_HERBALISM, SKILL_INSCRIPTION, 5}};
        case CLASS_DRUID:
            return {{SKILL_SKINNING, SKILL_LEATHERWORKING, 35},
                    {SKILL_HERBALISM, SKILL_ALCHEMY, 35},
                    {SKILL_HERBALISM, SKILL_INSCRIPTION, 20}};
        case CLASS_SHAMAN:
            return {{SKILL_HERBALISM, SKILL_ALCHEMY, 35},
                    {SKILL_SKINNING, SKILL_LEATHERWORKING, 25},
                    {SKILL_HERBALISM, SKILL_INSCRIPTION, 25}};
        case CLASS_PRIEST:
            return {{SKILL_TAILORING, SKILL_ENCHANTING, 45},
                    {SKILL_HERBALISM, SKILL_INSCRIPTION, 30},
                    {SKILL_HERBALISM, SKILL_ALCHEMY, 25}};
        case CLASS_MAGE:
            return {{SKILL_TAILORING, SKILL_ENCHANTING, 50},
                    {SKILL_HERBALISM, SKILL_ALCHEMY, 25},
                    {SKILL_HERBALISM, SKILL_INSCRIPTION, 25}};
        case CLASS_WARLOCK:
        default:
            return {{SKILL_TAILORING, SKILL_ENCHANTING, 50},
                    {SKILL_HERBALISM, SKILL_ALCHEMY, 25},
                    {SKILL_HERBALISM, SKILL_INSCRIPTION, 25}};
    }
}

std::vector<PlayerbotFactory::WeightedProfessionPair> PlayerbotFactory::GetRandomProfessionPairs()
{
    return {{SKILL_MINING, SKILL_BLACKSMITHING, 20},
            {SKILL_MINING, SKILL_ENGINEERING, 18},
            {SKILL_SKINNING, SKILL_LEATHERWORKING, 18},
            {SKILL_HERBALISM, SKILL_ALCHEMY, 18},
            {SKILL_HERBALISM, SKILL_INSCRIPTION, 14},
            {SKILL_TAILORING, SKILL_ENCHANTING, 10},
            {SKILL_HERBALISM, SKILL_MINING, 6},
            {SKILL_HERBALISM, SKILL_SKINNING, 5},
            {SKILL_MINING, SKILL_SKINNING, 5}};
}

std::pair<uint16, uint16> PlayerbotFactory::ChooseProfessionPair(
    std::vector<WeightedProfessionPair> const& professionPairs)
{
    uint32 totalWeight = 0;
    for (WeightedProfessionPair const& pair : professionPairs)
        totalWeight += pair.weight;

    if (!totalWeight)
        return {SKILL_HERBALISM, SKILL_ALCHEMY};

    uint32 roll = urand(1, totalWeight);
    for (WeightedProfessionPair const& pair : professionPairs)
    {
        if (roll <= pair.weight)
            return {pair.firstSkill, pair.secondSkill};

        roll -= pair.weight;
    }

    WeightedProfessionPair const& fallback = professionPairs.back();
    return {fallback.firstSkill, fallback.secondSkill};
}

uint16 PlayerbotFactory::ChooseSingleProfession(std::vector<WeightedProfessionPair> const& professionPairs)
{
    std::vector<std::pair<uint16, uint32>> gatheringSkills;
    std::vector<std::pair<uint16, uint32>> craftingSkills;

    auto addWeightedSkill = [](std::vector<std::pair<uint16, uint32>>& skills, uint16 skillId, uint32 weight)
    {
        for (std::pair<uint16, uint32>& skill : skills)
        {
            if (skill.first == skillId)
            {
                skill.second += weight;
                return;
            }
        }

        skills.push_back({skillId, weight});
    };

    for (WeightedProfessionPair const& pair : professionPairs)
    {
        if (IsGatheringTradeSkill(pair.firstSkill))
            addWeightedSkill(gatheringSkills, pair.firstSkill, pair.weight);
        if (IsCraftingTradeSkill(pair.firstSkill))
            addWeightedSkill(craftingSkills, pair.firstSkill, pair.weight);

        if (IsGatheringTradeSkill(pair.secondSkill))
            addWeightedSkill(gatheringSkills, pair.secondSkill, pair.weight);
        if (IsCraftingTradeSkill(pair.secondSkill))
            addWeightedSkill(craftingSkills, pair.secondSkill, pair.weight);
    }

    std::vector<std::pair<uint16, uint32>>* selectedPool = nullptr;
    if (!gatheringSkills.empty() && !craftingSkills.empty())
        selectedPool = urand(0, 1) == 0 ? &gatheringSkills : &craftingSkills;
    else if (!gatheringSkills.empty())
        selectedPool = &gatheringSkills;
    else if (!craftingSkills.empty())
        selectedPool = &craftingSkills;

    if (!selectedPool || selectedPool->empty())
        return SKILL_HERBALISM;

    uint32 totalWeight = 0;
    for (std::pair<uint16, uint32> const& skill : *selectedPool)
        totalWeight += skill.second;

    if (!totalWeight)
        return selectedPool->front().first;

    uint32 roll = urand(1, totalWeight);
    for (std::pair<uint16, uint32> const& skill : *selectedPool)
    {
        if (roll <= skill.second)
            return skill.first;

        roll -= skill.second;
    }

    return selectedPool->back().first;
}

uint16 PlayerbotFactory::ChooseComplementaryProfession(
    std::vector<WeightedProfessionPair> const& professionPairs, uint16 existingSkill)
{
    std::vector<std::pair<uint16, uint32>> candidates;
    uint32 totalWeight = 0;

    for (WeightedProfessionPair const& pair : professionPairs)
    {
        uint16 candidate = 0;
        if (pair.firstSkill == existingSkill)
            candidate = pair.secondSkill;
        else if (pair.secondSkill == existingSkill)
            candidate = pair.firstSkill;

        if (!candidate || candidate == existingSkill)
            continue;

        bool merged = false;
        for (std::pair<uint16, uint32>& existingCandidate : candidates)
        {
            if (existingCandidate.first != candidate)
                continue;

            existingCandidate.second += pair.weight;
            merged = true;
            break;
        }

        if (!merged)
            candidates.push_back({candidate, pair.weight});

        totalWeight += pair.weight;
    }

    if (candidates.empty() || !totalWeight)
    {
        std::pair<uint16, uint16> const fallback = ChooseProfessionPair(professionPairs);
        return fallback.first != existingSkill ? fallback.first : fallback.second;
    }

    uint32 roll = urand(1, totalWeight);
    for (std::pair<uint16, uint32> const& candidate : candidates)
    {
        if (roll <= candidate.second)
            return candidate.first;

        roll -= candidate.second;
    }

    return candidates.back().first;
}

uint32 PlayerbotFactory::GetStoredOrRandomValue(Player* bot,
                                                std::string const& key,
                                                uint32 minValue,
                                                uint32 maxValue)
{
    uint32 value = sRandomPlayerbotMgr.GetValue(bot, key);
    if (value < minValue || value > maxValue)
    {
        value = urand(minValue, maxValue);
        sRandomPlayerbotMgr.SetValue(bot, key, value);
    }

    return value;
}

bool PlayerbotFactory::HasAnySpell(Player* bot, std::vector<uint32> const& spells)
{
    for (uint32 spellId : spells)
    {
        if (bot->HasSpell(spellId))
            return true;
    }

    return false;
}

bool PlayerbotFactory::LearnProfessionSpecialization(Player* bot,
                                                     ProfessionSpecializationSpell knownSpell,
                                                     ProfessionSpecializationSpell learnSpell)
{
    uint32 const knownSpellId = static_cast<uint32>(knownSpell);
    uint32 const learnSpellId = static_cast<uint32>(learnSpell);

    if (bot->HasSpell(knownSpellId))
        return false;

    // Classic 1.60 has no switch spell for the weapon smithing specialties (39097-39099 are 3.3.5 ids):
    // a specialty without one is learned directly.
    if (!sSpellMgr->GetSpellInfo(learnSpellId, DIFFICULTY_NONE))
    {
        if (!sSpellMgr->GetSpellInfo(knownSpellId, DIFFICULTY_NONE))
            return false;

        bot->LearnSpell(knownSpellId, false);
        return bot->HasSpell(knownSpellId);
    }

    bot->CastSpell(bot, learnSpellId, true);
    return bot->HasSpell(knownSpellId);
}

PlayerbotFactory::PlayerbotFactory(Player* bot, uint32 level, uint32 itemQuality, uint32 gearScoreLimit)
    : level(level), itemQuality(itemQuality), gearScoreLimit(gearScoreLimit), bot(bot)
{
    botAI = GET_PLAYERBOT_AI(bot);
    if (!this->itemQuality)
    {
        uint32 gs = sPlayerbotAIConfig.RandomGearScoreLimit == 0
                        ? 0
                        : PlayerbotFactory::CalcMixedGearScore(sPlayerbotAIConfig.RandomGearScoreLimit,
                                                               sPlayerbotAIConfig.RandomGearQualityLimit);
        this->itemQuality = sPlayerbotAIConfig.RandomGearQualityLimit;
        this->gearScoreLimit = gs;
    }
}

void PlayerbotFactory::Init()
{
    if (sPlayerbotAIConfig.RandomBotPreQuests)
    {
        ObjectMgr::QuestContainer const& questTemplates = sObjectMgr->GetQuestTemplates();
        for (ObjectMgr::QuestContainer::const_iterator i = questTemplates.begin(); i != questTemplates.end(); ++i)
        {
            uint32 questId = i->first;
            Quest const* quest = i->second.get();

            if (!quest->GetAllowableClasses() || quest->IsRepeatable() || quest->GetClassicMinLevel() < 10)
                continue;

            if (quest->GetRewSpell() > 0)
            {
                int32 spellId = quest->GetRewSpell();
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
                if (!spellInfo)
                    continue;
            }
            else if (quest->GetRewSpell() > 0)
            {
                int32 spellId = quest->GetRewSpell();
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
                if (!spellInfo)
                    continue;
            }

            AddPrevQuests(questId, classQuestIds);
            classQuestIds.remove(questId);
            classQuestIds.push_back(questId);
        }
    }

    for (std::vector<uint32>::iterator i = sPlayerbotAIConfig.RandomBotQuestIds.begin();
         i != sPlayerbotAIConfig.RandomBotQuestIds.end(); ++i)
    {
        uint32 questId = *i;
        AddPrevQuests(questId, specialQuestIds);
        specialQuestIds.remove(questId);
        specialQuestIds.push_back(questId);
    }
    // 12.x exposes no "all spells" store; enchant scroll spells are taught through a skill line.
    for (uint32 abilityIndex = 0; abilityIndex < sSkillLineAbilityStore.GetNumRows(); ++abilityIndex)
    {
        SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(abilityIndex);
        if (!ability)
            continue;
        uint32 id = ability->Spell;
        if (id == 7218 || id == 19927 || id == 44119 || id == 47147 || id == 47181 ||
            id == 47242 || id == 50358 || id == 52639) // Test Enchants
            continue;

        if (id == 35791 || id == 39405) // Grandfathered TBC Enchants
            continue;

        if (id == 15463 || id == 15490) // Legendary Arcane Amalgamation
            continue;

        if (id == 29467 || id == 29475 || id == 29480 || id == 29483) // Naxx40 Sapphiron Shoulder Enchants
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
        if (!spellInfo)
            continue;

        //uint32 requiredLevel = spellInfo->BaseLevel; //not used, line marked for removal.

        for (uint8 j = 0; j < spellInfo->GetEffects().size(); ++j)
        {
            if (GetEffectOrDefault(spellInfo, SpellEffIndex(j)).Effect != SPELL_EFFECT_ENCHANT_ITEM)
                continue;

            uint32 enchant_id = GetEffectOrDefault(spellInfo, SpellEffIndex(j)).MiscValue;
            if (!enchant_id)
                continue;

            SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
            if (!enchant)
                continue;

            // SpellInfo const* enchantSpell = sSpellMgr->GetSpellInfo(enchant->EffectArg[0], DIFFICULTY_NONE);
            // if (!enchantSpell)
            //     continue;
            if (strstr(spellInfo->SpellName->Str[0], "Test"))
                break;

            enchantSpellIdCache.push_back(id);
            break;
            // TC_LOG_INFO("playerbots", "Add {} to enchantment spells", id);
        }
    }
    TC_LOG_INFO("playerbots", "Loading {} enchantment spells", enchantSpellIdCache.size());

    BuildCcBreakTrinketCache();
}

void PlayerbotFactory::BuildCcBreakTrinketCache()
{
    ccBreakTrinketCache.clear();
    // Spell 42292: removes all movement-impairing and loss-of-control effects — the PvP trinket spell.
    // 3.3.5 queried world.item_template; 12.x keeps items in the hotfixes DB2 store, exposed here
    // through sObjectMgr->GetItemTemplateStore().
    struct CcItem { uint32 itemId; uint16 itemLevel; };
    std::vector<CcItem> tmp;

    ItemTemplateContainer const& itemTemplates = sObjectMgr->GetItemTemplateStore();
    for (auto const& itr : itemTemplates)
    {
        ItemTemplate const* proto = &itr.second;
        if (proto->GetQuality() < ITEM_QUALITY_UNCOMMON || proto->GetInventoryType() != INVTYPE_TRINKET)
            continue;

        bool hasCcBreak = false;
        for (ItemEffectEntry const* itemEffect : proto->Effects)
        {
            if (itemEffect && itemEffect->SpellID == 42292)
            {
                hasCcBreak = true;
                break;
            }
        }
        if (!hasCcBreak)
            continue;

        tmp.push_back({itr.first, uint16(proto->GetBaseItemLevel())});
    }

    if (tmp.empty())
    {
        TC_LOG_INFO("playerbots", "CC-break trinket cache: no items found.");
        return;
    }

    std::sort(tmp.begin(), tmp.end(), [](CcItem const& a, CcItem const& b) {
        return a.itemLevel > b.itemLevel;
    });
    for (auto& c : tmp)
        ccBreakTrinketCache.push_back(c.itemId);

    TC_LOG_INFO("playerbots", "CC-break trinket cache: {} items.", ccBreakTrinketCache.size());
}

uint8 PlayerbotFactory::GetPreferredArmorType(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR:
        case CLASS_PALADIN:
            return ITEM_SUBCLASS_ARMOR_PLATE;

        case CLASS_HUNTER:
        case CLASS_SHAMAN:
            return ITEM_SUBCLASS_ARMOR_MAIL;

        case CLASS_ROGUE:
        case CLASS_DRUID:
            return ITEM_SUBCLASS_ARMOR_LEATHER;

        case CLASS_PRIEST:
        case CLASS_MAGE:
        case CLASS_WARLOCK:
            return ITEM_SUBCLASS_ARMOR_CLOTH;

        default:
            return 0;
    }
}

void PlayerbotFactory::Prepare()
{
    if (bot->isDead())
        bot->ResurrectPlayer(1.0f, false);

    bot->CombatStop(true);
    uint32 currentLevel = bot->GetLevel();
    bot->GiveLevel(level);
    if (level != currentLevel)
    {
        bot->SetXP(0);
    }

    // 12.x moved the helmet/cloak visibility toggles to the client; nothing to set here.
}

void PlayerbotFactory::Randomize(bool incremental)
{
    // if (sPlayerbotAIConfig.DisableRandomLevels)
    //     return;

    TC_LOG_DEBUG("playerbots", "{} randomizing {} (level {} class = {})...", (incremental ? "Incremental" : "Full"),
             bot->GetName().c_str(), level, bot->GetClass());
    // TC_LOG_DEBUG("playerbots", "Preparing to {} randomize...", (incremental ? "incremental" : "full"));
    uint32 oldLevel = bot->GetLevel();
    Prepare();
    TC_LOG_DEBUG("playerbots", "Resetting player...");
    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Reset");

    if (!sPlayerbotAIConfig.EquipAndSpecPersistence ||
        level < uint32(sPlayerbotAIConfig.EquipAndSpecPersistenceLevel))
    {
        bot->ResetTalents(true);
    }

    if (!incremental)
    {
        ClearSkills();
        ClearSpells();
        ResetQuests();

        if (!sPlayerbotAIConfig.EquipAndSpecPersistence ||
            level < uint32(sPlayerbotAIConfig.EquipAndSpecPersistenceLevel) || level < oldLevel)
        {
            ClearAllItems();
        }
    }
    ClearInventory();
    bot->GetSpellHistory()->ResetAllCooldowns();
    UnbindInstance();

    bot->GiveLevel(level);
    bot->InitStatsForLevel(true);
    CancelAuras();
    // bot->SaveToDB(false);
    if (pmo)
        pmo->finish();

    // pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Immersive");
    // TC_LOG_INFO("playerbots", "Initializing immersive...");
    // InitImmersive();
    // if (pmo)
    //     pmo->finish();

    if (sPlayerbotAIConfig.RandomBotPreQuests)
    {
        pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Quests");
        InitInstanceQuests();
        InitAttunementQuests();
        if (pmo)
            pmo->finish();
    }
    else
    {
        pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Quests");
        InitAttunementQuests();
        if (pmo)
            pmo->finish();
    }

    TC_LOG_DEBUG("playerbots", "Initializing skills (step 1)...");
    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Skills1");
    bot->LearnDefaultSkills();
    InitSkills();
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Spells1");
    TC_LOG_DEBUG("playerbots", "Initializing spells (step 1)...");
    InitClassSpells();
    InitRacials();
    InitAvailableSpells();
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Talents");
    TC_LOG_DEBUG("playerbots", "Initializing talents...");
    if (!incremental || !sPlayerbotAIConfig.EquipAndSpecPersistence ||
        bot->GetLevel() < sPlayerbotAIConfig.EquipAndSpecPersistenceLevel)
    {
        uint32 specIndex = InitTalentsTree();
        sRandomPlayerbotMgr.SetValue(bot->GetGUID().GetCounter(), "specNo", specIndex + 1);
    }
    if (botAI)
    {
        PlayerbotRepository::instance().Reset(botAI);
        // botAI->DoSpecificAction("auto talents");
        botAI->ResetStrategies(false);  // fix wrong stored strategy
    }
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Spells2");
    TC_LOG_DEBUG("playerbots", "Initializing spells (step 2)...");
    InitAvailableSpells();
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Reputation");
    TC_LOG_DEBUG("playerbots", "Initializing reputation...");
    InitReputation();
    if (pmo)
        pmo->finish();

    TC_LOG_DEBUG("playerbots", "Initializing special spells...");
    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Spells3");
    InitSpecialSpells();
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Mounts");
    TC_LOG_DEBUG("playerbots", "Initializing mounts...");
    InitMounts();
    // Final authority for the riding tier: every spell-learning step is done by now, and any of them
    // may have rewritten skill 762 through AddSpell's learn-skill branch.
    InitRiding();
    // bot->SaveToDB(false);
    if (pmo)
        pmo->finish();

    // pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Skills2");
    // TC_LOG_INFO("playerbots", "Initializing skills (step 2)...");
    // UpdateTradeSkills();
    // if (pmo)
    //     pmo->finish();

    // Bags before equipment: InitEquipment puts the item an upgrade replaces into the bags and skips the slot
    // when they are full.
    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Bags");
    TC_LOG_DEBUG("playerbots", "Initializing bags...");
    InitBags();
    // bot->SaveToDB(false);
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Equip");
    TC_LOG_DEBUG("playerbots", "Initializing equipmemt...");
    if (!incremental || !sPlayerbotAIConfig.EquipAndSpecPersistence ||
        bot->GetLevel() < sPlayerbotAIConfig.EquipAndSpecPersistenceLevel)
    {
        InitEquipment(incremental, incremental ? false : sPlayerbotAIConfig.TwoRoundsGearInit);
    }
    // bot->SaveToDB(false);
    if (pmo)
        pmo->finish();

    // if (bot->GetLevel() >= sPlayerbotAIConfig.MinEnchantingBotLevel)
    // {
    //     pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Enchant");
    //     TC_LOG_INFO("playerbots", "Initializing enchant templates...");
    //     LoadEnchantContainer();
    //     if (pmo)
    //         pmo->finish();
    // }

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Ammo");
    TC_LOG_DEBUG("playerbots", "Initializing ammo...");
    InitAmmo();
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Food");
    TC_LOG_DEBUG("playerbots", "Initializing food...");
    InitFood();
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Potions");
    TC_LOG_DEBUG("playerbots", "Initializing potions...");
    InitPotions();
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Reagents");
    TC_LOG_DEBUG("playerbots", "Initializing reagents...");
    InitReagents();
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Keys");
    TC_LOG_DEBUG("playerbots", "Initializing keys...");
    InitKeyring();
    if (pmo)
        pmo->finish();

    // pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_EqSets");
    // TC_LOG_DEBUG("playerbots", "Initializing second equipment set...");
    //    InitSecondEquipmentSet();
    // if (pmo)
    //     pmo->finish();

    if (bot->GetLevel() >= sPlayerbotAIConfig.MinEnchantingBotLevel)
    {
        ApplyEnchants();
    }
    // {
    // pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_EnchantTemplate");
    // TC_LOG_INFO("playerbots", "Initializing enchant templates...");
    // ApplyEnchantTemplate();
    // if (pmo)
    //     pmo->finish();
    // }

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Inventory");
    TC_LOG_DEBUG("playerbots", "Initializing inventory...");
    // InitInventory();
    if (pmo)
        pmo->finish();

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Consumable");
    TC_LOG_DEBUG("playerbots", "Initializing consumables...");
    InitConsumables();
    if (pmo)
        pmo->finish();

    // bot->SaveToDB(false);

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Guilds");
    // bot->SaveToDB(false);
    if (sPlayerbotAIConfig.RandomBotGuildCount > 0)
    {
        TC_LOG_DEBUG("playerbots", "Initializing guilds...");
        InitGuild();
    }
    // bot->SaveToDB(false);
    if (pmo)
        pmo->finish();


    if (!incremental)
    {
        bot->RemovePet(nullptr, PET_SAVE_AS_CURRENT, true);
        bot->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT, true);
        // bot->SaveToDB(false);
    }
    if (bot->GetLevel() >= 10)
    {
        pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Pet");
        TC_LOG_DEBUG("playerbots", "Initializing pet...");
        InitPet();
        // bot->SaveToDB(false);
        InitPetTalents();
        if (pmo)
            pmo->finish();
    }

    pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Save");
    TC_LOG_DEBUG("playerbots", "Saving to DB...");
    // Every bot starts with at least 200 gold; the roll by level (10 to 50 gold per level) stays on top of it.
    bot->SetMoney(std::max<uint32>(200 * 10000, urand(level * 100000, level * 5 * 100000)));
    bot->SetHealth(bot->GetMaxHealth());
    bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));
    bot->SaveToDB(false);
    TC_LOG_DEBUG("playerbots", "Initialization Done.");
    if (pmo)
        pmo->finish();
}

void PlayerbotFactory::Refresh()
{
    // Prepare();
    // if (!sPlayerbotAIConfig.EquipAndSpecPersistence ||
    //     bot->GetLevel() < sPlayerbotAIConfig.EquipAndSpecPersistenceLevel)
    // {
    //     InitEquipment(true);
    // }
    InitAttunementQuests();
    ClearInventory();
    InitAmmo();
    InitFood();
    InitReagents();
    InitConsumables();
    InitPotions();
    InitPet();
    InitPetTalents();
    InitSkills();
    InitClassSpells();
    InitRacials();
    InitAvailableSpells();
    InitReputation();
    InitSpecialSpells();
    InitMounts();
    // Every spell-learning step is done by now; spell learning may have rewritten skill 762.
    InitRiding();
    InitKeyring();
    if (!sPlayerbotAIConfig.EquipAndSpecPersistence ||
        bot->GetLevel() < sPlayerbotAIConfig.EquipAndSpecPersistenceLevel)
    {
        InitTalentsTree(true, true, true);
    }
    if (bot->GetLevel() >= sPlayerbotAIConfig.MinEnchantingBotLevel)
        ApplyEnchants();
    bot->DurabilityRepairAll(false, 1.0f, false);
    if (bot->isDead())
        bot->ResurrectPlayer(1.0f, false);
    uint32 money = urand(level * 1000, level * 5 * 1000);
    if (bot->GetMoney() < money)
        bot->SetMoney(money);
    // bot->SaveToDB(false);
}

void PlayerbotFactory::InitConsumables()
{
    uint8 specTab = AiFactory::GetPlayerSpecTab(bot);
    std::vector<std::pair<uint32, uint32>> items;

    switch (bot->GetClass())
    {
        case CLASS_PRIEST:
        {
            if (specTab == PRIEST_TAB_SHADOW)
            {
                std::vector<uint32> wizard_oils = {
                    BRILLIANT_WIZARD_OIL, WIZARD_OIL, LESSER_WIZARD_OIL, MINOR_WIZARD_OIL };
                for (uint32 itemId : wizard_oils)
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                        continue;
                    items.push_back({itemId, 2});
                    break;
                }
            }
            else
            {
                std::vector<uint32> mana_oils = {
                    BRILLIANT_MANA_OIL, LESSER_MANA_OIL, MINOR_MANA_OIL };
                for (uint32 itemId : mana_oils)
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                        continue;
                    items.push_back({itemId, 2});
                    break;
                }
            }
            break;
        }
        case CLASS_MAGE:
        {
            std::vector<uint32> wizard_oils = {
                BRILLIANT_WIZARD_OIL, WIZARD_OIL, LESSER_WIZARD_OIL, MINOR_WIZARD_OIL };
            for (uint32 itemId : wizard_oils)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                    continue;
                items.push_back({itemId, 2});
                break;
            }
            break;
        }
        case CLASS_DRUID:
        {
            if (specTab == DRUID_TAB_BALANCE)
            {
                std::vector<uint32> wizard_oils = {
                    BRILLIANT_WIZARD_OIL, WIZARD_OIL, LESSER_WIZARD_OIL, MINOR_WIZARD_OIL };
                for (uint32 itemId : wizard_oils)
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                        continue;
                    items.push_back({itemId, 2});
                    break;
                }
            }
            else if (specTab == DRUID_TAB_FERAL)
            {
                std::vector<uint32> sharpening_stones = {
                    ELEMENTAL_SHARPENING_STONE, DENSE_SHARPENING_STONE, SOLID_SHARPENING_STONE,
                    HEAVY_SHARPENING_STONE, COARSE_SHARPENING_STONE, ROUGH_SHARPENING_STONE };
                std::vector<uint32> weightstones = {
                    DENSE_WEIGHTSTONE, SOLID_WEIGHTSTONE,
                    HEAVY_WEIGHTSTONE, COARSE_WEIGHTSTONE, ROUGH_WEIGHTSTONE };
                for (uint32 itemId : sharpening_stones)
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                        continue;
                    items.push_back({itemId, 20});
                    break;
                }
                for (uint32 itemId : weightstones)
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                        continue;
                    items.push_back({itemId, 20});
                    break;
                }
            }
            else
            {
                std::vector<uint32> mana_oils = {
                    BRILLIANT_MANA_OIL, LESSER_MANA_OIL, MINOR_MANA_OIL };
                for (uint32 itemId : mana_oils)
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                        continue;
                    items.push_back({itemId, 2});
                    break;
                }
            }
            break;
        }
        case CLASS_PALADIN:
        {
            if (specTab == PALADIN_TAB_HOLY)
            {
                std::vector<uint32> mana_oils = {
                    BRILLIANT_MANA_OIL, LESSER_MANA_OIL, MINOR_MANA_OIL };
                for (uint32 itemId : mana_oils)
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                        continue;
                    items.push_back({itemId, 2});
                    break;
                }
            }
            else
            {
                std::vector<uint32> sharpening_stones = {
                    ELEMENTAL_SHARPENING_STONE, DENSE_SHARPENING_STONE, SOLID_SHARPENING_STONE,
                    HEAVY_SHARPENING_STONE, COARSE_SHARPENING_STONE, ROUGH_SHARPENING_STONE };
                std::vector<uint32> weightstones = {
                    DENSE_WEIGHTSTONE, SOLID_WEIGHTSTONE,
                    HEAVY_WEIGHTSTONE, COARSE_WEIGHTSTONE, ROUGH_WEIGHTSTONE };
                for (uint32 itemId : sharpening_stones)
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                        continue;
                    items.push_back({itemId, 20});
                    break;
                }
                for (uint32 itemId : weightstones)
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                        continue;
                    items.push_back({itemId, 20});
                    break;
                }
            }
            break;
        }
        case CLASS_WARRIOR:
        case CLASS_HUNTER:
        {
            std::vector<uint32> sharpening_stones = {
                ELEMENTAL_SHARPENING_STONE, DENSE_SHARPENING_STONE, SOLID_SHARPENING_STONE,
                HEAVY_SHARPENING_STONE, COARSE_SHARPENING_STONE, ROUGH_SHARPENING_STONE };
            std::vector<uint32> weightstones = {
                DENSE_WEIGHTSTONE, SOLID_WEIGHTSTONE,
                HEAVY_WEIGHTSTONE, COARSE_WEIGHTSTONE, ROUGH_WEIGHTSTONE };
            for (uint32 itemId : sharpening_stones)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                    continue;
                items.push_back({itemId, 20});
                break;
            }
            for (uint32 itemId : weightstones)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto || proto->GetBaseRequiredLevel() > level || level > 75)
                    continue;
                items.push_back({itemId, 20});
                break;
            }
            break;
        }
        case CLASS_ROGUE:
        {
            std::vector<uint32> instant_poisons = {
                INSTANT_POISON_VI, INSTANT_POISON_V,
                INSTANT_POISON_IV, INSTANT_POISON_III, INSTANT_POISON_II, INSTANT_POISON };
            std::vector<uint32> deadly_poisons = {
                DEADLY_POISON_V,
                DEADLY_POISON_IV, DEADLY_POISON_III, DEADLY_POISON_II, DEADLY_POISON };
            for (uint32 itemId : deadly_poisons)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto || proto->GetBaseRequiredLevel() > level)
                    continue;
                items.push_back({itemId, 20});
                break;
            }
            for (uint32 itemId : instant_poisons)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto || proto->GetBaseRequiredLevel() > level)
                    continue;
                items.push_back({itemId, 20});
                break;
            }
            break;
        }
        default:
            break;
    }

    for (std::pair<uint32, uint32> item : items)
    {
        int count = (int)item.second - (int)bot->GetItemCount(item.first);
        if (count > 0)
            StoreItem(item.first, count);
    }
}


void PlayerbotFactory::InitPetTalents()
{
    // Pet talent trees are not part of the 1.6x ruleset.
}

void PlayerbotFactory::InitPet()
{
    Pet* pet = bot->GetPet();

    if (!pet && bot->GetPetStable() && bot->GetPetStable()->GetCurrentPet())
        return;

    if (!pet)
    {
        if (bot->GetClass() != CLASS_HUNTER || bot->GetLevel() < 10)
            return;

        Map* map = bot->GetMap();
        if (!map)
            return;

        std::vector<uint32> ids;

        CreatureTemplateContainer const& creatures = sObjectMgr->GetCreatureTemplates();

        for (CreatureTemplateContainer::const_iterator itr = creatures.begin(); itr != creatures.end(); ++itr)
        {
            if (!itr->second.IsTameable(bot->CanTameExoticPets(), itr->second.GetDifficulty(DIFFICULTY_NONE)))
                continue;

            if (!HasCreatureSpawnRow(itr->first))
                continue;

            if (PlayerbotCreature::GetClassicLevelRange(&itr->second).second > bot->GetLevel())
                continue;

            bool onlyWolf = sPlayerbotAIConfig.HunterWolfPet == 2 ||
                            (sPlayerbotAIConfig.HunterWolfPet == 1 &&
                             bot->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
            // Wolf only (for higher dps)
            if (onlyWolf && itr->second.family != CREATURE_FAMILY_WOLF)
                continue;

            // Exclude configured pet families
            if (std::find(sPlayerbotAIConfig.ExcludedHunterPetFamilies.begin(),
                          sPlayerbotAIConfig.ExcludedHunterPetFamilies.end(),
                          itr->second.family) != sPlayerbotAIConfig.ExcludedHunterPetFamilies.end())
                continue;

            ids.push_back(itr->first);
        }

        if (ids.empty())
        {
            TC_LOG_ERROR("playerbots", "No pets available for bot {} ({} level)", bot->GetName().c_str(), bot->GetLevel());
            return;
        }

        for (uint32 i = 0; i < 10; i++)
        {
            uint32 index = urand(0, ids.size() - 1);
            CreatureTemplate const* co = sObjectMgr->GetCreatureTemplate(ids[index]);
            if (!co)
                continue;
            if (co->Name.size() > 21)
                continue;
            if (bot->GetPetStable() && bot->GetPetStable()->GetCurrentPet())
            {
                auto petGuid = bot->GetPetStable()->GetCurrentPet(); // To correct the build warnin in VS
                // bot->GetPetStable()->GetCurrentPet();
                // bot->GetPetStable()->GetCurrentPet().reset();
                bot->RemovePet(nullptr, PET_SAVE_AS_CURRENT);
                bot->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT);
            }
            if (bot->GetPetStable() && !bot->GetPetStable()->UnslottedPets.empty())
            {
                bot->GetPetStable()->UnslottedPets.clear();
                bot->RemovePet(nullptr, PET_SAVE_AS_CURRENT);
                bot->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT);
            }
            // }
            pet = bot->CreateTamedPetFrom(co->Entry, 0);
            if (!pet)
            {
                continue;
            }

            // prepare visual effect for levelup
            pet->SetLevel(bot->GetLevel() - 1);

            // add to world
            pet->GetMap()->AddToMap(pet->ToCreature());

            // visual effect for levelup
            pet->SetLevel(bot->GetLevel());

            // caster have pet now
            bot->SetMinion(pet, true);

            // 1.60 pets have no talent trees.

            pet->SavePetToDB(PET_SAVE_AS_CURRENT);
            bot->PetSpellInitialize();
            break;
        }
    }

    if (pet)
    {
        pet->InitStatsForLevel(bot->GetLevel());
        pet->SetLevel(bot->GetLevel());
        pet->SetPower(POWER_HAPPINESS, pet->GetMaxPower(Powers(POWER_HAPPINESS)));
        pet->SetHealth(pet->GetMaxHealth());
    }
    else
    {
        TC_LOG_ERROR("playerbots", "Cannot create pet for bot {}", bot->GetName().c_str());
        return;
    }

    // TC_LOG_INFO("playerbots", "Start make spell auto cast for {} spells. {} already auto casted.", pet->m_spells.size(),
    // pet->GetPetAutoSpellSize());
    for (PetSpellMap::const_iterator itr = pet->m_spells.begin(); itr != pet->m_spells.end(); ++itr)
    {
        if (itr->second.state == PETSPELL_REMOVED)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itr->first, DIFFICULTY_NONE);
        if (!spellInfo)
            continue;

        if (spellInfo->IsPassive())
        {
            continue;
        }
        pet->ToggleAutocast(spellInfo, true);
    }
}

void PlayerbotFactory::ClearSkills()
{
    for (uint32 i = 0; i < sizeof(tradeSkills) / sizeof(uint32); ++i)
    {
        bot->SetSkill(tradeSkills[i], 0, 0, 0);
    }

    // unlearn default race/class skills
    if (PlayerInfo const* info = sObjectMgr->GetPlayerInfo(bot->GetRace(), bot->GetClass()))
    {
        for (PlayerCreateInfoSkills::const_iterator itr = info->skills.begin(); itr != info->skills.end(); ++itr)
        {
            uint32 skillId = (*itr)->SkillID;
            if (!bot->HasSkill(skillId))
                continue;
            bot->SetSkill(skillId, 0, 0, 0);
        }
    }
}

void PlayerbotFactory::ClearEverything()
{
    bot->GiveLevel(sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL));
    bot->SetXP(0);
    TC_LOG_INFO("playerbots", "Resetting player...");
    bot->ResetTalents(true);
    ClearSkills();
    ClearSpells();
    ClearInventory();
    ResetQuests();
    // bot->SaveToDB(false);
}

void PlayerbotFactory::ClearSpells()
{
    std::list<uint32> spells;
    for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;
        //const SpellInfo* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE); //not used, line marked for removal.
        if (itr->second.state == PLAYERSPELL_REMOVED)
        {
            continue;
        }

        spells.push_back(spellId);
    }

    for (std::list<uint32>::iterator i = spells.begin(); i != spells.end(); ++i)
    {
        bot->RemoveSpell(*i, false, false);
    }
}

void PlayerbotFactory::ResetQuests()
{
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        bot->SetQuestSlot(slot, 0);
    }
    ObjectMgr::QuestContainer const& questTemplates = sObjectMgr->GetQuestTemplates();
    for (ObjectMgr::QuestContainer::const_iterator i = questTemplates.begin(); i != questTemplates.end(); ++i)
    {
        Quest const* quest = i->second.get();

        uint32 entry = quest->GetQuestId();
        if (bot->GetQuestStatus(entry) == QUEST_STATUS_NONE)
            continue;

        bot->RemoveRewardedQuest(entry);
        bot->RemoveActiveQuest(entry, false);

    }
}

uint32 PlayerbotFactory::InitTalentsTree(bool increment, bool /*use_template*/, bool reset)
{
    // 1.6x talents are trait configs: pick one of the class's classic talent builds and apply it
    // through the ported V2 walk (Api/ClassicTalentsApply). force = true: the factory batches bots
    // at creation and must place ranks now (the walk itself stays budget-bounded).
    namespace Classic = ::Playerbot::Classic;

    // The specs the class has a build for (Forever keeps three classic tabs per class).
    std::vector<uint32> specs;
    for (Classic::TalentBuild const& build : Classic::GetTalentBuilds())
        if (build.cls == bot->GetClass() &&
            std::find(specs.begin(), specs.end(), build.spec) == specs.end())
            specs.push_back(build.spec);

    if (specs.empty())
        return 0;

    // Keep (or pick) the spec: an owner-pinned override wins, then the stored specNo survives an
    // incremental pass, otherwise roll one and pin it so EffectiveSpecId follows.
    uint32 spec = Classic::GetSpecOverride(bot->GetGUID());
    if (!spec)
    {
        uint32 const specNo = sRandomPlayerbotMgr.GetValue(bot, "specNo");
        if (increment && !reset && specNo >= 1 && specNo <= specs.size())
            spec = specs[specNo - 1];
        else
            spec = specs[urand(0, specs.size() - 1)];
        Classic::SetSpecOverride(bot, spec);
    }

    uint32 specIndex = 0;
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i] == spec)
        {
            specIndex = uint32(i);
            break;
        }

    (void)Classic::ApplyClassicTalents(bot, /*force*/ true);
    return specIndex;
}





bool PlayerbotFactory::CanEquipArmor(ItemTemplate const* proto)
{
    if (proto->GetSubClass() == ITEM_SUBCLASS_ARMOR_PLATE && !bot->HasSkill(SKILL_PLATE_MAIL))
    {
        return false;
    }
    if (proto->GetSubClass() == ITEM_SUBCLASS_ARMOR_MAIL && !bot->HasSkill(SKILL_MAIL))
    {
        return false;
    }
    if (proto->GetSubClass() == ITEM_SUBCLASS_ARMOR_LEATHER && !bot->HasSkill(SKILL_LEATHER))
    {
        return false;
    }
    if (proto->GetSubClass() == ITEM_SUBCLASS_ARMOR_CLOTH && !bot->HasSkill(SKILL_CLOTH))
    {
        return false;
    }
    if (proto->GetSubClass() == ITEM_SUBCLASS_ARMOR_SHIELD && !bot->HasSkill(SKILL_SHIELD))
    {
        return false;
    }

    return true;
    // for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
    // {
    //     if (slot == EQUIPMENT_SLOT_TABARD || slot == EQUIPMENT_SLOT_BODY)
    //         continue;

    //     if (slot == EQUIPMENT_SLOT_OFFHAND && bot->GetClass() == CLASS_ROGUE && proto->GetClass() != ITEM_CLASS_WEAPON)
    //         continue;

    //     if (slot == EQUIPMENT_SLOT_OFFHAND && bot->GetClass() == CLASS_PALADIN && proto->GetSubClass() !=
    //     ITEM_SUBCLASS_ARMOR_SHIELD)
    //         continue;
    // }

    // uint8 sp = 0;
    // uint8 ap = 0;
    // uint8 tank = 0;
    // for (uint8 j = 0; j < MAX_ITEM_PROTO_STATS; ++j)
    // {
    //     // for ItemStatValue != 0
    //     if (!proto->ItemStat[j].ItemStatValue)
    //         continue;

    //     AddItemStats(proto->ItemStat[j].ItemStatType, sp, ap, tank);
    // }

    // return CheckItemStats(sp, ap, tank);
}

bool PlayerbotFactory::CheckItemStats(uint8 sp, uint8 ap, uint8 tank)
{
    switch (bot->GetClass())
    {
        case CLASS_PRIEST:
        case CLASS_MAGE:
        case CLASS_WARLOCK:
            if (!sp || ap > sp || tank > sp)
                return false;
            break;
        case CLASS_PALADIN:
        case CLASS_WARRIOR:
            if ((!ap && !tank) || sp > ap || sp > tank)
                return false;
            break;
        case CLASS_HUNTER:
        case CLASS_ROGUE:
            if (!ap || sp > ap || sp > tank)
                return false;
            break;
    }

    return sp || ap || tank;
}

void PlayerbotFactory::AddItemStats(uint32 mod, uint8& sp, uint8& ap, uint8& tank)
{
    switch (mod)
    {
        case ITEM_MOD_HEALTH:
        case ITEM_MOD_STAMINA:
        case ITEM_MOD_MANA:
        case ITEM_MOD_INTELLECT:
        case ITEM_MOD_SPIRIT:
            ++sp;
            break;
    }

    switch (mod)
    {
        case ITEM_MOD_AGILITY:
        case ITEM_MOD_STRENGTH:
        case ITEM_MOD_HEALTH:
        case ITEM_MOD_STAMINA:
            ++tank;
            break;
    }

    switch (mod)
    {
        case ITEM_MOD_HEALTH:
        case ITEM_MOD_STAMINA:
        case ITEM_MOD_AGILITY:
        case ITEM_MOD_STRENGTH:
            ++ap;
            break;
    }
}

bool PlayerbotFactory::CanEquipWeapon(ItemTemplate const* proto)
{
    switch (bot->GetClass())
    {
        case CLASS_PRIEST:
            if (proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_STAFF && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_WAND &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_DAGGER)
                return false;
            break;
        case CLASS_MAGE:
        case CLASS_WARLOCK:
            if (proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_STAFF && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_WAND &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_DAGGER && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_SWORD)
                return false;
            break;
        case CLASS_WARRIOR:
            if (proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE2 && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_POLEARM && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_SWORD &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_GUN && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_BOW && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_THROWN &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE2 && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_FIST_WEAPON &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_DAGGER && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_STAFF)
                return false;
            break;
        case CLASS_PALADIN:
            if (proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE2 && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_POLEARM &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_SWORD2 && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE2 &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_SWORD)
                return false;
            break;
        case CLASS_SHAMAN:
            if (proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_FIST_WEAPON && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE2 && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_DAGGER &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_STAFF)
                return false;
            break;
        case CLASS_DRUID:
            if (proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_DAGGER && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_STAFF &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_POLEARM)
                return false;
            break;
        case CLASS_HUNTER:
            if (proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_DAGGER && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_BOW &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE2 && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_SWORD2 && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_SWORD &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_FIST_WEAPON && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_CROSSBOW && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_STAFF &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_POLEARM)
                return false;
            break;
        case CLASS_ROGUE:
            if (proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_DAGGER && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_SWORD &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_FIST_WEAPON && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_GUN && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_BOW && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_THROWN &&
                proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE)
                return false;
            break;
    }

    return true;
}

bool PlayerbotFactory::CanEquipItem(ItemTemplate const* proto)
{
    if (proto->GetDuration() != 0)
        return false;

    if (proto->GetBonding() == BIND_QUEST /*|| proto->GetBonding() == BIND_ON_USE*/)
        return false;

    if (proto->GetClass() == ITEM_CLASS_CONTAINER)
        return true;

    uint32 requiredLevel = proto->GetBaseRequiredLevel();
    // disable since bad performance
    bool hasItem = bot->HasItemCount(proto->GetId(), 1, false);
    // bot->GetItemCount()
    // !requiredLevel -> it's a quest reward item
    if (!requiredLevel && hasItem)
        return false;

    uint32 level = bot->GetLevel();

    if (requiredLevel > level)
        return false;

    return true;
}

void Shuffle(std::vector<uint32>& items)
{
    uint32 count = items.size();
    for (uint32 i = 0; i < count * 5; i++)
    {
        int i1 = urand(0, count - 1);
        int i2 = urand(0, count - 1);

        uint32 item = items[i1];
        items[i1] = items[i2];
        items[i2] = item;
    }
}

// void PlayerbotFactory::InitEquipmentNew(bool incremental)
// {
//     if (incremental)
//     {
//         DestroyItemsVisitor visitor(bot);
//         IterateItems(&visitor, (IterateItemsMask)(ITERATE_ITEMS_IN_BAGS | ITERATE_ITEMS_IN_BANK));
//     }
//     else
//     {
//     DestroyItemsVisitor visitor(bot);
//     IterateItems(&visitor, ITERATE_ALL_ITEMS);
//     }

//     std::string const& specName = AiFactory::GetPlayerSpecName(bot);
//     if (specName.empty())
//         return;

//     // look for upgrades
//     for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
//     {
//         if (slot == EQUIPMENT_SLOT_TABARD && !bot->GetGuildId())
//             continue;

//         bool isUpgrade = false;
//         bool found = false;
//         bool noItem = false;
//         uint32 quality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_EPIC);
//         uint32 attempts = 10;
//         if (urand(0, 100) < 100 * sPlayerbotAIConfig.RandomGearLoweringChance && quality > ITEM_QUALITY_NORMAL)
//         {
//             quality--;
//         }
//         // current item;
//         Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
//         if (oldItem)
//             isUpgrade = true;

//         uint32 itemInSlot = isUpgrade ? oldItem->GetTemplate()->GetId() : 0;

//         uint32 maxLevel = sPlayerbotAIConfig.RandomBotMaxLevel;
//         if (maxLevel > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
//             maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

//         uint32 minLevel = sPlayerbotAIConfig.RandomBotMinLevel;
//         if (minLevel < sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL))
//             minLevel = sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL);

//         // test
//         do
//         {
//             if (isUpgrade)
//             {
//                 std::vector<uint32> ids = sRandomItemMgr.GetUpgradeList(bot, specName, slot, 0, itemInSlot);
//                 if (!ids.empty())
//                     Shuffle(ids);

//                 for (uint32 index = 0; index < ids.size(); ++index)
//                 {
//                     uint32 newItemId = ids[index];
//                     if (incremental && !IsDesiredReplacement(oldItem))
//                     {
//                         continue;
//                     }

//                     uint16 dest;
//                     if (!CanEquipUnseenItem(slot, dest, newItemId))
//                         continue;

//                     if (oldItem)
//                     {
//                         bot->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
//                         oldItem->DestroyForPlayer(bot);
//                     }

//                     Item* newItem = bot->EquipNewItem(dest, newItemId, ItemContext::NONE, true);
//                     if (newItem)
//                     {
//                         newItem->AddToWorld();
//         //                         bot->AutoUnequipOffhandIfNeed();
//                         newItem->SetOwnerGUID(bot->GetGUID());
//                         EnchantItem(newItem);
//                         TC_LOG_INFO("playerbots", "Bot {} {}:{} <{}>: Equip: {}, slot: {}, Old item: {}",
//                             bot->GetGUID().ToString().c_str(), IsAlliance(bot->GetRace()) ? "A" : "H",
//                             bot->GetLevel(), bot->GetName(), newItemId, slot, itemInSlot);
//                         found = true;
//                         break;
//                     }
//                 }
//             }
//             else
//             {
//                 std::vector<uint32> ids = sRandomItemMgr.GetUpgradeList(bot, specName, slot, quality, itemInSlot);
//                 if (!ids.empty())
//                     Shuffle(ids);

//                 for (uint32 index = 0; index < ids.size(); ++index)
//                 {
//                     uint32 newItemId = ids[index];
//                     uint16 dest;
//                     if (!CanEquipUnseenItem(slot, dest, newItemId))
//                         continue;

//                     Item* newItem = bot->EquipNewItem(dest, newItemId, ItemContext::NONE, true);
//                     if (newItem)
//                     {
//                         bot->AutoUnequipOffhandIfNeed();
//                         newItem->SetOwnerGUID(bot->GetGUID());
//                         EnchantItem(newItem);
//                         found = true;
//                         TC_LOG_INFO("playerbots", "Bot {} {}:{} <{}>: Equip: {}, slot: {}",
//                             bot->GetGUID().ToString().c_str(), IsAlliance(bot->GetRace()) ? "A" : "H",
//                             bot->GetLevel(), bot->GetName(), newItemId, slot);
//                         break;
//                     }
//                 }
//             }
//             quality--;
//         } while (!found && quality != ITEM_QUALITY_POOR);
//         if (!found)
//         {
//             TC_LOG_INFO("playerbots", "Bot {} {}:{} <{}>: no item for slot {}",
//                 bot->GetGUID().ToString().c_str(), IsAlliance(bot->GetRace()) ? "A" : "H", bot->GetLevel(),
//                 bot->GetName(), slot);
//             continue;
//         }
//     }
// }

void PlayerbotFactory::InitEquipment(bool incremental, bool second_chance)
{
    if (level < 5)
    {
        // 12.x has no CharStartOutfit table: starting gear comes from the playercreateinfo
        // item rows that Player::Create already applied.
        return;
    }

    std::unordered_map<uint8, std::vector<std::pair<uint32, int32>>> items;
    // int tab = AiFactory::GetPlayerSpecTab(bot);

    uint32 blevel = bot->GetLevel();
    int32 delta = std::min(blevel, 10u);

    bool isPvp = sRandomPlayerbotMgr.IsSpecPvp(bot->GetGUID().GetCounter(), bot->GetClass());

    StatsWeightCalculator calculator(bot);
    if (isPvp)
        calculator.SetPvpSpec(true);

    // Pre-select CC-break trinket for PvP specs: best available by item level
    // that the bot meets the level requirement for.
    // Humans (Every Man for Himself) and Undead (Will of the Forsaken) have a
    // racial that shares the PvP trinket cooldown, so they don't need one.
    bool racialHasCcBreak = (bot->GetRace() == RACE_HUMAN || bot->GetRace() == RACE_UNDEAD_PLAYER);
    uint32 pvpTrinket1 = 0;
    if (isPvp && level >= 50 && !racialHasCcBreak)
    {
        for (uint32 itemId : ccBreakTrinketCache)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto) continue;
            // Respect gear quality limit: trinket must not exceed itemQuality setting
            if (static_cast<int32>(proto->GetQuality()) > static_cast<int32>(itemQuality)) continue;
            if (!proto || proto->GetBaseRequiredLevel() > level) continue;
            if (!CanEquipItem(proto)) continue;
            uint16 dest;
            if (!CanEquipUnseenItem(EQUIPMENT_SLOT_TRINKET1, dest, itemId)) continue;
            pvpTrinket1 = itemId;
            break;
        }
    }

    for (int32 slot : initSlotsOrder)
    {
        if (slot == EQUIPMENT_SLOT_TABARD || slot == EQUIPMENT_SLOT_BODY)
            continue;

        if (level < 50 && (slot == EQUIPMENT_SLOT_TRINKET1 || slot == EQUIPMENT_SLOT_TRINKET2))
            continue;

        if (level < 30 && (slot == EQUIPMENT_SLOT_NECK || slot == EQUIPMENT_SLOT_HEAD))
            continue;

        if (level < 20 && (slot == EQUIPMENT_SLOT_FINGER1 || slot == EQUIPMENT_SLOT_FINGER2))
            continue;

        if (level < 5 && (slot != EQUIPMENT_SLOT_MAINHAND) && (slot != EQUIPMENT_SLOT_OFFHAND) &&
            (slot != EQUIPMENT_SLOT_FEET) && (slot != EQUIPMENT_SLOT_LEGS) && (slot != EQUIPMENT_SLOT_CHEST) &&
            (slot != EQUIPMENT_SLOT_RANGED))
            continue;

        // Exclude resilience weighting for trinkets
        bool isTrinketSlot = (slot == EQUIPMENT_SLOT_TRINKET1 || slot == EQUIPMENT_SLOT_TRINKET2);
        calculator.SetExcludeResilience(isTrinketSlot);

        Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

        if (second_chance && oldItem)
        {
            bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
        }

        oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

        // PvP specs: force TRINKET1 to the best available CC-break trinket.
        if (slot == EQUIPMENT_SLOT_TRINKET1 && pvpTrinket1 != 0)
        {
            if (oldItem)
            {
                uint8 bagIndex = oldItem->GetBagSlot();
                uint8 oldSlot  = oldItem->GetSlot();
                uint8 dstBag   = NULL_BAG;
                WorldPackets::Item::AutoStoreBagItem packet(WorldPacket(CMSG_AUTO_STORE_BAG_ITEM, 0));
                packet.ContainerSlotA = bagIndex;
                packet.SlotA = oldSlot;
                packet.ContainerSlotB = dstBag;
                bot->GetSession()->HandleAutoStoreBagItemOpcode(packet);
                oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (oldItem) continue;
            }
            uint16 dest;
            if (CanEquipUnseenItem(slot, dest, pvpTrinket1))
                bot->EquipNewItem(dest, pvpTrinket1, ItemContext::NONE, true);
            continue;
        }

        int32 desiredQuality = itemQuality;
        if (urand(0, 100) < 100 * sPlayerbotAIConfig.RandomGearLoweringChance && desiredQuality > ITEM_QUALITY_NORMAL)
            desiredQuality--;

        uint32 const windowFloor = uint32(std::max((int32)bot->GetLevel() - delta, 0));
        uint32 levelTop = bot->GetLevel();
        uint32 levelFloor = windowFloor;
        int32 const topQuality = desiredQuality;
        bool belowWindow = false;
        bool foundWearable = false;  // a candidate under the gear score limit passes the equip checks below
        // Nothing fits next to a two-hander, so searching on would only cost time.
        // Classic has no Titan's Grip: an equipped two-hander always blocks the offhand.
        bool const offhandBlocked = slot == EQUIPMENT_SLOT_OFFHAND && bot->IsTwoHandUsed();

        auto hasWearableCandidate = [&]() -> bool
        {
            for (auto const& candidate : items[slot])
            {
                uint16 dest;
                if (CanEquipItem(sObjectMgr->GetItemTemplate(candidate.first)) &&
                    CanEquipUnseenItem(slot, dest, candidate.first))
                    return true;
            }
            return false;
        };

        // After green: with a gear score limit and nothing green or better under it that the bot can wear in the
        // usual window, search the lower required levels from the top quality again before white and grey.
        auto nextSearchStep = [&]() -> bool
        {
            if (desiredQuality == ITEM_QUALITY_UNCOMMON)
            {
                if (gearScoreLimit != 0 && !offhandBlocked)
                    foundWearable = hasWearableCandidate();
                if (!belowWindow && gearScoreLimit != 0 && windowFloor > 0 && !offhandBlocked && !foundWearable)
                {
                    belowWindow = true;
                    levelTop = windowFloor;
                    levelFloor = 0;
                    desiredQuality = std::min<int32>(topQuality, ITEM_QUALITY_EPIC);  // no legendary in the cache
                    return true;
                }
                levelTop = bot->GetLevel();
                levelFloor = windowFloor;
            }
            return desiredQuality-- > ITEM_QUALITY_POOR;
        };

        do
        {
            // Grey and white skip the limit only as a last resort, when no better quality found anything wearable.
            bool const shouldCheckGS = desiredQuality > ITEM_QUALITY_NORMAL || foundWearable;

            for (uint32 requiredLevel = levelTop; requiredLevel > levelFloor; requiredLevel--)
            {
                for (InventoryType inventoryType : GetPossibleInventoryTypeListBySlot((EquipmentSlots)slot))
                {
                    for (uint32 itemId : sRandomItemMgr.GetEquipmentNew(requiredLevel, inventoryType))
                    {
                        uint32 skipProb = 25;
                        if (urand(1, 100) <= skipProb)
                            continue;

                        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                        // disable next expansion gear
                        if (sPlayerbotAIConfig.LimitGearExpansion && bot->GetLevel() <= 60 && itemId >= 23728)
                            continue;

                        if (sPlayerbotAIConfig.LimitGearExpansion && bot->GetLevel() <= 70 && itemId >= 35570 &&
                            itemId != 36737 && itemId != 37739 &&
                            itemId != 37740)  // transition point from TBC -> WOTLK isn't as clear, and there are other
                                              // wearable TBC items above 35570 but nothing of significance
                            continue;

                        if (!proto)
                            continue;

                        if (shouldCheckGS && gearScoreLimit != 0 &&
                            CalcMixedGearScore(proto->GetBaseItemLevel(), proto->GetQuality()) > gearScoreLimit)
                        {
                            continue;
                        }
                        if (proto->GetClass() != ITEM_CLASS_WEAPON && proto->GetClass() != ITEM_CLASS_ARMOR)
                            continue;

                        if (proto->GetQuality() != uint32(desiredQuality))
                            continue;

                        if (proto->GetClass() == ITEM_CLASS_ARMOR &&
                            (slot == EQUIPMENT_SLOT_HEAD || slot == EQUIPMENT_SLOT_SHOULDERS ||
                             slot == EQUIPMENT_SLOT_CHEST || slot == EQUIPMENT_SLOT_WAIST ||
                             slot == EQUIPMENT_SLOT_LEGS || slot == EQUIPMENT_SLOT_FEET ||
                             slot == EQUIPMENT_SLOT_WRISTS || slot == EQUIPMENT_SLOT_HANDS) &&
                            !CanEquipArmor(proto))
                            continue;

                        if (proto->GetClass() == ITEM_CLASS_WEAPON && !CanEquipWeapon(proto))
                            continue;

                        if (slot == EQUIPMENT_SLOT_OFFHAND && bot->GetClass() == CLASS_ROGUE &&
                            proto->GetClass() != ITEM_CLASS_WEAPON)
                            continue;

                        int32 bestRandomProp = 0;
                        if (false)  // 12.x has no random prefix/suffix fields
                            bestRandomProp = calculator.PickBestRandomPropertyId(itemId);
                        items[slot].push_back({itemId, bestRandomProp});
                    }
                }
            }
        } while (items[slot].size() < 25 && nextSearchStep());

        std::vector<std::pair<uint32, int32>>& ids = items[slot];
        if (ids.empty())
        {
            continue;
        }

        float bestScoreForSlot = -1;
        uint32 bestItemForSlot = 0;
        int32 bestRandomPropForSlot = 0;
        for (size_t index = 0; index < ids.size(); index++)
        {
            uint32 newItemId = ids[index].first;
            int32 newItemProp = ids[index].second;

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(newItemId);

            float cur_score = calculator.CalculateItem(newItemId, newItemProp, slot);

            if (cur_score > 0.0f && proto && proto->GetClass() == ITEM_CLASS_ARMOR && sPlayerbotAIConfig.PreferClassArmorType)
            {
                uint8 preferredArmorType = GetPreferredArmorType(bot->GetClass());
                if (preferredArmorType != 0 && proto->GetSubClass() == preferredArmorType)
                    cur_score *= 3.0f;  // 3x multiplier for preferred armor type
            }

            if (cur_score > bestScoreForSlot)
            {
                // delay heavy check to here
                if (!CanEquipItem(proto))
                    continue;
                uint16 dest;
                if (!CanEquipUnseenItem(slot, dest, newItemId))
                    continue;
                bestScoreForSlot = cur_score;
                bestItemForSlot = newItemId;
                bestRandomPropForSlot = newItemProp;
            }
        }

        if (bestItemForSlot == 0)
        {
            continue;
        }
        uint16 dest;
        if (!CanEquipUnseenItem(slot, dest, bestItemForSlot))
        {
            continue;
        }

        if (incremental && oldItem)
        {
            float old_score = calculator.CalculateItem(oldItem->GetEntry(), oldItem->GetItemRandomBonusListId(), slot);
            if (bestScoreForSlot < 1.2f * old_score)
                continue;
        }
        if (oldItem)
        {
            uint8 bagIndex = oldItem->GetBagSlot();
            uint8 slot = oldItem->GetSlot();
            uint8 dstBag = NULL_BAG;

            // 12.x packet is structured: fill the fields directly (Inv.Items stays empty),
            // reading a hand-built byte stream would overrun the buffer.
            WorldPackets::Item::AutoStoreBagItem packet(WorldPacket(CMSG_AUTO_STORE_BAG_ITEM, 0));
            packet.ContainerSlotA = bagIndex;
            packet.SlotA = slot;
            packet.ContainerSlotB = dstBag;
            bot->GetSession()->HandleAutoStoreBagItemOpcode(packet);
        }

        oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        // fail to store in bag
        if (oldItem)
            continue;

        if (Item* equipped = bot->EquipNewItem(dest, bestItemForSlot, ItemContext::NONE, true))
        {
            if (bestRandomPropForSlot != 0)
            {
                uint8 equipSlot = equipped->GetSlot();
                bot->_ApplyItemMods(equipped, equipSlot, false);
                equipped->SetItemRandomBonusList(bestRandomPropForSlot);
                bot->_ApplyItemMods(equipped, equipSlot, true);
            }
        }
        bot->AutoUnequipOffhandIfNeed();
        // if (newItem)
        // {
        //     newItem->AddToWorld();
        // newItem->AddToUpdateQueueOf(bot);
        // }
    }
    // Secondary init for better equips
    /// @todo: clean up duplicate code
    if (second_chance)
    {
        for (int32 slot : initSlotsOrder)
        {
            if (slot == EQUIPMENT_SLOT_TABARD || slot == EQUIPMENT_SLOT_BODY)
                continue;

            if (level < 50 && (slot == EQUIPMENT_SLOT_TRINKET1 || slot == EQUIPMENT_SLOT_TRINKET2))
                continue;

            if (level < 30 && (slot == EQUIPMENT_SLOT_NECK || slot == EQUIPMENT_SLOT_HEAD))
                continue;

            if (level < 20 && (slot == EQUIPMENT_SLOT_FINGER1 || slot == EQUIPMENT_SLOT_FINGER2))
                continue;

            if (level < 5 && (slot != EQUIPMENT_SLOT_MAINHAND) && (slot != EQUIPMENT_SLOT_OFFHAND) &&
                (slot != EQUIPMENT_SLOT_FEET) && (slot != EQUIPMENT_SLOT_LEGS) && (slot != EQUIPMENT_SLOT_CHEST) &&
                (slot != EQUIPMENT_SLOT_RANGED))
                continue;

            // CC-break trinket was force-equipped in the main pass; leave it alone.
            if (slot == EQUIPMENT_SLOT_TRINKET1 && pvpTrinket1 != 0)
                continue;

            bool isTrinketSlot = (slot == EQUIPMENT_SLOT_TRINKET1 || slot == EQUIPMENT_SLOT_TRINKET2);
            calculator.SetExcludeResilience(isTrinketSlot);

            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);

            std::vector<std::pair<uint32, int32>>& ids = items[slot];
            if (ids.empty())
                continue;

            float bestScoreForSlot = -1;
            uint32 bestItemForSlot = 0;
            int32 bestRandomPropForSlot = 0;
            for (size_t index = 0; index < ids.size(); index++)
            {
                uint32 newItemId = ids[index].first;
                int32 newItemProp = ids[index].second;

                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(newItemId);

                float cur_score = calculator.CalculateItem(newItemId, newItemProp, slot);

                if (cur_score > 0.0f && proto && proto->GetClass() == ITEM_CLASS_ARMOR && sPlayerbotAIConfig.PreferClassArmorType)
                {
                    uint8 preferredArmorType = GetPreferredArmorType(bot->GetClass());
                    if (preferredArmorType != 0 && proto->GetSubClass() == preferredArmorType)
                        cur_score *= 3.0f;  // 3x multiplier for preferred armor type
                }

                if (cur_score > bestScoreForSlot)
                {
                    // delay heavy check to here
                    if (!CanEquipItem(proto))
                        continue;
                    uint16 dest;
                    if (!CanEquipUnseenItem(slot, dest, newItemId))
                        continue;
                    bestScoreForSlot = cur_score;
                    bestItemForSlot = newItemId;
                    bestRandomPropForSlot = newItemProp;
                }
            }

            if (bestItemForSlot == 0)
                continue;

            uint16 dest;
            if (!CanEquipUnseenItem(slot, dest, bestItemForSlot))
                continue;

            if (Item* equipped = bot->EquipNewItem(dest, bestItemForSlot, ItemContext::NONE, true))
            {
                if (bestRandomPropForSlot != 0)
                {
                    uint8 equipSlot = equipped->GetSlot();
                    bot->_ApplyItemMods(equipped, equipSlot, false);
                    equipped->SetItemRandomBonusList(bestRandomPropForSlot);
                    bot->_ApplyItemMods(equipped, equipSlot, true);
                }
            }
            bot->AutoUnequipOffhandIfNeed();
        }
    }
}

bool PlayerbotFactory::IsDesiredReplacement(Item* item)
{
    if (!item)
        return true;

    ItemTemplate const* proto = item->GetTemplate();
    uint32 requiredLevel = proto->GetBaseRequiredLevel();
    if (!requiredLevel)
        return true;

    uint32 delta = 1 + (80 - bot->GetLevel()) / 10;
    return proto->GetQuality() < ITEM_QUALITY_RARE || (bot->GetLevel() - requiredLevel) > delta;
}

inline Item* StoreNewItemInInventorySlot(Player* player, uint32 newItemId, uint32 count)
{
    ItemPosCountVec vDest;
    InventoryResult msg = player->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, vDest, newItemId, count);
    if (msg == EQUIP_ERR_OK)
    {
        if (Item* newItem = player->StoreNewItem(vDest, newItemId, true, GenerateItemRandomBonusListId(newItemId)))
            return newItem;
    }

    return nullptr;
}

// void PlayerbotFactory::InitSecondEquipmentSet()
// {
//     if (bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARLOCK || bot->GetClass() == CLASS_PRIEST)
//         return;

//     std::map<uint32, std::vector<uint32>> items;

//     uint32 desiredQuality = itemQuality;
//     while (urand(0, 100) < 100 * sPlayerbotAIConfig.RandomGearLoweringChance && desiredQuality >
//     ITEM_QUALITY_NORMAL)
//     {
//         desiredQuality--;
//     }

//     ItemTemplateContainer const* itemTemplate = sObjectMgr->GetItemTemplateStore();
//     do
//     {
//         for (auto const& itr : *itemTemplate)
//         {
//             ItemTemplate const* proto = &itr.second;
//             if (!proto)
//                 continue;
//             if (!CanEquipItem(proto, desiredQuality))
//                 continue;

//             if (proto->GetClass() == ITEM_CLASS_WEAPON)
//             {
//                 //if (!CanEquipWeapon(proto))
//                 //    continue;

//                 if (sRandomItemMgr.HasStatWeight(proto->GetId()))
//                 {
//                     if (!sRandomItemMgr.GetLiveStatWeight(bot, proto->GetId()))
//                         continue;
//                 }

//                 Item* existingItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
//                 if (existingItem)
//                 {
//                     switch (existingItem->GetTemplate()->GetSubClass())
//                     {
//                         case ITEM_SUBCLASS_WEAPON_AXE:
//                         case ITEM_SUBCLASS_WEAPON_DAGGER:
//                         case ITEM_SUBCLASS_WEAPON_FIST_WEAPON:
//                         case ITEM_SUBCLASS_WEAPON_MACE:
//                         case ITEM_SUBCLASS_WEAPON_SWORD:
//                             if (proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_AXE || proto->GetSubClass() ==
//                             ITEM_SUBCLASS_WEAPON_DAGGER || proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_FIST_WEAPON ||
//                                 proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_MACE || proto->GetSubClass() ==
//                                 ITEM_SUBCLASS_WEAPON_SWORD) continue;
//                             break;
//                         default:
//                             if (proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_AXE && proto->GetSubClass() !=
//                             ITEM_SUBCLASS_WEAPON_DAGGER && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_FIST_WEAPON &&
//                                 proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_MACE && proto->GetSubClass() !=
//                                 ITEM_SUBCLASS_WEAPON_SWORD) continue;
//                             break;
//                     }
//                 }
//             }
//             else if (proto->GetClass() == ITEM_CLASS_ARMOR && proto->GetSubClass() == ITEM_SUBCLASS_ARMOR_SHIELD)
//             {
//                 //if (!CanEquipArmor(proto))
//                 //    continue;

//                 if (sRandomItemMgr.HasStatWeight(proto->GetId()))
//                 {
//                     if (!sRandomItemMgr.GetLiveStatWeight(bot, proto->GetId()))
//                         continue;
//                 }

//                 if (Item* existingItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
//                     if (existingItem->GetTemplate()->GetSubClass() == ITEM_SUBCLASS_ARMOR_SHIELD)
//                         continue;
//             }
//             else
//                 continue;

//             items[proto->GetClass()].push_back(itr.first);
//         }
//     } while (items[ITEM_CLASS_ARMOR].empty() && items[ITEM_CLASS_WEAPON].empty() && desiredQuality-- >
//     ITEM_QUALITY_NORMAL);

//     for (std::map<uint32, std::vector<uint32>>::iterator i = items.begin(); i != items.end(); ++i)
//     {
//         std::vector<uint32>& ids = i->second;
//         if (ids.empty())
//         {
//             TC_LOG_DEBUG("playerbots",   "{}: no items to make second equipment set for slot {}",
//             bot->GetName().c_str(), i->first); continue;
//         }

//         for (uint32 attempts = 0; attempts < 15; attempts++)
//         {
//             uint32 index = urand(0, ids.size() - 1);
//             uint32 newItemId = ids[index];

//             if (Item* newItem = StoreNewItemInInventorySlot(bot, newItemId, 1))
//             {
//                 newItem->SetOwnerGUID(bot->GetGUID());
//                 EnchantItem(newItem);
//                 newItem->AddToWorld();
// //                 break;
//             }
//         }
//     }
// }

void PlayerbotFactory::InitBags(bool destroyOld)
{
    // Classic 1.60 keeps the four bag slots (Player.h InventorySlots) and the reagent bag slot after them strictly
    // apart: a plain bag never goes into the reagent slot and a reagent container never into a bag slot. A bot gets
    // Bottomless Bag in the four bag slots and Bottomless Reagent Bag in the reagent one (world.armory_item ids).
    // 3.3.5 hardcoded item 51809 (a WotLK bag) the 1.60 store has no entry for.
    uint32 const BAG_ITEM = 14156;
    uint32 const REAGENT_BAG_ITEM = 254139;

    auto const equipBags = [&](uint8 slotBegin, uint8 slotEnd, uint32 itemId)
    {
        for (uint8 slot = slotBegin; slot < slotEnd; ++slot)
        {
            Item* oldBag = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (oldBag && oldBag->GetTemplate()->GetId() == itemId)
                continue;

            uint16 dest;
            if (!CanEquipUnseenItem(slot, dest, itemId))
            {
                TC_LOG_DEBUG("playerbots", "{}: bag item {} does not fit slot {}.", bot->GetName(), itemId, slot);
                continue;
            }

            if (oldBag)
            {
                if (!destroyOld)
                    continue;
                bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
            }

            if (!bot->EquipNewItem(dest, itemId, ItemContext::NONE, true))
                TC_LOG_DEBUG("playerbots", "{}: bag item {} was not stored in slot {}.", bot->GetName(), itemId, slot);
        }
    };

    equipBags(INVENTORY_SLOT_BAG_START, INVENTORY_SLOT_BAG_END, BAG_ITEM);
    equipBags(REAGENT_BAG_SLOT_START, REAGENT_BAG_SLOT_END, REAGENT_BAG_ITEM);
}

void PlayerbotFactory::EnchantItem(Item* item)
{
    if (bot->GetLevel() < sPlayerbotAIConfig.MinEnchantingBotLevel)
        return;

    if (urand(0, 100) < 100 * sPlayerbotAIConfig.RandomGearLoweringChance)
        return;

    ItemTemplate const* proto = item->GetTemplate();
    uint32 itemLevel = proto->GetBaseItemLevel();

    std::vector<uint32> ids;

    // 12.x exposes no "all spells" store; enchant scroll spells come from a skill line.
    for (uint32 abilityIndex = 0; abilityIndex < sSkillLineAbilityStore.GetNumRows(); ++abilityIndex)
    {
        SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(abilityIndex);
        if (!ability)
            continue;
        uint32 id = ability->Spell;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
        if (!spellInfo)
            continue;

        uint32 requiredLevel = spellInfo->BaseLevel;
        if (requiredLevel && (requiredLevel > itemLevel || requiredLevel < itemLevel - 35))
            continue;

        if (spellInfo->MaxLevel && level > spellInfo->MaxLevel)
            continue;

        uint32 spellLevel = spellInfo->SpellLevel;
        if (spellLevel && (spellLevel > level || spellLevel < level - 10))
            continue;

        for (uint8 j = 0; j < spellInfo->GetEffects().size(); ++j)
        {
            if (GetEffectOrDefault(spellInfo, SpellEffIndex(j)).Effect != SPELL_EFFECT_ENCHANT_ITEM)
                continue;

            uint32 enchant_id = GetEffectOrDefault(spellInfo, SpellEffIndex(j)).MiscValue;
            if (!enchant_id)
                continue;

            SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
            if (!enchant)
                continue;

            SpellInfo const* enchantSpell = sSpellMgr->GetSpellInfo(enchant->EffectArg[0], DIFFICULTY_NONE);
            if (!enchantSpell || (enchantSpell->SpellLevel && enchantSpell->SpellLevel > level))
                continue;

            uint8 sp = 0;
            uint8 ap = 0;
            uint8 tank = 0;
            for (uint8 i = 0; i < MAX_ITEM_ENCHANTMENT_EFFECTS; ++i)
            {
                if (enchant->Effect[i] != ITEM_ENCHANTMENT_TYPE_STAT)
                    continue;

                AddItemStats(enchant->EffectArg[i], sp, ap, tank);
            }

            if (!CheckItemStats(sp, ap, tank))
                continue;

            if (enchant->ConditionID && !bot->EnchantmentFitsRequirements(enchant->ConditionID, -1))
                continue;

            if (!item->IsFitToSpellRequirements(spellInfo))
                continue;

            ids.push_back(enchant_id);
        }
    }

    if (ids.empty())
    {
        TC_LOG_DEBUG("playerbots", "{}: no enchantments found for item {}", bot->GetName().c_str(),
                  item->GetTemplate()->GetId());
        return;
    }

    uint32 index = urand(0, ids.size() - 1);
    uint32 id = ids[index];

    SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(id);
    if (!enchant)
        return;

    bot->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
    item->SetEnchantment(PERM_ENCHANTMENT_SLOT, id, 0, 0, bot->GetGUID());
    bot->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, true);
}

bool PlayerbotFactory::CanEquipUnseenItem(uint8 slot, uint16& dest, uint32 item)
{
    dest = 0;

    // Item::CreateItem aborts (ABORT()) when the template is missing; 1.60 lacks many
    // 3.3.5 item ids the ported code still references.
    if (!sObjectMgr->GetItemTemplate(item))
        return false;

    if (Item* pItem = Item::CreateItem(item, 1, ItemContext::NONE))
    {
        InventoryResult result = botAI ? botAI->CanEquipItem(slot, dest, pItem, true, true)
                                       : bot->CanEquipItem(slot, dest, pItem, true, true);
        delete pItem;
        return result == EQUIP_ERR_OK;
    }

    return false;
}

void PlayerbotFactory::InitTradeSkills()
{
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return;

    uint32 const maxPrimaryTradeSkills =
        std::min<uint32>(2, sWorld->getIntConfig(CONFIG_MAX_PRIMARY_TRADE_SKILL));

    uint16 firstSkill = sRandomPlayerbotMgr.GetValue(bot, "firstSkill");
    uint16 secondSkill = sRandomPlayerbotMgr.GetValue(bot, "secondSkill");
    ProfessionRollType professionRollType =
        static_cast<ProfessionRollType>(sRandomPlayerbotMgr.GetValue(bot, "professionRollType"));

    if (professionRollType != ProfessionRollType::Class && professionRollType != ProfessionRollType::Random)
    {
        professionRollType = urand(1, 100) <= sPlayerbotAIConfig.ClassMatchingProfessionChance
                                 ? ProfessionRollType::Class
                                 : ProfessionRollType::Random;
        sRandomPlayerbotMgr.SetValue(bot, "professionRollType", static_cast<uint32>(professionRollType));
    }

    std::vector<WeightedProfessionPair> professionPairs = professionRollType == ProfessionRollType::Class
                                                              ? GetClassProfessionPairs(bot)
                                                              : GetRandomProfessionPairs();

    std::vector<uint16> knownPrimarySkills;
    for (uint32 tradeSkill : tradeSkills)
    {
        if (IsPrimaryTradeSkill(tradeSkill) && bot->HasSkill(tradeSkill))
            knownPrimarySkills.push_back(static_cast<uint16>(tradeSkill));
    }

    std::vector<uint16> primarySkills;
    auto addPrimarySkill = [&primarySkills, maxPrimaryTradeSkills](uint16 skillId)
    {
        if (!skillId || !IsPrimaryTradeSkill(skillId) || primarySkills.size() >= maxPrimaryTradeSkills)
            return;

        if (std::find(primarySkills.begin(), primarySkills.end(), skillId) == primarySkills.end())
            primarySkills.push_back(skillId);
    };

    auto isKnownPrimarySkill = [&knownPrimarySkills](uint16 skillId)
    {
        return std::find(knownPrimarySkills.begin(), knownPrimarySkills.end(), skillId) != knownPrimarySkills.end();
    };

    if (knownPrimarySkills.empty())
    {
        // A full randomization clears skills before this method. Keep a valid stored assignment stable and relearn it.
        addPrimarySkill(firstSkill);
        addPrimarySkill(secondSkill);
    }
    else if (knownPrimarySkills.size() == 1)
    {
        // The real skill is authoritative. Reuse a stored complement only when the stored pair contains that skill.
        uint16 const knownSkill = knownPrimarySkills.front();
        addPrimarySkill(knownSkill);

        if (firstSkill == knownSkill && secondSkill != knownSkill)
            addPrimarySkill(secondSkill);
        else if (secondSkill == knownSkill && firstSkill != knownSkill)
            addPrimarySkill(firstSkill);
    }
    else
    {
        // For contaminated bots, stored values are trusted only when the corresponding skill is actually present.
        if (isKnownPrimarySkill(firstSkill))
            addPrimarySkill(firstSkill);
        if (isKnownPrimarySkill(secondSkill))
            addPrimarySkill(secondSkill);

        for (uint16 skillId : knownPrimarySkills)
            addPrimarySkill(skillId);
    }

    if (primarySkills.empty() && maxPrimaryTradeSkills == 1)
        addPrimarySkill(ChooseSingleProfession(professionPairs));
    else if (primarySkills.empty() && maxPrimaryTradeSkills >= 2)
    {
        std::pair<uint16, uint16> const professionPair = ChooseProfessionPair(professionPairs);
        addPrimarySkill(professionPair.first);
        addPrimarySkill(professionPair.second);
    }
    else if (primarySkills.size() == 1 && maxPrimaryTradeSkills >= 2)
    {
        addPrimarySkill(ChooseComplementaryProfession(professionPairs, primarySkills.front()));

        // Defensive fallback for an empty or malformed pair table.
        if (primarySkills.size() == 1)
        {
            for (uint32 tradeSkill : tradeSkills)
            {
                if (IsPrimaryTradeSkill(tradeSkill) && tradeSkill != primarySkills.front())
                {
                    addPrimarySkill(static_cast<uint16>(tradeSkill));
                    break;
                }
            }
        }
    }

    firstSkill = primarySkills.empty() ? 0 : primarySkills[0];
    secondSkill = primarySkills.size() > 1 ? primarySkills[1] : 0;
    sRandomPlayerbotMgr.SetValue(bot, "firstSkill", firstSkill);
    sRandomPlayerbotMgr.SetValue(bot, "secondSkill", secondSkill);

    for (uint16 skillId : knownPrimarySkills)
    {
        if (std::find(primarySkills.begin(), primarySkills.end(), skillId) == primarySkills.end())
            bot->SetSkill(skillId, 0, 0, 0);
    }

    SetRandomSkill(SKILL_FIRST_AID);
    SetRandomSkill(SKILL_FISHING);
    SetRandomSkill(SKILL_COOKING);

    std::vector<uint16> skillsToLearn = {SKILL_FIRST_AID, SKILL_FISHING, SKILL_COOKING};
    skillsToLearn.insert(skillsToLearn.end(), primarySkills.begin(), primarySkills.end());

    uint32 selectedPrimaryStarterSpells = 0;
    for (uint16 skillId : primarySkills)
    {
        uint32 const starterSpellId = GetProfessionStarterSpell(skillId);
        if (starterSpellId && bot->HasSpell(starterSpellId))
            ++selectedPrimaryStarterSpells;
    }

    bot->SetFreePrimaryProfessions(
        static_cast<uint16>(maxPrimaryTradeSkills > selectedPrimaryStarterSpells
                                ? maxPrimaryTradeSkills - selectedPrimaryStarterSpells
                                : 0));

    for (uint16 skillId : skillsToLearn)
    {
        uint32 const spellId = GetProfessionStarterSpell(skillId);
        if (!spellId || bot->HasSpell(spellId))
            continue;

        if (IsPrimaryTradeSkill(skillId) && !bot->GetFreePrimaryProfessionPoints())
            continue;

        bot->LearnSpell(spellId, false);
    }

    for (uint16 skillId : primarySkills)
        SetRandomSkill(skillId);

    bot->SetFreePrimaryProfessions(
        static_cast<uint16>(maxPrimaryTradeSkills > primarySkills.size()
                                ? maxPrimaryTradeSkills - primarySkills.size()
                                : 0));

    InitTradeSpecializations();
}

void PlayerbotFactory::InitTradeSpecializations()
{
    InitEngineeringSpecialization();
    InitLeatherworkingSpecialization();
    InitBlacksmithingSpecialization();
}

bool PlayerbotFactory::InitEngineeringSpecialization()
{
    if (!bot->HasSkill(SKILL_ENGINEERING) ||
        bot->GetBaseSkillValue(SKILL_ENGINEERING) < 200 ||
        bot->GetLevel() < 30)
        return false;

    if (HasAnySpell(bot, {static_cast<uint32>(ProfessionSpecializationSpell::Goblin),
                          static_cast<uint32>(ProfessionSpecializationSpell::Gnomish)}))
        return false;

    switch (GetStoredOrRandomValue(bot, "engineeringSpecialization", 1, 2))
    {
        case 1:
            return LearnProfessionSpecialization(bot,
                                                 ProfessionSpecializationSpell::Goblin,
                                                 ProfessionSpecializationSpell::LearnGoblin);
        case 2:
        default:
            return LearnProfessionSpecialization(bot,
                                                 ProfessionSpecializationSpell::Gnomish,
                                                 ProfessionSpecializationSpell::LearnGnomish);
    }
}

bool PlayerbotFactory::InitLeatherworkingSpecialization()
{
    if (!bot->HasSkill(SKILL_LEATHERWORKING) ||
        bot->GetBaseSkillValue(SKILL_LEATHERWORKING) < 225 ||
        bot->GetLevel() <= 40)
        return false;

    if (HasAnySpell(bot, {static_cast<uint32>(ProfessionSpecializationSpell::Dragon),
                          static_cast<uint32>(ProfessionSpecializationSpell::Elemental),
                          static_cast<uint32>(ProfessionSpecializationSpell::Tribal)}))
        return false;

    switch (GetStoredOrRandomValue(bot, "leatherSpecialization", 1, 3))
    {
        case 1:
            return LearnProfessionSpecialization(bot,
                                                 ProfessionSpecializationSpell::Dragon,
                                                 ProfessionSpecializationSpell::LearnDragon);
        case 2:
            return LearnProfessionSpecialization(bot,
                                                 ProfessionSpecializationSpell::Elemental,
                                                 ProfessionSpecializationSpell::LearnElemental);
        case 3:
        default:
            return LearnProfessionSpecialization(bot,
                                                 ProfessionSpecializationSpell::Tribal,
                                                 ProfessionSpecializationSpell::LearnTribal);
    }
}

bool PlayerbotFactory::InitBlacksmithingSpecialization()
{
    bool learnedSpecialization = false;

    if (!bot->HasSkill(SKILL_BLACKSMITHING) ||
        bot->GetBaseSkillValue(SKILL_BLACKSMITHING) < 225)
        return false;

    if (!bot->HasSpell(static_cast<uint32>(ProfessionSpecializationSpell::Armor)) &&
        !bot->HasSpell(static_cast<uint32>(ProfessionSpecializationSpell::Weapon)))
    {
        switch (GetStoredOrRandomValue(bot, "blacksmithSpecialization", 1, 2))
        {
            case 1:
                learnedSpecialization = LearnProfessionSpecialization(bot,
                                                                      ProfessionSpecializationSpell::Armor,
                                                                      ProfessionSpecializationSpell::LearnArmor);
                break;
            case 2:
            default:
                learnedSpecialization = LearnProfessionSpecialization(bot,
                                                                      ProfessionSpecializationSpell::Weapon,
                                                                      ProfessionSpecializationSpell::LearnWeapon);
                break;
        }
    }

    if (!bot->HasSpell(static_cast<uint32>(ProfessionSpecializationSpell::Weapon)) ||
        bot->GetBaseSkillValue(SKILL_BLACKSMITHING) < 250 ||
        bot->GetLevel() <= 49 ||
        HasAnySpell(bot, {static_cast<uint32>(ProfessionSpecializationSpell::Hammer),
                          static_cast<uint32>(ProfessionSpecializationSpell::Axe),
                          static_cast<uint32>(ProfessionSpecializationSpell::Sword)}))
        return learnedSpecialization;

    switch (GetStoredOrRandomValue(bot, "blacksmithWeaponSpecialization", 1, 3))
    {
        case 1:
            return LearnProfessionSpecialization(bot,
                                                 ProfessionSpecializationSpell::Hammer,
                                                 ProfessionSpecializationSpell::LearnHammer);
        case 2:
            return LearnProfessionSpecialization(bot,
                                                 ProfessionSpecializationSpell::Axe,
                                                 ProfessionSpecializationSpell::LearnAxe);
        case 3:
        default:
            return LearnProfessionSpecialization(bot,
                                                 ProfessionSpecializationSpell::Sword,
                                                 ProfessionSpecializationSpell::LearnSword);
    }
}

void PlayerbotFactory::UpdateTradeSkills()
{
    for (uint32 i = 0; i < sizeof(tradeSkills) / sizeof(uint32); ++i)
    {
        if (bot->GetSkillValue(tradeSkills[i]) == 1)
            bot->SetSkill(tradeSkills[i], 0, 0, 0);
    }
}

void PlayerbotFactory::InitRiding()
{
    // 1.60 riding is skill 762 with two tiers: Apprentice (75) from level 40, Journeyman (150) from
    // level 60. Run the skill line's own spells first (33388/33391 are learned here exactly as a
    // character picks them up at the riding trainer; Player::AddSpell's learn-skill branch rewrites
    // 762 from the 3.3.5 skill_tiers table while that happens), then write the tier for the level.
    // The second step is what makes the value immune to that spell pass.
    uint32 const ridingSkill = bot->GetLevel() >= 60 ? 150 : bot->GetLevel() >= 40 ? 75 : 0;
    if (ridingSkill)
        bot->LearnSkillRewardedSpells(SKILL_RIDING, ridingSkill, Races(bot->GetRace()));
    bot->SetSkill(SKILL_RIDING, uint16(ridingSkill / 75), ridingSkill, ridingSkill);
}

void PlayerbotFactory::InitSkills()
{
    //uint32 maxValue = level * 5; //not used, line marked for removal.
    bot->UpdateSkillsForLevel();
    InitRiding();

    uint32 skillLevel = bot->GetLevel() < 40 ? 0 : 1;
    uint32 dualWieldLevel = bot->GetLevel() < 20 ? 0 : 1;
    SetRandomSkill(SKILL_DEFENSE);
    SetRandomSkill(SKILL_UNARMED);
    switch (bot->GetClass())
    {
        case CLASS_DRUID:
            SetRandomSkill(SKILL_MACES);
            SetRandomSkill(SKILL_STAVES);
            SetRandomSkill(SKILL_TWO_HANDED_MACES);
            SetRandomSkill(SKILL_DAGGERS);
            SetRandomSkill(SKILL_POLEARMS);
            SetRandomSkill(SKILL_FIST_WEAPONS);
            break;
        case CLASS_WARRIOR:
            SetRandomSkill(SKILL_SWORDS);
            SetRandomSkill(SKILL_AXES);
            SetRandomSkill(SKILL_BOWS);
            SetRandomSkill(SKILL_GUNS);
            SetRandomSkill(SKILL_MACES);
            SetRandomSkill(SKILL_TWO_HANDED_SWORDS);
            SetRandomSkill(SKILL_STAVES);
            SetRandomSkill(SKILL_TWO_HANDED_MACES);
            SetRandomSkill(SKILL_TWO_HANDED_AXES);
            SetRandomSkill(SKILL_DAGGERS);
            SetRandomSkill(SKILL_CROSSBOWS);
            SetRandomSkill(SKILL_POLEARMS);
            SetRandomSkill(SKILL_FIST_WEAPONS);
                        bot->SetSkill(SKILL_DUAL_WIELD, 0, dualWieldLevel, dualWieldLevel);
            bot->SetSkill(SKILL_PLATE_MAIL, 0, skillLevel, skillLevel);
            bot->SetCanDualWield(dualWieldLevel);
            break;
        case CLASS_PALADIN:
            SetRandomSkill(SKILL_SWORDS);
            SetRandomSkill(SKILL_AXES);
            SetRandomSkill(SKILL_MACES);
            SetRandomSkill(SKILL_TWO_HANDED_SWORDS);
            SetRandomSkill(SKILL_TWO_HANDED_MACES);
            SetRandomSkill(SKILL_TWO_HANDED_AXES);
            SetRandomSkill(SKILL_POLEARMS);
            bot->SetSkill(SKILL_PLATE_MAIL, 0, skillLevel, skillLevel);
            break;
        case CLASS_PRIEST:
            SetRandomSkill(SKILL_MACES);
            SetRandomSkill(SKILL_STAVES);
            SetRandomSkill(SKILL_DAGGERS);
            SetRandomSkill(SKILL_WANDS);
            break;
        case CLASS_SHAMAN:
            SetRandomSkill(SKILL_AXES);
            SetRandomSkill(SKILL_MACES);
            SetRandomSkill(SKILL_STAVES);
            SetRandomSkill(SKILL_TWO_HANDED_MACES);
            SetRandomSkill(SKILL_TWO_HANDED_AXES);
            SetRandomSkill(SKILL_DAGGERS);
            SetRandomSkill(SKILL_FIST_WEAPONS);
            bot->SetSkill(SKILL_MAIL, 0, skillLevel, skillLevel);
            break;
        case CLASS_MAGE:
            SetRandomSkill(SKILL_SWORDS);
            SetRandomSkill(SKILL_STAVES);
            SetRandomSkill(SKILL_DAGGERS);
            SetRandomSkill(SKILL_WANDS);
            break;
        case CLASS_WARLOCK:
            SetRandomSkill(SKILL_SWORDS);
            SetRandomSkill(SKILL_STAVES);
            SetRandomSkill(SKILL_DAGGERS);
            SetRandomSkill(SKILL_WANDS);
            break;
        case CLASS_HUNTER:
            SetRandomSkill(SKILL_SWORDS);
            SetRandomSkill(SKILL_AXES);
            SetRandomSkill(SKILL_BOWS);
            SetRandomSkill(SKILL_GUNS);
            SetRandomSkill(SKILL_TWO_HANDED_SWORDS);
            SetRandomSkill(SKILL_STAVES);
            SetRandomSkill(SKILL_TWO_HANDED_AXES);
            SetRandomSkill(SKILL_DAGGERS);
            SetRandomSkill(SKILL_CROSSBOWS);
            SetRandomSkill(SKILL_POLEARMS);
            SetRandomSkill(SKILL_FIST_WEAPONS);
                        bot->SetSkill(SKILL_DUAL_WIELD, 0, dualWieldLevel, dualWieldLevel);
            bot->SetSkill(SKILL_MAIL, 0, skillLevel, skillLevel);
            bot->SetCanDualWield(dualWieldLevel);
            break;
        case CLASS_ROGUE:
            SetRandomSkill(SKILL_SWORDS);
            SetRandomSkill(SKILL_AXES);
            SetRandomSkill(SKILL_BOWS);
            SetRandomSkill(SKILL_GUNS);
            SetRandomSkill(SKILL_MACES);
            SetRandomSkill(SKILL_DAGGERS);
            SetRandomSkill(SKILL_CROSSBOWS);
            SetRandomSkill(SKILL_FIST_WEAPONS);
                        SetRandomSkill(SKILL_LOCKPICKING);
            bot->SetSkill(SKILL_DUAL_WIELD, 0, 1, 1);
            bot->SetCanDualWield(true);
            break;
        default:
            break;
    }

    InitTradeSkills();
    InitInventorySkill();

    // switch (bot->GetClass())
    // {
    //     case CLASS_WARRIOR:
    //     case CLASS_PALADIN:
    //         bot->SetSkill(SKILL_PLATE_MAIL, 0, skillLevel, skillLevel);
    //         break;
    //     case CLASS_SHAMAN:
    //     case CLASS_HUNTER:
    //         bot->SetSkill(SKILL_MAIL, 0, skillLevel, skillLevel);
    //         break;
    //     default:
    //         break;
    // }
}

void PlayerbotFactory::SetRandomSkill(uint16 id)
{
    uint32 maxValue = level * 5;

    // do not let skill go beyond limit even if maxlevel > blizzlike
    // if (level > 60)
    // {
    //     maxValue = (level + 10) * 5;
    // }

    // uint32 value = urand(maxValue - level, maxValue);
    uint32 value = maxValue;
    //uint32 curValue = bot->GetSkillValue(id); //not used, line marked for removal.

    uint16 step = bot->GetSkillValue(id) ? bot->GetSkillStep(id) : 1;

    // if (!bot->HasSkill(id) || value > curValue)
    bot->SetSkill(id, step, value, maxValue);
}

void PlayerbotFactory::InitAvailableSpells()
{
    // 12.x keys the trainer store by world.trainer.Id, not by creature entry (the 3.3.5 layout this
    // code assumed — GetTrainer(creatureEntry) missed every trainer, so bots never got their class
    // spells taught). Walk the store directly instead. On the 1.60 data the class trainers are
    // Type None (27 warrior trainers, ~100 spells each), besides the tradeskill/talent/pet ones.
    if (trainerIdCache[bot->GetClass()].empty())
    {
        for (auto const& [trainerId, trainer] : sObjectMgr->GetTrainers())
        {
            // Class and talent trainers only, the ones that hold a class's spell book: pet trainers
            // hold pet abilities (a hunter ended with 217 spells of growls and aspects), profession
            // trainers hold recipes. Both are taught elsewhere / not at all.
            if (trainer.GetType() != Trainer::Type::None && trainer.GetType() != Trainer::Type::Talent)
                continue;

            if (!(trainer.GetClassMask() & (1u << (bot->GetClass() - 1))))
                continue;

            trainerIdCache[bot->GetClass()].push_back(trainerId);
        }
    }

    // Class spells: teach in rounds until a round adds nothing. A spell whose required ability
    // (ReqAbility) is taught by the same trainer later in its list is refused on the first pass and
    // would stay missing (one class/race came out with 158 or 169 spells depending on the order).
    // Profession recipes have no prerequisite chain and are taught once, so they do not multiply.
    for (uint8 round = 0; round < 3; ++round)
    {
        uint32 const before = uint32(bot->GetSpellMap().size());
        for (uint32 trainerId : trainerIdCache[bot->GetClass()])
        {
            Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(trainerId);
            if (!trainer || trainer->GetType() == Trainer::Type::Tradeskill)
                continue;
            trainer->TeachAvailableSpellsFree(bot);
        }
        if (uint32(bot->GetSpellMap().size()) == before)
            break;
    }
    // Profession recipes are not taught here: the requirement is the class spells of the bot's level
    // (the profession skill itself comes from InitTradeSkills; a recipe is picked up when crafting).
}

void PlayerbotFactory::InitClassSpells()
{
    // Classic 1.60: class spells straight from the client data (SkillLineAbility, loaded from the
    // extracted db2), not a hard-coded 3.3.5 list. Forever's class layouts differ from 3.3.5 (a
    // level-20 warrior made on the official client holds 72/100/285/... and has no 78/2457, both
    // of this file's old list), so the old ids were removed wholesale.
    // The filters mirror Player::LearnSkillRewardedSpells: acquire method with its condition
    // checks, race/class masks, and the spell's own level gate. Trade-skill recipes are skipped
    // (the trainer face, InitAvailableSpells, covers the learned attacks; recipes would only
    // flood the spell book).
    uint32 const classMask = bot->GetClassMask();
    Races const race = Races(bot->GetRace());
    uint32 const level = bot->GetLevel();
    uint32 const familyName = uint32(PlayerbotAI::Class2SpellFamilyName(bot->GetClass()));

    for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
    {
        SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(i);
        if (!ability)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(ability->Spell, DIFFICULTY_NONE);
        if (!spellInfo)
            continue;

        // The riding line's own spells rewrite skill 762 through AddSpell's learn-skill branch; InitSkills
        // owns the riding tier, so they are never learned from here.
        if (IsRidingSkillSpell(spellInfo))
            continue;

        // Class spells only: every line of the class category (SKILL_CATEGORY_CLASS), not the
        // professions/secondaries — the spell book of the level-60 mage from the official client
        // (146 spells) is class lines plus a handful of secondaries, and Learned acquire methods
        // cover the professions too (a first pass here learned 887 spells of recipes and tools).
        SkillLineEntry const* line = sSkillLineStore.LookupEntry(ability->SkillLine);
        if (!line || line->CategoryID != SKILL_CATEGORY_CLASS)
            continue;

        // Only the spells a character acquires by existing (automatic ranks): everything the
        // trainer teaches comes from InitAvailableSpells, which knows the trainer's level and
        // prerequisite gate. This pass used to take plain Learned rows too, and the 1.60 data has
        // no SpellLevel on many of them, so the level gate let 60-level and 3.3.5 talent-passive
        // spells into a level-10 spell book (73 spells on a level-10 warrior, 9 of them real).
        switch (ability->GetAcquireMethod())
        {
            case SkillLineAbilityAcquireMethod::AutomaticSkillRank:
            case SkillLineAbilityAcquireMethod::AutomaticCharLevel:
            case SkillLineAbilityAcquireMethod::LearnedOrAutomaticCharLevel:
                break;
            default:
                continue;
        }
        if (spellInfo->ShowFutureSpellPlayerConditionID && !ConditionMgr::IsPlayerMeetingCondition(bot, spellInfo->ShowFutureSpellPlayerConditionID))
            continue;

        if (!ability->RaceMask.IsEmpty() && !ability->RaceMask.HasRace(race))
            continue;

        if (ability->ClassMask && !(ability->ClassMask & classMask))
            continue;

        // Many ability rows carry no class mask of their own (the skill line used to narrow them),
        // so every class's rows passed the check above. The spell family is the reliable class tag:
        // only spells of this bot's family (plus the family-less ones a class trainer still teaches,
        // which InitAvailableSpells fits by class and race itself).
        if (spellInfo->SpellFamilyName != SpellFamilyNames(familyName))
            continue;

        uint32 const requiredLevel = std::max(spellInfo->SpellLevel, spellInfo->BaseLevel);
        if (requiredLevel > level)
            continue;

        bot->LearnSpell(ability->Spell, false);
    }
}

void PlayerbotFactory::InitRacials()
{
    // 12.x hands racials out through the skill lines and never gets there for bots (no bot in the DB owns one),
    // so they are learned here by race. Classic 1.60 ids: the vanilla values of the V2 bot racial list
    // (PlayerbotV2/Bot/States/State_InCombat.cpp) plus the racials on the official first-login action bars
    // (sql/custom/world/2026_10_06_00_world_classic_start_sniff.sql, 2026_10_02_01_world_skyborne_racials.sql).
    std::vector<uint32> spells;
    switch (bot->GetRace())
    {
        case RACE_HUMAN:
            spells = { 1259718, 20600 };               // Perception
            break;
        case RACE_ORC:
            spells = { 20572 };                        // Blood Fury
            break;
        case RACE_DWARF:
            spells = { 20594, 2481 };                  // Stoneform, Find Treasure
            break;
        case RACE_NIGHTELF:
            spells = { 1259799, 20580 };               // Elune's Light, Shadowmeld
            break;
        case RACE_UNDEAD_PLAYER:
            spells = { 7744, 20577 };                  // Will of the Forsaken, Cannibalize
            break;
        case RACE_TAUREN:
            spells = { 20549, 20552 };                 // War Stomp, Plainrunning
            break;
        case RACE_GNOME:
            spells = { 20589 };                        // Escape Artist
            // The second gnome racial has one spell per class.
            switch (bot->GetClass())
            {
                case CLASS_WARRIOR: spells.push_back(1259813); break;
                case CLASS_ROGUE:   spells.push_back(1259812); break;
                case CLASS_PRIEST:  spells.push_back(1259823); break;
                case CLASS_MAGE:    spells.push_back(1259817); break;
                case CLASS_WARLOCK: spells.push_back(1259821); break;
                default: break;
            }
            break;
        case RACE_TROLL:
            spells = { 20554, 1260270 };               // Berserking
            break;
        case RACE_SKYBORNE_ALLIANCE:
            spells = { 1259705, 1259416, 1259707, 1259710 };  // Read Ley Line, Walk on Air, Elemental Insight, Wind Blessed
            break;
        case RACE_SKYBORNE_HORDE:
            spells = { 1259686, 1259416, 1259707, 1259710 };  // Skysight, Walk on Air, Elemental Insight, Wind Blessed
            break;
        default:
            return;
    }

    for (uint32 spellId : spells)
    {
        // Ids missing from this client build are skipped: LearnSpell would only log an error for them.
        if (sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
            bot->LearnSpell(spellId, false);
    }
}

void PlayerbotFactory::InitSpecialSpells()
{
    for (std::vector<uint32>::iterator i = sPlayerbotAIConfig.RandomBotSpellIds.begin();
         i != sPlayerbotAIConfig.RandomBotSpellIds.end(); ++i)
    {
        uint32 spellId = *i;
        bot->LearnSpell(spellId, false);
    }
}

void PlayerbotFactory::InitTalents(uint32 /*specNo*/)
{
    // 3.3.5 talent trees do not exist in 1.60: talents are trait configs, handled by the V2
    // classic talent package (see InitTalentsTree).
}

void PlayerbotFactory::InitTalentsByTemplate(uint32 /*specTab*/)
{
    // See InitTalents: the 3.3.5 talent template path does not apply to 1.6x traits.
}

void PlayerbotFactory::AddPrevQuests(uint32 questId, std::list<uint32>& questIds)
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return;

    // 12.x keeps a single previous quest id instead of the 3.3.5 prevQuests list.
    int32 const prevId = quest->GetPrevQuestId();
    if (prevId > 0)
    {
        uint32 const id = uint32(prevId);
        // A quest already in the list means the chain loops; stop instead of recursing forever.
        if (std::find(questIds.begin(), questIds.end(), id) != questIds.end())
            return;

        AddPrevQuests(id, questIds);
        questIds.remove(id);
        questIds.push_back(id);
    }
}

void PlayerbotFactory::InitQuests(std::list<uint32>& questMap, bool withRewardItem)
{
    for (std::list<uint32>::iterator i = questMap.begin(); i != questMap.end(); ++i)
    {
        uint32 questId = *i;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        // Classic 1.60: a quest without a classic level row is not part of the game world (quest_template also holds
        // the TBC/WotLK quests). The offering level is the gate, the quest level less four when the row has none.
        int32 const minLevel = quest->GetClassicMinLevel() > 0 ? quest->GetClassicMinLevel()
                                                               : std::max(1, quest->GetClassicQuestLevel() - 4);
        if (quest->GetClassicQuestLevel() <= 0 || minLevel > bot->GetLevel() ||
            !bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false))
            continue;

        bot->SetQuestStatus(questId, QUEST_STATUS_COMPLETE);
        // set reward to 5 to skip majority quest reward
        uint32 reward = withRewardItem ? 0 : 5;
        bot->RewardQuest(quest, LootItemType::Item, reward, bot, false);

        if (!withRewardItem)
        {
            // destroy the quest reward item
            if (uint32 itemId = quest->RewardChoiceItemId[reward])
            {
                bot->DestroyItemCount(itemId, quest->RewardChoiceItemCount[reward], true);
            }

            if (quest->GetRewItemsCount())
            {
                for (uint32 i = 0; i < quest->GetRewItemsCount(); ++i)
                {
                    if (uint32 itemId = quest->RewardItemId[i])
                    {
                        bot->DestroyItemCount(itemId, quest->RewardItemCount[i], true);
                    }
                }
            }
        }
    }
}

void PlayerbotFactory::InitInstanceQuests()
{
    // Yunfan: use configuration instead of hard code
    uint32 currentXP = bot->GetXP();
    // TC_LOG_INFO("playerbots", "Initializing quests...");
    InitQuests(classQuestIds, false);
    InitQuests(specialQuestIds, false);

    // quest rewards boost bot level, so reduce back
    bot->GiveLevel(level);

    ClearInventory();
    bot->SetXP(currentXP);
}

void PlayerbotFactory::ClearInventory()
{
    DestroyItemsVisitor visitor(bot);
    IterateItems(&visitor);
}

void PlayerbotFactory::ClearAllItems()
{
    DestroyItemsVisitor visitor(bot);
    IterateItems(&visitor, ITERATE_ALL_ITEMS);
}

static void DestroyObsoleteAmmo(Player* bot, ItemTemplate const* ammo)
{
    std::vector<Item*> obsolete;
    auto const collect = [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (proto->GetClass() != ITEM_CLASS_PROJECTILE || proto->GetId() == ammo->GetId())
            return;

        if (proto->GetSubClass() != ammo->GetSubClass())
        {
            obsolete.push_back(item);
            return;
        }

        // Ranked as in RandomItemMgr::BuildCacheAmmo(); ammo above the stocked kind can only come from a player
        uint32 const stack = proto->GetMaxStackSize();
        uint32 const ammoStack = ammo->GetMaxStackSize();
        if (stack < ammoStack || (stack == ammoStack && proto->GetBaseItemLevel() <= ammo->GetBaseItemLevel()))
            obsolete.push_back(item);
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            collect(item);

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(slot))
                    collect(item);

    for (Item* item : obsolete)
        bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
}

void PlayerbotFactory::InitAmmo()
{
    uint8 const botClass = bot->GetClass();
    if (botClass != CLASS_HUNTER && botClass != CLASS_ROGUE && botClass != CLASS_WARRIOR)
        return;

    Item const* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!item)
        return;

    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return;

    uint32 subClass = 0;
    switch (proto->GetSubClass())
    {
        case ITEM_SUBCLASS_WEAPON_GUN:
            subClass = ITEM_SUBCLASS_BULLET;
            break;
        case ITEM_SUBCLASS_WEAPON_BOW:
        case ITEM_SUBCLASS_WEAPON_CROSSBOW:
            subClass = ITEM_SUBCLASS_ARROW;
            break;
        default: // invalid weapon type, nothing to do
            return;
    }

    uint32 entry = sRandomItemMgr.GetAmmo(level, subClass);
    if (!entry)
        return;

    ItemTemplate const* ammoProto = sObjectMgr->GetItemTemplate(entry);
    if (!ammoProto)
        return;

    DestroyObsoleteAmmo(bot, ammoProto);

    uint32 count = bot->GetItemCount(entry);
    uint32 maxCount = botClass == CLASS_HUNTER ? 6000 : 1000;

    if (count < maxCount)
    {
        // Stock as much as fits: each stack destroyed above has freed a slot for a full stack of the new ammo
        ItemPosCountVec dest;
        bot->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, dest, entry, maxCount - count);
        if (!dest.empty())
        {
            bot->StoreNewItem(dest, entry, true);
        }
    }

    bot->SetAmmo(entry);
}

uint32 PlayerbotFactory::CalcMixedGearScore(uint32 gs, uint32 quality)
{
    return gs * PlayerbotAI::GetItemScoreMultiplier(ItemQualities(quality));
}

void PlayerbotFactory::DestroyEquippedGear(Player* bot)
{
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_TABARD || slot == EQUIPMENT_SLOT_BODY)
            continue;
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
    }
}

void PlayerbotFactory::AutoGear(Player* bot, uint32 itemQuality, uint32 ilvl, bool incremental, bool secondChance,
                                bool applyFinishers)
{
    uint32 gs = ilvl == 0 ? 0 : CalcMixedGearScore(ilvl, itemQuality);
    PlayerbotFactory factory(bot, bot->GetLevel(), itemQuality, gs);
    factory.InitEquipment(incremental, secondChance);

    if (!applyFinishers)
        return;

    factory.InitAmmo();
    if (bot->GetLevel() >= sPlayerbotAIConfig.MinEnchantingBotLevel)
        factory.ApplyEnchants();
    bot->DurabilityRepairAll(false, 1.0f, false);
}

namespace
{
// Ground mounts a race can ride at each riding tier: [0] walk-pace (needs Apprentice), [1] swift (needs
// Journeyman). 1.60 keeps the vanilla mount spells; entries the client data does not have are dropped by
// the caller. Tiers added for TBC races stay: they are filtered out when the spell is missing.
std::pair<std::vector<uint32>, std::vector<uint32>> RaceMountTiers(uint8 race, TeamId team)
{
    switch (race)
    {
        case RACE_HUMAN:        return { { 470, 6648, 458, 472 }, { 23228, 23227, 23229 } };
        case RACE_ORC:          return { { 6654, 6653, 580 }, { 23250, 23252, 23251 } };
        case RACE_DWARF:        return { { 6899, 6777, 6898 }, { 23238, 23239, 23240 } };
        case RACE_NIGHTELF:     return { { 10789, 8394, 10793 }, { 23219, 23220, 63637 } };
        case RACE_UNDEAD_PLAYER:return { { 17463, 17464, 17462 }, { 17465, 23246, 66846 } };
        case RACE_TAUREN:       return { { 18990, 18989, 64657 }, { 23249, 23248, 23247 } };
        case RACE_GNOME:        return { { 10969, 17453, 10873, 17454 }, { 23225, 23223, 23222 } };
        case RACE_TROLL:        return { { 10796, 10799, 8395 }, { 23241, 23242, 23243 } };
        case RACE_DRAENEI:      return { { 34406, 35711, 35710 }, { 35713, 35712, 35714 } };
        case RACE_BLOODELF:     return { { 33660, 35020, 35022, 35018 }, { 35025, 35025, 35027 } };
        default:
            return team == TEAM_HORDE ? std::pair<std::vector<uint32>, std::vector<uint32>>{ { 6654, 6653, 580 }, { 23250, 23252, 23251 } }
                                      : std::pair<std::vector<uint32>, std::vector<uint32>>{ { 470, 6648, 458, 472 }, { 23228, 23227, 23229 } };
    }
}
} // namespace

void PlayerbotFactory::InitMounts()
{
    // The mount pick below gates on the riding skill, and the spell learning before this point may
    // have rewritten it, so the tier is re-asserted first.
    InitRiding();

    // 1.60 has two ground tiers and no flying mounts: the normal mount at 40, the swift one at 60.
    constexpr uint32 firstmount = 40;
    constexpr uint32 secondmount = 60;

    if (bot->GetLevel() < firstmount)
        return;

    auto [slowSpells, fastSpells] = RaceMountTiers(bot->GetRace(), bot->GetTeamId());
    std::map<uint8, std::map<uint32, std::vector<uint32>>> mounts;
    mounts[bot->GetRace()][0] = slowSpells;
    mounts[bot->GetRace()][1] = fastSpells;

    uint32 const ridingSkill = bot->GetPureSkillValue(SKILL_RIDING);

    // Only a spell that actually mounts the bot counts as a mount. Some ids the 3.3.5 tables carry
    // for a tier are not mounts in this build: learning one either does nothing or, for a riding
    // teacher spell, rewrites skill 762 through AddSpell's learn-skill branch.
    for (uint32 type = 0; type < 2; type++)
        std::erase_if(mounts[bot->GetRace()][type], [](uint32 spell) {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spell, DIFFICULTY_NONE);
            return !info || !info->HasAura(SPELL_AURA_MOUNTED) || IsRidingSkillSpell(info);
        });

    for (uint32 type = 0; type < 2; type++)
    {
        if (mounts[bot->GetRace()][type].empty())
            continue;
        bool hasMount = false;
        for (uint32& spell : mounts[bot->GetRace()][type])
        {
            if (bot->HasSpell(spell))
            {
                hasMount = true;
                break;
            }
        }
        if (hasMount)
            continue;

        if (bot->GetLevel() < secondmount && type == 1)
            continue;

        uint32 index = urand(0, mounts[bot->GetRace()][type].size() - 1);
        uint32 spell = mounts[bot->GetRace()][type][index];
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spell, DIFFICULTY_NONE);
        if (spellInfo && PlayerbotSpellRepository::Instance().GetRequiredRidingSkill(spellInfo) > ridingSkill)
            continue;

        if (spell)
        {
            bot->LearnSpell(spell, false);
            TC_LOG_DEBUG("playerbots", "Bot {} ({}) learned {} mount {}", bot->GetGUID().ToString().c_str(),
                      bot->GetLevel(), type == 0 ? "slow" : "fast", spell);
        }
    }
}

void PlayerbotFactory::UnbindInstance()
{
    // 12.x keeps no instance bind manager of the 3.3.5 shape, so there is nothing to unbind here.
}

void PlayerbotFactory::InitPotions()
{
    uint32 effects[] = {SPELL_EFFECT_HEAL, SPELL_EFFECT_ENERGIZE};
    for (uint8 i = 0; i < 2; ++i)
    {
        uint32 effect = effects[i];

        if (effect == SPELL_EFFECT_ENERGIZE && !bot->GetPower(POWER_MANA))
            continue;

        FindPotionVisitor visitor(bot, effect);
        IterateItems(&visitor);
        if (!visitor.GetResult().empty())
            continue;

        uint32 itemId = sRandomItemMgr.GetRandomPotion(level, effect);
        if (!itemId)
        {
            // TC_LOG_INFO("playerbots", "No potions (type {}) available for bot {} ({} level)", effect,
            // bot->GetName().c_str(), bot->GetLevel());
            continue;
        }

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        uint32 maxCount = proto->GetMaxStackSize();
        StoreNewItemInInventorySlot(bot, itemId, urand(maxCount / 2, maxCount));
    }
}

void PlayerbotFactory::InitFood()
{
    if (botAI && botAI->HasCheat(BotCheatMask::food))
    {
        return;
    }
    std::unordered_map<uint32, std::vector<uint32>> items;
    ItemTemplateContainer const& itemTemplateContainer = sObjectMgr->GetItemTemplateStore();
    for (ItemTemplateContainer::const_iterator i = itemTemplateContainer.begin(); i != itemTemplateContainer.end();
         ++i)
    {
        uint32 itemId = i->first;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        if (proto->GetClass() != ITEM_CLASS_CONSUMABLE || proto->GetSubClass() != ITEM_SUBCLASS_FOOD_DRINK ||
            (PlayerbotItem::GetSpellCategory(proto, 0) != 11 && PlayerbotItem::GetSpellCategory(proto, 0) != 59) || proto->GetBonding() != BIND_NONE)
            continue;

        if (proto->GetBaseRequiredLevel() > bot->GetLevel() ||
            static_cast<int32>(proto->GetBaseRequiredLevel()) < static_cast<int32>(bot->GetLevel()) - 9)
            continue;

        if (proto->GetRequiredSkill() && !bot->HasSkill(proto->GetRequiredSkill()))
            continue;

        if (proto->GetArea(0) || proto->GetMap())
            continue;

        items[PlayerbotItem::GetSpellCategory(proto, 0)].push_back(itemId);
    }

    uint32 categories[] = {11, 59};
    for (size_t i = 0; i < sizeof(categories) / sizeof(uint32); ++i)
    {
        uint32 category = categories[i];
        std::vector<uint32>& ids = items[category];
        int tries = 0;
        for (int j = 0; j < 2; j++)
        {
            uint32 index = urand(0, ids.size() - 1);
            if (index >= ids.size())
                continue;

            uint32 itemId = ids[index];
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            // beer / wine ...
            if (PlayerbotItem::GetSpellId(proto, 0) == 11007 || PlayerbotItem::GetSpellId(proto, 0) == 11008 ||
                PlayerbotItem::GetSpellId(proto, 0) == 11009 || PlayerbotItem::GetSpellId(proto, 0) == 11629 ||
                PlayerbotItem::GetSpellId(proto, 0) == 50986)
            {
                tries++;
                if (tries > 5)
                {
                    continue;
                }
                j--;
                continue;
            }
            StoreItem(itemId, proto->GetMaxStackSize());
        }
    }
}

void PlayerbotFactory::InitReagents()
{
    std::vector<std::pair<uint32, uint32>> items;
    switch (bot->GetClass())
    {
        case CLASS_DRUID:
            if (level >= 20 && level < 30)
                items.push_back({17034, 20});   // Maple Seed
            else if (level >= 30 && level < 40)
                items.push_back({17035, 20});   // Stranglethorn Seed
            else if (level >= 40 && level < 50)
                items.push_back({17036, 20});   // Ashwood Seed
            else if (level >= 50 && level < 60)
            {
                items.push_back({17037, 20});   // Hornbeam Seed
                items.push_back({17021, 20});   // Wild Berries
            }
            else if (level >= 60 && level < 69)
            {
                items.push_back({17038, 20});   // Ironwood Seed
                items.push_back({17026, 20});   // Wild Thornroot
            }
            else if (level == 69)
            {
                items.push_back({22147, 20});   // Flintweed Seed
                items.push_back({17026, 20});   // Wild Thornroot
            }
            else if (level >= 70 && level < 79)
            {
                items.push_back({22147, 20});   // Flintweed Seed
                items.push_back({22148, 20});   // Wild Quillvine
            }
            else if (level == 79)
            {
                items.push_back({44614, 20});   // Starleaf Seed
                items.push_back({22148, 20});   // Wild Quillvine
            }
            else if (level >= 80)
            {
                items.push_back({44614, 20});   // Starleaf Seed
                items.push_back({44605, 20});   // Wild Spineleaf
            }
            break;
        case CLASS_MAGE:
            if (level >= 20)
                items.push_back({17031, 20});  // Rune of Teleportation
            if (level >= 40)
                items.push_back({17032, 20});  // Rune of Portals
            if (level >= 56)
                items.push_back({17020, 20});  // Arcane Powder
            break;
        case CLASS_PALADIN:
            if (level >= 52)
                items.push_back({21177, 100});   // Symbol of Kings
            break;
        case CLASS_PRIEST:
            if (level >= 48 && level < 56)
                items.push_back({17028, 40});  // Holy Candle
            else if (level >= 56 && level < 60)
            {
                items.push_back({17028, 20});  // Holy Candle
                items.push_back({17029, 20});  // Sacred Candle
            }
            else if (level >= 60 && level < 77)
                items.push_back({17029, 40});  // Sacred Candle
            else if (level >= 77 && level < 80)
            {
                items.push_back({17029, 20});  // Sacred Candle
                items.push_back({44615, 20});  // Devout Candle
            }
            else if (level >= 80)
                items.push_back({44615, 40});  // Devout Candle
            break;
        case CLASS_SHAMAN:
        {
            HasRelicBySubclassVisitor relicVisitor(ITEM_SUBCLASS_ARMOR_TOTEM);
            IterateItems(&relicVisitor, (IterateItemsMask)(ITERATE_ITEMS_IN_BAGS | ITERATE_ITEMS_IN_EQUIP));
            bool hasRelic = relicVisitor.found;

            if (!hasRelic)
            {
                if (level >= 4)
                    items.push_back({5175, 1});  // Earth Totem
                if (level >= 10)
                    items.push_back({5176, 1});  // Flame Totem
                if (level >= 20)
                    items.push_back({5177, 1});  // Water Totem
            }
            else
            {
                ItemIds totemIds = {5175, 5176, 5177, 5178};
                FindItemByIdsVisitor totemVisitor(totemIds);
                IterateItems(&totemVisitor, (IterateItemsMask)(ITERATE_ITEMS_IN_BAGS | ITERATE_ITEMS_IN_EQUIP | ITERATE_ITEMS_IN_BANK));
                for (Item* item : totemVisitor.GetResult())
                    bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            }
            if (level >= 30)
            {
                if (!hasRelic)
                    items.push_back({5178, 1});  // Air Totem
                items.push_back({17030, 20});  // Ankh
            }
            break;
        }
        case CLASS_WARLOCK:
            items.push_back({6265, 5});  // Soul Shard
            break;
        default:
            break;
    }
    for (std::pair item : items)
    {
        int count = (int)item.second - (int)bot->GetItemCount(item.first);
        if (count > 0)
            StoreItem(item.first, count);
    }
}

void PlayerbotFactory::CleanupConsumables() // remove old consumables as part of randombot level-up maintenance
{
    std::vector<Item*> itemsToDelete;

    std::vector<Item*> items;
    for (uint32 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            items.push_back(item);

    for (uint32 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        if (Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                if (Item* item = bag->GetItemByPos(j))
                    items.push_back(item);

    for (Item* item : items)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto) continue;

        // Remove ammo
        if (proto->GetClass() == ITEM_CLASS_PROJECTILE)
            itemsToDelete.push_back(item);

        // Remove food/drink
        if (proto->GetClass() == ITEM_CLASS_CONSUMABLE && proto->GetSubClass() == ITEM_SUBCLASS_FOOD_DRINK)
            itemsToDelete.push_back(item);

        // Remove potions
        if (proto->GetClass() == ITEM_CLASS_CONSUMABLE && proto->GetSubClass() == ITEM_SUBCLASS_POTION)
            itemsToDelete.push_back(item);

        // Remove reagents
        if (proto->GetClass() == ITEM_CLASS_REAGENT || (proto->GetClass() == ITEM_CLASS_MISCELLANEOUS && proto->GetSubClass() == ITEM_SUBCLASS_REAGENT))
            itemsToDelete.push_back(item);
    }

    std::set<uint32> idsToDelete = {
        BRILLIANT_MANA_OIL, LESSER_MANA_OIL, MINOR_MANA_OIL,
        BRILLIANT_WIZARD_OIL, WIZARD_OIL, LESSER_WIZARD_OIL, MINOR_WIZARD_OIL,
        ELEMENTAL_SHARPENING_STONE, DENSE_SHARPENING_STONE, SOLID_SHARPENING_STONE,
        HEAVY_SHARPENING_STONE, COARSE_SHARPENING_STONE, ROUGH_SHARPENING_STONE,
        DENSE_WEIGHTSTONE, SOLID_WEIGHTSTONE,
        HEAVY_WEIGHTSTONE, COARSE_WEIGHTSTONE, ROUGH_WEIGHTSTONE,
        INSTANT_POISON_VI, INSTANT_POISON_V,
        INSTANT_POISON_IV, INSTANT_POISON_III, INSTANT_POISON_II, INSTANT_POISON,
        DEADLY_POISON_V,
        DEADLY_POISON_IV, DEADLY_POISON_III, DEADLY_POISON_II, DEADLY_POISON
    };

    for (Item* item : items)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto) continue;

        if (idsToDelete.find(proto->GetId()) != idsToDelete.end())
            itemsToDelete.push_back(item);
    }

    for (Item* item : itemsToDelete)
        bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
}


void PlayerbotFactory::CancelAuras() { bot->RemoveAllAuras(); }

void PlayerbotFactory::InitInventory()
{
    InitInventoryTrade();
    InitInventoryEquip();
    InitInventorySkill();
}

void PlayerbotFactory::InitInventorySkill()
{
    if (bot->HasSkill(SKILL_MINING) && !bot->HasItemCount(2901, 1, true))
        StoreItem(2901, 1);  // Mining Pick

    if ((bot->HasSkill(SKILL_BLACKSMITHING) || bot->HasSkill(SKILL_ENGINEERING)) &&
        !bot->HasItemCount(5956, 1, true))
        StoreItem(5956, 1);  // Blacksmith Hammer

    if (bot->HasSkill(SKILL_ENGINEERING) && !bot->HasItemCount(6219, 1, true))
        StoreItem(6219, 1);  // Arclight Spanner

    if (bot->HasSkill(SKILL_ENCHANTING) && !bot->HasItemCount(16207, 1, true))
        StoreItem(16207, 1);  // Runed Arcanite Rod

    if (bot->HasSkill(SKILL_SKINNING) && !bot->HasItemCount(7005, 1, true))
        StoreItem(7005, 1);  // Skinning Knife
}

Item* PlayerbotFactory::StoreItem(uint32 itemId, uint32 count)
{
    //ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId); //not used, line marked for removal.
    ItemPosCountVec sDest;
    InventoryResult msg = bot->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, sDest, itemId, count);
    if (msg != EQUIP_ERR_OK)
        return nullptr;

    return bot->StoreNewItem(sDest, itemId, true, GenerateItemRandomBonusListId(itemId));
}

void PlayerbotFactory::InitInventoryTrade()
{
    uint32 itemId = sRandomItemMgr.GetRandomTrade(level);
    if (!itemId)
    {
        TC_LOG_ERROR("playerbots", "No trade items available for bot {} ({} level)", bot->GetName().c_str(),
                  bot->GetLevel());
        return;
    }

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return;

    uint32 count = 1, stacks = 1;
    switch (proto->GetQuality())
    {
        case ITEM_QUALITY_NORMAL:
            count = proto->GetMaxStackSize();
            stacks = urand(1, 3);
            break;
        case ITEM_QUALITY_UNCOMMON:
            stacks = 1;
            int maxStackSize = proto->GetMaxStackSize() / 2;
            uint32 max = std::max(1, maxStackSize);
            count = urand(1, max);
            break;
    }

    for (uint32 i = 0; i < stacks; i++)
        StoreItem(itemId, count);
}

void PlayerbotFactory::InitInventoryEquip()
{
    std::vector<uint32> ids;

    uint32 desiredQuality = itemQuality;
    if (urand(0, 100) < 100 * sPlayerbotAIConfig.RandomGearLoweringChance && desiredQuality > ITEM_QUALITY_NORMAL)
    {
        desiredQuality--;
    }

    ItemTemplateContainer const& itemTemplates = sObjectMgr->GetItemTemplateStore();
    for (auto const& itr : itemTemplates)
    {
        ItemTemplate const* proto = &itr.second;
        if (!proto)
            continue;

        if ((proto->GetClass() != ITEM_CLASS_ARMOR && proto->GetClass() != ITEM_CLASS_WEAPON) ||
            (proto->GetBonding() == BIND_ON_ACQUIRE || proto->GetBonding() == BIND_ON_USE))
            continue;

        if (proto->GetClass() == ITEM_CLASS_ARMOR && !CanEquipArmor(proto))
            continue;

        if (proto->GetClass() == ITEM_CLASS_WEAPON && !CanEquipWeapon(proto))
            continue;

        if (proto->GetQuality() != desiredQuality)
            continue;

        if (!CanEquipItem(proto))
            continue;

        ids.push_back(itr.first);
    }

    uint32 maxCount = urand(0, 3);
    uint32 count = 0;
    for (uint32 attempts = 0; attempts < 15; attempts++)
    {
        uint32 index = urand(0, ids.size() - 1);
        if (index >= ids.size())
            continue;

        uint32 itemId = ids[index];
        if (StoreItem(itemId, 1) && count++ >= maxCount)
            break;
    }
}

void PlayerbotFactory::InitGuild()
{
    if (bot->GetGuildId())
    {
        if (!bot->HasItemCount(5976, 1) && bot->GetLevel() > 9)
            StoreItem(5976, 1);
        return;
    }

    if (sPlayerbotAIConfig.DeleteRandomBotGuilds)
        return;

    std::string guildName = PlayerbotGuildMgr::instance().AssignToGuild(bot);
    if (guildName.empty())
        return;

    Guild* guild = sGuildMgr->GetGuildByName(guildName);
    if (!guild)
    {
        if (!PlayerbotGuildMgr::instance().CreateGuild(bot, guildName))
            TC_LOG_ERROR("playerbots","Failed to create guild {} for bot {}", guildName, bot->GetName());
        return;
    }
    else
    {
        // 12.x adds members through a transaction and an optional rank id (the rank comes
        // from the guild's own rank table).
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        bool const added = guild->AddMember(trans, bot->GetGUID(), {});
        CharacterDatabase.CommitTransaction(trans);
        if (added)
            PlayerbotGuildMgr::instance().OnGuildUpdate(guild);
        else
            TC_LOG_ERROR("playerbots","Bot {} failed to join guild {}.", bot->GetName(), guildName);
    }
    // add guild tabard
    if (bot->GetGuildId() && bot->GetLevel() > 9 && urand(0, 4) && !bot->HasItemCount(5976, 1))
        StoreItem(5976, 1);
}

void PlayerbotFactory::InitImmersive()
{
    uint32 owner = bot->GetGUID().GetCounter();
    std::map<Stats, uint32> percentMap;

    bool initialized = false;
    for (uint32 i = STAT_STRENGTH; i < MAX_STATS; ++i)
    {
        Stats type = (Stats)i;

        std::ostringstream name;
        name << "immersive_stat_" << i;

        uint32 value = sRandomPlayerbotMgr.GetValue(owner, name.str());
        if (value)
            initialized = true;

        percentMap[type] = value;
    }

    if (!initialized)
    {
        switch (bot->GetClass())
        {
            case CLASS_DRUID:
            case CLASS_SHAMAN:
                percentMap[STAT_STRENGTH] = 10;
                percentMap[STAT_INTELLECT] = 10;
                percentMap[STAT_SPIRIT] = 20;
                percentMap[STAT_AGILITY] = 30;
                percentMap[STAT_STAMINA] = 30;
                break;
            case CLASS_PALADIN:
                percentMap[STAT_STRENGTH] = 10;
                percentMap[STAT_INTELLECT] = 10;
                percentMap[STAT_SPIRIT] = 20;
                percentMap[STAT_AGILITY] = 50;
                percentMap[STAT_STAMINA] = 10;
                break;
            case CLASS_WARRIOR:
                percentMap[STAT_STRENGTH] = 10;
                percentMap[STAT_SPIRIT] = 20;
                percentMap[STAT_AGILITY] = 50;
                percentMap[STAT_STAMINA] = 20;
                break;
            case CLASS_ROGUE:
            case CLASS_HUNTER:
                percentMap[STAT_SPIRIT] = 40;
                percentMap[STAT_AGILITY] = 50;
                percentMap[STAT_STAMINA] = 10;
                break;
            case CLASS_MAGE:
                percentMap[STAT_INTELLECT] = 50;
                percentMap[STAT_SPIRIT] = 40;
                percentMap[STAT_STAMINA] = 10;
                break;
            case CLASS_PRIEST:
                percentMap[STAT_INTELLECT] = 50;
                percentMap[STAT_SPIRIT] = 40;
                percentMap[STAT_STAMINA] = 10;
                break;
            case CLASS_WARLOCK:
                percentMap[STAT_INTELLECT] = 50;
                percentMap[STAT_SPIRIT] = 10;
                percentMap[STAT_STAMINA] = 40;
                break;
        }

        for (uint8 i = 0; i < 5; i++)
        {
            Stats from = (Stats)urand(STAT_STRENGTH, MAX_STATS - 1);
            Stats to = (Stats)urand(STAT_STRENGTH, MAX_STATS - 1);
            int32 delta = urand(0, 5 + bot->GetLevel() / 3);
            if (from != to && static_cast<int32>(percentMap[to]) + delta <= 100 &&
                static_cast<int32>(percentMap[from]) - delta >= 0)
            {
                percentMap[to] += delta;
                percentMap[from] -= delta;
            }
        }

        for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i)
        {
            Stats type = (Stats)i;

            std::ostringstream name;
            name << "immersive_stat_" << i;
            sRandomPlayerbotMgr.SetValue(owner, name.str(), percentMap[type]);
        }
    }
}

void PlayerbotFactory::ApplyEnchantTemplate()
{
    uint8 tab = AiFactory::GetPlayerSpecTab(bot);

    switch (bot->GetClass())
    {
        case CLASS_WARRIOR:
            if (tab == 2)
                ApplyEnchantTemplate(12);
            else if (tab == 1)
                ApplyEnchantTemplate(11);
            else
                ApplyEnchantTemplate(10);
            break;
        case CLASS_DRUID:
            if (tab == 2)
                ApplyEnchantTemplate(112);
            else if (tab == 0)
                ApplyEnchantTemplate(110);
            else
                ApplyEnchantTemplate(111);
            break;
        case CLASS_SHAMAN:
            if (tab == 0)
                ApplyEnchantTemplate(70);
            else if (tab == 2)
                ApplyEnchantTemplate(71);
            else
                ApplyEnchantTemplate(72);
            break;
        case CLASS_PALADIN:
            if (tab == 0)
                ApplyEnchantTemplate(20);
            else if (tab == 2)
                ApplyEnchantTemplate(22);
            else if (tab == 1)
                ApplyEnchantTemplate(21);
            break;
        case CLASS_HUNTER:
            ApplyEnchantTemplate(30);
            break;
        case CLASS_ROGUE:
            ApplyEnchantTemplate(40);
            break;
        case CLASS_MAGE:
            ApplyEnchantTemplate(80);
            break;
        case CLASS_WARLOCK:
            ApplyEnchantTemplate(90);
            break;
        case CLASS_PRIEST:
            ApplyEnchantTemplate(50);
            break;
    }
}

void PlayerbotFactory::ApplyEnchantTemplate(uint8 spec)
{
    for (EnchantContainer::const_iterator itr = GetEnchantContainerBegin(); itr != GetEnchantContainerEnd(); ++itr)
        if ((*itr).ClassId == bot->GetClass() && (*itr).SpecId == spec)
        {
            uint32 spellid = (*itr).SpellId;
            uint32 slot = (*itr).SlotId;
            Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!pItem || !pItem->IsInWorld() || !pItem->GetOwner() || !pItem->GetOwner()->IsInWorld() ||
                !pItem->GetOwner()->GetSession())
                return;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellid, DIFFICULTY_NONE);
            if (!spellInfo)
                return;

            if (spellInfo->GetEffects().empty())
                return;

            uint32 enchantid = GetEffectOrDefault(spellInfo, SpellEffIndex(0)).MiscValue;
            if (!enchantid)
            {
                // TC_LOG_ERROR("playerbots", "{}: Invalid enchantid ", enchantid, " report to devs",
                // bot->GetName().c_str());
                return;
            }

            if (!((1 << pItem->GetTemplate()->GetSubClass()) & spellInfo->EquippedItemSubClassMask) &&
                !((1 << pItem->GetTemplate()->GetInventoryType()) & spellInfo->EquippedItemInventoryTypeMask))
            {
                // TC_LOG_ERROR("playerbots", "{}: items could not be enchanted, wrong item type equipped",
                // bot->GetName().c_str());
                return;
            }

            bot->ApplyEnchantment(pItem, PERM_ENCHANTMENT_SLOT, false);
            pItem->SetEnchantment(PERM_ENCHANTMENT_SLOT, enchantid, 0, 0);
            bot->ApplyEnchantment(pItem, PERM_ENCHANTMENT_SLOT, true);
        }
    // botAI->EnchantItemT((*itr).SpellId, (*itr).SlotId);
    // const SpellItemEnchantmentEntry* a = sSpellItemEnchantmentStore.LookupEntry(1);
}

void PlayerbotFactory::ApplyEnchants()
{
    // Permanent enchants only.
    StatsWeightCalculator calculator(bot);
    for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_TABARD || slot == EQUIPMENT_SLOT_BODY)
            continue;
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || !item->GetOwner())
            continue;

        if (item->GetTemplate() && item->GetTemplate()->GetQuality() < ITEM_QUALITY_UNCOMMON)
            continue;
        int32 bestEnchantId = -1;
        float bestScore = 0;
        for (uint32 const& enchantSpell : enchantSpellIdCache)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(enchantSpell, DIFFICULTY_NONE);
            if (!spellInfo)
                continue;

            if (!item->IsFitToSpellRequirements(spellInfo))
                continue;

            uint32 requiredLevel = spellInfo->BaseLevel;
            if (requiredLevel > bot->GetLevel())
                continue;

            // disable next expansion enchantments
            if (sPlayerbotAIConfig.LimitEnchantExpansion && bot->GetLevel() <= 60 && enchantSpell >= 27899)
                continue;

            if (sPlayerbotAIConfig.LimitEnchantExpansion && bot->GetLevel() <= 70 && enchantSpell >= 44483)
                continue;

            for (uint8 j = 0; j < spellInfo->GetEffects().size(); ++j)
            {
                if (GetEffectOrDefault(spellInfo, SpellEffIndex(j)).Effect != SPELL_EFFECT_ENCHANT_ITEM)
                    continue;

                uint32 enchant_id = GetEffectOrDefault(spellInfo, SpellEffIndex(j)).MiscValue;
                if (!enchant_id)
                    continue;

                SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
                if (!enchant)
                    continue;

                // 12.x names: RequiredSkillID / RequiredSkillRank / MinLevel.
                if (enchant->RequiredSkillID &&
                    (!bot->HasSkill(enchant->RequiredSkillID) ||
                     (bot->GetSkillValue(enchant->RequiredSkillID) < enchant->RequiredSkillRank)))
                {
                    continue;
                }
                if (enchant->MinLevel > bot->GetLevel())
                    continue;

                float score = calculator.CalculateEnchant(enchant_id, item->GetTemplate()->GetDelay(),
                                                          item->GetTemplate()->IsRangedWeapon());
                if (score > 0.0f && score >= bestScore)
                {
                    bestScore = score;
                    bestEnchantId = enchant_id;
                }
            }
        }
        // enchant item
        if (bestEnchantId != -1)
        {
            bot->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
            item->SetEnchantment(PERM_ENCHANTMENT_SLOT, bestEnchantId, 0, 0, bot->GetGUID());
            bot->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, true);
        }
    }
}

std::vector<InventoryType> PlayerbotFactory::GetPossibleInventoryTypeListBySlot(EquipmentSlots slot)
{
    std::vector<InventoryType> ret;
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD:
            ret.push_back(INVTYPE_HEAD);
            break;
        case EQUIPMENT_SLOT_NECK:
            ret.push_back(INVTYPE_NECK);
            break;
        case EQUIPMENT_SLOT_SHOULDERS:
            ret.push_back(INVTYPE_SHOULDERS);
            break;
        case EQUIPMENT_SLOT_BODY:
            ret.push_back(INVTYPE_BODY);
            break;
        case EQUIPMENT_SLOT_CHEST:
            ret.push_back(INVTYPE_CHEST);
            ret.push_back(INVTYPE_ROBE);
            break;
        case EQUIPMENT_SLOT_WAIST:
            ret.push_back(INVTYPE_WAIST);
            break;
        case EQUIPMENT_SLOT_LEGS:
            ret.push_back(INVTYPE_LEGS);
            break;
        case EQUIPMENT_SLOT_FEET:
            ret.push_back(INVTYPE_FEET);
            break;
        case EQUIPMENT_SLOT_WRISTS:
            ret.push_back(INVTYPE_WRISTS);
            break;
        case EQUIPMENT_SLOT_HANDS:
            ret.push_back(INVTYPE_HANDS);
            break;
        case EQUIPMENT_SLOT_FINGER1:
        case EQUIPMENT_SLOT_FINGER2:
            ret.push_back(INVTYPE_FINGER);
            break;
        case EQUIPMENT_SLOT_TRINKET1:
        case EQUIPMENT_SLOT_TRINKET2:
            ret.push_back(INVTYPE_TRINKET);
            break;
        case EQUIPMENT_SLOT_BACK:
            ret.push_back(INVTYPE_CLOAK);
            break;
        case EQUIPMENT_SLOT_MAINHAND:
            ret.push_back(INVTYPE_WEAPON);
            ret.push_back(INVTYPE_2HWEAPON);
            ret.push_back(INVTYPE_WEAPONMAINHAND);
            break;
        case EQUIPMENT_SLOT_OFFHAND:
            ret.push_back(INVTYPE_WEAPON);
            ret.push_back(INVTYPE_2HWEAPON);
            ret.push_back(INVTYPE_WEAPONOFFHAND);
            ret.push_back(INVTYPE_SHIELD);
            ret.push_back(INVTYPE_HOLDABLE);
            break;
        case EQUIPMENT_SLOT_RANGED:
            ret.push_back(INVTYPE_RANGED);
            ret.push_back(INVTYPE_RANGEDRIGHT);
            ret.push_back(INVTYPE_RELIC);
            break;
        default:
            break;
    }
    return ret;
}

void PlayerbotFactory::LoadEnchantContainer()
{
    m_EnchantContainer.clear();

    if (QueryResult result = WorldDatabase.Query("SELECT class, spec, spellid, slotid FROM playerbots_enchants"))
    {
        do
        {
            Field* fields = result->Fetch();

            EnchantTemplate pEnchant;
            pEnchant.ClassId = PlayerbotFieldGet<uint8>(fields[0]);
            pEnchant.SpecId = PlayerbotFieldGet<uint8>(fields[1]);
            pEnchant.SpellId = PlayerbotFieldGet<uint32>(fields[2]);
            pEnchant.SlotId = PlayerbotFieldGet<uint8>(fields[3]);

            m_EnchantContainer.push_back(std::move(pEnchant));
        } while (result->NextRow());
    }
}

void PlayerbotFactory::IterateItems(IterateItemsVisitor* visitor, IterateItemsMask mask)
{
    if (mask & ITERATE_ITEMS_IN_BAGS)
        IterateItemsInBags(visitor);

    if (mask & ITERATE_ITEMS_IN_EQUIP)
        IterateItemsInEquip(visitor);

    if (mask & ITERATE_ITEMS_IN_BANK)
        IterateItemsInBank(visitor);
}

void PlayerbotFactory::IterateItemsInBags(IterateItemsVisitor* visitor)
{
    for (uint32 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (!visitor->Visit(pItem))
                return;

    for (uint32 i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (!visitor->Visit(pItem))
                return;

    for (uint32 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                if (Item* pItem = pBag->GetItemByPos(j))
                    if (!visitor->Visit(pItem))
                        return;
}

void PlayerbotFactory::IterateItemsInEquip(IterateItemsVisitor* visitor)
{
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; slot++)
    {
        Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!pItem)
            continue;

        if (!visitor->Visit(pItem))
            return;
    }
}

void PlayerbotFactory::IterateItemsInBank(IterateItemsVisitor* visitor)
{
    for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; slot++)
    {
        Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!pItem)
            continue;

        if (!visitor->Visit(pItem))
            return;
    }

    for (uint32 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pBag)
            {
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    if (Item* pItem = pBag->GetItemByPos(j))
                    {
                        if (!pItem)
                            continue;

                        if (!visitor->Visit(pItem))
                            return;
                    }
                }
            }
        }
    }
}

void PlayerbotFactory::InitKeyring()
{
    // The 3.3.5 body handed out the TBC heroic keys (level 70+). Classic 1.60 has neither: no key is handed out
    // here, the V2 bots carry none either.
}
void PlayerbotFactory::InitReputation()
{
    // The 3.3.5 body raised the TBC heroic factions (Lower City, Cenarion Expedition, ...) to Honored for the keys
    // InitKeyring checked. Classic 1.60 has neither the factions nor the keys, so nothing is set.
}

void PlayerbotFactory::InitAttunementQuests()
{
    uint32 level = bot->GetLevel();
    if (level < 55)
        return; // Only apply for level 55+ bots

    uint32 currentXP = bot->GetXP();

    // Complete all level-appropriate attunement quests for the bot
    if (level >= 60)
    {
        std::list<uint32> questsToComplete;

        // Check each quest status before adding to the completion list
        for (uint32 questId : sPlayerbotAIConfig.AttunementQuests)
        {
            QuestStatus questStatus = bot->GetQuestStatus(questId);

            if (questStatus == QUEST_STATUS_NONE) // Quest not yet taken/completed
            {
                questsToComplete.push_back(questId);
            }
        }

        // Only complete quests that haven't been finished yet
        if (!questsToComplete.empty())
        {
            InitQuests(questsToComplete, false);
        }
    }

    // Reset XP so bot's level remains unchanged
    bot->GiveLevel(level);
    bot->SetXP(currentXP);
}
