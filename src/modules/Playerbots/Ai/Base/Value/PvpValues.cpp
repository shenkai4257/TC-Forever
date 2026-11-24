/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PvpValues.h"

#include <algorithm>

#include "BattleGroundTactics.h"
#include "BattlegroundMgr.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "ServerFacade.h"

bool IsUnavailableCombatTarget(Player const* player, Unit const* target)
{
    return !player || !target || !target->IsInWorld() || target->GetMap() != player->GetMap() || !target->IsAlive() ||
           target->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION) ||
           (target->IsPlayer() && target->ToPlayer()->HasPlayerFlag(PLAYER_FLAGS_GHOST));
}

bool IsValidWsgCarrier(Player const* player, Unit const* carrier, bool friendly)
{
    // 12.x keeps WSG flag state inside the battleground scripts; not reachable here.
    return false;
}

bool CanRespondToWsgCarrier(PlayerbotAI* botAI, Unit* carrier)
{
    // 12.x keeps WSG flag state inside the battleground scripts; not reachable here.
    return false;
}

bool IsWsgEscortTargetAllowed(PlayerbotAI* botAI, Unit const* target)
{
    // 12.x keeps WSG flag state inside the battleground scripts; not reachable here.
    return false;
}

bool ShouldRegroupWsgEscort(PlayerbotAI* botAI)
{
    // 12.x keeps WSG flag state inside the battleground scripts; not reachable here.
    return false;
}

bool IsWsgSupportTarget(PlayerbotAI* botAI, Unit const* target)
{
    // 12.x keeps WSG flag state inside the battleground scripts; not reachable here.
    return false;
}

WsgTeamAssignment WsgTeamAssignmentValue::Calculate()
{
    // 12.x keeps WSG roles inside the battleground scripts; the module has no view of them.
    return WsgTeamAssignment{};
}

ObjectGuid WsgSupportTargetValue::Calculate()
{
    return ObjectGuid::Empty;
}

namespace
{
// a game from the random queue records BATTLEGROUND_RB as the player's type: use the rolled map
BattlegroundTypeId RealBgType(Player* bot)
{
    BattlegroundTypeId bgType = bot->GetBattlegroundTypeId();
    if (bgType == BATTLEGROUND_RB && bot->GetBattleground())
        bgType = bot->GetBattleground()->GetTypeID();
    return bgType;
}
}  // namespace

Unit* FlagCarrierValue::Calculate()
{
    // 12.x does not expose the WSG flag carrier to the module.
    return nullptr;
}

std::vector<CreatureData const*> BgMastersValue::Calculate()
{
    BattlegroundTypeId bgTypeId = (BattlegroundTypeId)stoi(qualifier);

    std::vector<uint32> entries;
    std::map<TeamId, std::map<BattlegroundTypeId, std::vector<uint32>>> battleMastersCache =
        sRandomPlayerbotMgr.getBattleMastersCache();
    entries.insert(entries.end(), battleMastersCache[TEAM_NEUTRAL][bgTypeId].begin(),
                   battleMastersCache[TEAM_NEUTRAL][bgTypeId].end());
    entries.insert(entries.end(), battleMastersCache[TEAM_ALLIANCE][bgTypeId].begin(),
                   battleMastersCache[TEAM_ALLIANCE][bgTypeId].end());
    entries.insert(entries.end(), battleMastersCache[TEAM_HORDE][bgTypeId].begin(),
                   battleMastersCache[TEAM_HORDE][bgTypeId].end());

    std::vector<CreatureData const*> bmGuids;

    for (auto entry : entries)
    {
        for (auto creaturePair : WorldPosition().getCreaturesNear(0, entry))
        {
            bmGuids.push_back(creaturePair);
        }
    }

    return bmGuids;
}

CreatureData const* BgMasterValue::Calculate()
{
    CreatureData const* bmPair = NearestBm(false);
    if (!bmPair)
        bmPair = NearestBm(true);

    return bmPair;
}

CreatureData const* BgMasterValue::NearestBm(bool allowDead)
{
    WorldPosition botPos(bot);

    std::vector<CreatureData const*> bmPairs = AI_VALUE2(std::vector<CreatureData const*>, "bg masters", qualifier);

    float rDist = 0.0f;
    CreatureData const* rbmPair = nullptr;

    for (auto& bmPair : bmPairs)
    {
        if (!bmPair)
            continue;

        WorldPosition bmPos(bmPair->mapId, bmPair->spawnPoint.GetPositionX(), bmPair->spawnPoint.GetPositionY(),
                               bmPair->spawnPoint.GetPositionZ(), bmPair->spawnPoint.GetOrientation());

        float dist = botPos.distance(bmPos);  // This is the aproximate travel distance.

        // Did we already find a closer unit that is not dead?
        if (rbmPair && rDist <= dist)
            continue;

        CreatureTemplate const* bmTemplate = sObjectMgr->GetCreatureTemplate(bmPair->id);
        if (!bmTemplate)
            continue;

        FactionTemplateEntry const* bmFactionEntry = sFactionTemplateStore.LookupEntry(bmTemplate->faction);

        // Is the unit hostile?
        if (bot->GetFactionTemplateEntry()->IsFriendlyTo(bmFactionEntry))
            continue;

        AreaTableEntry const* area = bmPos.getArea();

        if (!area)
            continue;

        // Is the area hostile? 12.x keeps the faction mask on the area data.
        if ((area->FactionGroupMask & FACTION_MASK_ALLIANCE) && bot->GetTeamId() == TEAM_ALLIANCE)
            continue;
        if ((area->FactionGroupMask & FACTION_MASK_HORDE) && bot->GetTeamId() == TEAM_HORDE)
            continue;

        if (!allowDead)
        {
            Unit* unit = botAI->GetUnit(bmPair);

            if (!unit)
                continue;

            // Is the unit dead?
            if (unit->getDeathState() == DEAD)
                continue;
        }

        rbmPair = bmPair;
        rDist = dist;
    }

    return rbmPair;
}

BattlegroundTypeId RpgBgTypeValue::Calculate()
{
    GuidPosition guidPosition = AI_VALUE(GuidPosition, "rpg target");

    if (guidPosition)
        // 1.60 battlegrounds are AV, WSG and AB only.
        for (BattlegroundTypeId bgTypeId : { BATTLEGROUND_AV, BATTLEGROUND_WS, BATTLEGROUND_AB })
        {
            // 12.x queues are addressed by packed ids; derive the queue from the template.
            BattlegroundTemplate const* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(bgTypeId);
            if (!bgTemplate || !bgTemplate->BattlemasterEntry)
                continue;

            if (bot->GetLevel() < bgTemplate->BattlemasterEntry->MinLevel)
                continue;

            BattlegroundQueueTypeId queueTypeId = sBattlegroundMgr->BGQueueTypeId(
                uint16(bgTemplate->Id), BattlegroundQueueIdType::Battleground, false, 0);

            // check if already in queue
            if (bot->InBattlegroundQueueForBattlegroundQueueType(queueTypeId))
                continue;

            std::map<TeamId, std::map<BattlegroundTypeId, std::vector<uint32>>> battleMastersCache =
                sRandomPlayerbotMgr.getBattleMastersCache();

            for (auto& entry : battleMastersCache[TEAM_NEUTRAL][bgTypeId])
                if (entry == guidPosition.GetEntry())
                    return bgTypeId;

            for (auto& entry : battleMastersCache[bot->GetTeamId()][bgTypeId])
                if (entry == guidPosition.GetEntry())
                    return bgTypeId;
        }

    return BATTLEGROUND_TYPE_NONE;
}
