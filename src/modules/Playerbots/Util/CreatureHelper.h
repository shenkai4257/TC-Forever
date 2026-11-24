/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CREATUREHELPER_H
#define PLAYERBOTS_CREATUREHELPER_H

#include "CreatureData.h"
#include "ObjectMgr.h"

#include <utility>

// 3.3.5 kept loot ids on creature_template itself; 12.x moved them into the per-difficulty
// creature_template_difficulty rows. These helpers read the base difficulty.
// 3.3.5 creature rank values. 12.x renamed the field to CreatureClassifications with the
// same value order (3 is named Obsolete there); the ported code compares the numbers, so keep
// the old names and read the field as a number.
constexpr uint32 CREATURE_ELITE_NORMAL    = 0;
constexpr uint32 CREATURE_ELITE_ELITE     = 1;
constexpr uint32 CREATURE_ELITE_RAREELITE = 2;
constexpr uint32 CREATURE_ELITE_WORLDBOSS = 3;
constexpr uint32 CREATURE_ELITE_RARE      = 4;

namespace PlayerbotCreature
{
    inline uint32 GetRank(CreatureTemplate const* creatureTemplate)
    {
        return creatureTemplate ? uint32(creatureTemplate->Classification) : 0;
    }

    inline CreatureDifficulty const* GetBaseDifficulty(CreatureTemplate const* creatureTemplate)
    {
        return creatureTemplate ? creatureTemplate->GetDifficulty(DIFFICULTY_NONE) : nullptr;
    }

    // Classic 1.60 creature level range: 12.x removed minlevel/maxlevel from the template;
    // the fork keeps the vanilla ranges in ObjectMgr (creature_template_difficulty/classic data).
    inline std::pair<uint8, uint8> GetClassicLevelRange(CreatureTemplate const* creatureTemplate)
    {
        if (!creatureTemplate)
            return { 0, 0 };

        if (std::pair<uint8, uint8> const* levels = sObjectMgr->GetCreatureClassicLevel(creatureTemplate->Entry))
            return *levels;

        return { 0, 0 };
    }

    inline uint32 GetLootId(CreatureTemplate const* creatureTemplate)
    {
        CreatureDifficulty const* difficulty = GetBaseDifficulty(creatureTemplate);
        return difficulty ? difficulty->LootID : 0;
    }

    inline uint32 GetPickPocketLootId(CreatureTemplate const* creatureTemplate)
    {
        CreatureDifficulty const* difficulty = GetBaseDifficulty(creatureTemplate);
        return difficulty ? difficulty->PickPocketLootID : 0;
    }

    inline uint32 GetSkinLootId(CreatureTemplate const* creatureTemplate)
    {
        CreatureDifficulty const* difficulty = GetBaseDifficulty(creatureTemplate);
        return difficulty ? difficulty->SkinLootID : 0;
    }
}

#endif
