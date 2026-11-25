/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NearestGameObjects.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "SpellMgr.h"

GuidVector NearestGameObjects::Calculate()
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, range);
    Trinity::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);

    GuidVector result;
    for (GameObject* go : targets)
    {
        // if (ignoreLos || bot->IsWithinLOSInMap(go))
        result.push_back(go->GetGUID());
    }

    return result;
}

GuidVector NearestTrapWithDamageValue::Calculate()
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, range);
    Trinity::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);

    GuidVector result;
    for (GameObject* go : targets)
    {
        if (go->GetGoType() != GAMEOBJECT_TYPE_TRAP)
        {
            continue;
        }
        Unit* owner = go->GetOwner();
        if (owner && owner->IsFriendlyTo(bot))
        {
            continue;
        }
        GameObjectTemplate const* goInfo = go->GetGOInfo();
        if (!goInfo)
        {
            continue;
        }
        uint32 spellId = go->GetSpellId();
        if (!spellId)
        {
            continue;
        }
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!spellInfo || spellInfo->IsPositive())
        {
            continue;
        }
        for (int i = 0; i < int(spellInfo->GetEffects().size()); i++)
        {
            if (GetEffectOrDefault(spellInfo, SpellEffIndex(i)).Effect == SPELL_EFFECT_APPLY_AURA)
            {
                if (GetEffectOrDefault(spellInfo, SpellEffIndex(i)).ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE)
                {
                    result.push_back(go->GetGUID());
                    break;
                }
            }
            else if (GetEffectOrDefault(spellInfo, SpellEffIndex(i)).Effect == SPELL_EFFECT_SCHOOL_DAMAGE)
            {
                result.push_back(go->GetGUID());
                break;
            }
        }
    }
    return result;
}
