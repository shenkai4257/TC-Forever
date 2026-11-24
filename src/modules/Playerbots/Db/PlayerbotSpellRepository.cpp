/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotSpellRepository.h"
#include "FieldHelper.h"

#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QueryResult.h"  // Required due to a poor implementation by AC
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

static constexpr uint32 SPELL_APPRENTICE_RIDING = 33388;
static constexpr uint32 SPELL_JOURNEYMAN_RIDING = 33391;
static constexpr uint32 SPELL_EXPERT_RIDING = 34090;
static constexpr uint32 SPELL_ARTISAN_RIDING = 34091;
static constexpr uint32 RIDING_RANK_APPRENTICE = 75;
static constexpr uint32 RIDING_RANK_JOURNEYMAN = 150;
static constexpr uint32 RIDING_RANK_EXPERT = 225;
static constexpr uint32 RIDING_RANK_ARTISAN = 300;
// Speed auras store the bonus minus one: 99 is 100%, 279 is 280%.
static constexpr int32 FAST_GROUND_MOUNT_SPEED = 99;
static constexpr int32 FAST_FLYING_MOUNT_SPEED = 279;

// Highest first, so a mount that lists several Riding spells gets the highest rank.
static constexpr std::array<std::pair<uint32, uint32>, 4> RIDING_SPELL_RANKS = {{
    {SPELL_ARTISAN_RIDING, RIDING_RANK_ARTISAN},
    {SPELL_EXPERT_RIDING, RIDING_RANK_EXPERT},
    {SPELL_JOURNEYMAN_RIDING, RIDING_RANK_JOURNEYMAN},
    {SPELL_APPRENTICE_RIDING, RIDING_RANK_APPRENTICE},
}};

//  caches the result set
void PlayerbotSpellRepository::Initialize()
{
    TC_LOG_INFO("playerbots", "Playerbots: ListSpellsAction caches initialized");

    // Mount spells carry no Riding requirement; the item that teaches them does.
    for (auto const& [itemId, itemTemplate] : sObjectMgr->GetItemTemplateStore())
    {
        if (itemTemplate.GetRequiredSkill() != SKILL_RIDING)
            continue;

        // 12.x keeps a compressed effect list instead of the 3.3.5 fixed spell slots.
        for (ItemEffectEntry const* itemSpell : itemTemplate.Effects)
        {
            if (!itemSpell || itemSpell->SpellID <= 0 || itemSpell->TriggerType != ITEM_SPELLTRIGGER_ON_LEARN)
                continue;

            auto [itr, inserted] =
                _ridingSkillByMount.try_emplace(uint32(itemSpell->SpellID), itemTemplate.GetRequiredSkillRank());
            if (!inserted)
                itr->second = std::min(itr->second, itemTemplate.GetRequiredSkillRank());
        }
    }

    // Fast class mounts are taught by no item; spell_required lists their Riding spell.
    for (auto const& [ridingSpell, rank] : RIDING_SPELL_RANKS)
    {
        SpellsRequiringSpellMapBounds const bounds = sSpellMgr->GetSpellsRequiringSpellBounds(ridingSpell);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            _ridingSkillByMount.try_emplace(itr->second, rank);
    }

    std::unordered_map<uint32, std::vector<SkillLineAbilityEntry const*>> abilitiesBySkill;
    for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
    {
        SkillLineAbilityEntry const* skillLine = sSkillLineAbilityStore.LookupEntry(j);
        if (!skillLine)
            continue;

        skillSpells[skillLine->Spell] = skillLine;

        // Only linkable professions have a profession link.
        SkillLineEntry const* skill = sSkillLineStore.LookupEntry(skillLine->SkillLine);
        if (skill && skill->CanLink)
            abilitiesBySkill[skillLine->SkillLine].push_back(skillLine);
    }

    InitTradeSkillLinks(abilitiesBySkill);

    // TEMP 1.60 skill dump for fitting the class rotations (remove once the APLs are written).
    // The 12.x client data has no all-spell iterator, so the skill-line cache above is the complete
    // list of the spells the client knows: one line per spell with its skill line and category.
    if (FILE* dump = fopen("/home/shenk/tools/backups/spell_dump_160.tsv", "w"))
    {
        for (auto const& [spellId, ability] : skillSpells)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
            if (!spellInfo || !spellInfo->SpellName)
                continue;

            // The 1.60 zhCN client leaves Str[LOCALE_enUS] empty for many spells, and that is the
            // locale SpellIdValue matches action names against: dump both so the missing English
            // names show up here instead of as silently dead actions.
            char const* en = spellInfo->SpellName->Str[LOCALE_enUS];
            char const* zh = spellInfo->SpellName->Str[LOCALE_zhCN];
            if ((!en || !*en) && (!zh || !*zh))
                continue;

            SkillLineEntry const* line = sSkillLineStore.LookupEntry(ability->SkillLine);
            fprintf(dump, "%u\t%u\t%d\t%s\t%s\n", spellId, uint32(ability->SkillLine),
                    line ? int32(line->CategoryID) : -1, en ? en : "", zh ? zh : "");
        }
        fclose(dump);
        TC_LOG_INFO("playerbots", "1.60 spell dump written to /home/shenk/tools/backups/spell_dump_160.tsv");
    }

    // 1.60 action-name table (see GetSpellIdByName): the zhCN client data carries almost no enUS
    // spell names, so the English names the strategies use are mapped to their lowest-rank ids here
    // and SpellIdValue walks the chain up to the rank the bot actually holds. Warrior first; the
    // other classes are filled in as their rotation is fitted.
    static constexpr std::pair<char const*, uint32> ACTION_SPELL_IDS[] = {
        {"heroic strike", 78},       {"charge", 100},         {"rend", 772},          {"thunder clap", 6343},
        {"hamstring", 1715},         {"pummel", 6552},        {"overpower", 7384},    {"execute", 5308},
        {"slam", 1464},              {"whirlwind", 1680},     {"cleave", 845},        {"sunder armor", 7386},
        {"mocking blow", 694},       {"battle shout", 5242},  {"demoralizing shout", 1160}, {"challenging shout", 1161},
        {"intimidating shout", 5246},{"berserker rage", 18499},{"bloodrage", 2687},   {"shield bash", 72},
        {"shield block", 2565},      {"shield wall", 871},    {"shield slam", 23922}, {"revenge", 6572},
        {"disarm", 676},             {"taunt", 355},          {"intercept", 20252},   {"retaliation", 20230},
        {"recklessness", 1719},      {"death wish", 12328},   {"sweeping strikes", 12292}, {"mortal strike", 12294},
        {"bloodthirst", 23881},      {"concussion blow", 12809}, {"piercing howl", 12323}, {"victory rush", 402927},
        {"enraged regeneration", 402913}, {"shockwave", 440488}, {"devastate", 403196}, {"commanding shout", 403215},
        {"last stand", 12975},       {"battle stance", 2457}, {"defensive stance", 71}, {"berserker stance", 2458},
    };
    _spellIdByName.clear();
    for (auto const& [name, spellId] : ACTION_SPELL_IDS)
        _spellIdByName.emplace(name, spellId);

    // 12.x exposes no "all spells" iterator; opening and skinning spells are always taught
    // through a skill line, so walk the skill-line spell cache built above instead.
    for (auto const& [spellId, ability] : skillSpells)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!spellInfo)
            continue;

        for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
        {
            if ((spellEffectInfo.Effect == SPELL_EFFECT_OPEN_LOCK || spellEffectInfo.Effect == SPELL_EFFECT_SKINNING) &&
                spellEffectInfo.MiscValue)
                _openingSpells[uint32(spellEffectInfo.MiscValue)].push_back(spellId);
        }
    }

    // Fill the vendorItems cache once from the world database.
    QueryResult results = WorldDatabase.PQuery("SELECT item FROM npc_vendor WHERE maxcount = 0");
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            int32 entry = PlayerbotFieldGet<int32>(fields[0]);
            if (entry <= 0)
                continue;

            vendorItems.insert(static_cast<uint32>(entry));
        } while (results->NextRow());
    }

    TC_LOG_DEBUG("playerbots", "ListSpellsAction: initialized caches (skillSpells={}, vendorItems={}).",
              skillSpells.size(), vendorItems.size());
}

// The client numbers link bits in DBC file order, which the stock file sorts by MinSkillLineRank and then row ID.
void PlayerbotSpellRepository::InitTradeSkillLinks(
    std::unordered_map<uint32, std::vector<SkillLineAbilityEntry const*>>& abilitiesBySkill)
{
    for (auto& [skillId, abilities] : abilitiesBySkill)
    {
        std::sort(abilities.begin(), abilities.end(),
                  [](SkillLineAbilityEntry const* lhs, SkillLineAbilityEntry const* rhs)
                  {
                      if (lhs->MinSkillLineRank != rhs->MinSkillLineRank)
                          return lhs->MinSkillLineRank < rhs->MinSkillLineRank;
                      return lhs->ID < rhs->ID;
                  });

        TradeSkillLinkData data{0, {}};
        data.entries.reserve(abilities.size());
        for (SkillLineAbilityEntry const* ability : abilities)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(ability->Spell, DIFFICULTY_NONE);
            if (!data.tradeSpellId && spellInfo && !spellInfo->GetEffects().empty() &&
                spellInfo->GetEffectOrDefault(EFFECT_0).Effect == SPELL_EFFECT_TRADE_SKILL)
                data.tradeSpellId = ability->Spell;

            bool bindOnPickup = false;
            if (spellInfo)
            {
                for (SpellEffectInfo const& effect : spellInfo->GetEffects())
                {
                    if (effect.Effect != SPELL_EFFECT_CREATE_ITEM || !effect.ItemType)
                        continue;

                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(effect.ItemType);
                    if (proto && proto->GetBonding() == BIND_ON_ACQUIRE)
                        bindOnPickup = true;
                }
            }

            data.entries.push_back({uint32_t(ability->Spell), uint32_t(ability->RaceMask.RawValue[0]),
                                   uint32_t(ability->ClassMask), bindOnPickup});
        }

        if (data.tradeSpellId)
            _tradeSkillLinks.emplace(skillId, std::move(data));
    }
}

TradeSkillLinkData const* PlayerbotSpellRepository::GetTradeSkillLinkData(uint32 skillId) const
{
    auto const itr = _tradeSkillLinks.find(skillId);
    return itr != _tradeSkillLinks.end() ? &itr->second : nullptr;
}

uint32 PlayerbotSpellRepository::GetSpellIdByName(std::string const& name) const
{
    auto const itr = _spellIdByName.find(name);
    return itr != _spellIdByName.end() ? itr->second : 0;
}

SkillLineAbilityEntry const* PlayerbotSpellRepository::GetSkillLine(uint32 spellId) const
{
    auto itr = skillSpells.find(spellId);
    if (itr != skillSpells.end())
        return itr->second;
    return nullptr;
}

bool PlayerbotSpellRepository::IsItemBuyable(uint32 itemId) const
{
    return vendorItems.find(itemId) != vendorItems.end();
}

std::vector<uint32> const& PlayerbotSpellRepository::GetOpeningSpells(uint32 lockType) const
{
    static std::vector<uint32> const empty;
    auto const itr = _openingSpells.find(lockType);
    return itr != _openingSpells.end() ? itr->second : empty;
}

uint32 PlayerbotSpellRepository::GetRequiredRidingSkill(SpellInfo const* mountSpell) const
{
    auto const itr = _ridingSkillByMount.find(mountSpell->Id);
    if (itr != _ridingSkillByMount.end())
        return itr->second;

    // No item or spell_required row (Acherus Deathcharger, slow class and quest mounts): rate by speed.
    int32 speed = 0;
    bool flying = false;
    for (SpellEffectInfo const& effect : mountSpell->GetEffects())
    {
        switch (effect.ApplyAuraName)
        {
            case SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED:
            case SPELL_AURA_MOD_MOUNTED_FLIGHT_SPEED_ALWAYS:
                flying = true;
                speed = std::max<int32>(speed, int32(effect.BasePoints));
                break;
            case SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED:
            case SPELL_AURA_MOD_MOUNTED_SPEED_ALWAYS:
            case SPELL_AURA_MOD_MOUNTED_SPEED_NOT_STACK:
                speed = std::max<int32>(speed, int32(effect.BasePoints));
                break;
            default:
                break;
        }
    }

    if (speed <= 0)
        return 0;

    if (flying)
        return speed >= FAST_FLYING_MOUNT_SPEED ? RIDING_RANK_ARTISAN : RIDING_RANK_EXPERT;

    return speed >= FAST_GROUND_MOUNT_SPEED ? RIDING_RANK_JOURNEYMAN : RIDING_RANK_APPRENTICE;
}
