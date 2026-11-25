/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CheckMountStateAction.h"
#include "DatabaseEnv.h"
#include "StringFormat.h"
#include "DB2Structure.h"
#include "DB2Stores.h"
#include "Event.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotSpellRepository.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "SpellAuraEffects.h"
#include "SpellHistory.h"
#include "SpellPackets.h"
#include "MoveSpline.h"

// 1.6x has no flight (see the header note), so no cold-weather flying spell and
// no parachute threshold: a dismounted bot lands instead of gliding down.

// 1.60 riding tiers: Apprentice (75) from level 40, Journeyman (150) from level 60. The factory
// writes exactly these tiers (PlayerbotFactory::InitSkills), so AI-side checks use the same values.
constexpr uint32 kApprenticeRidingLevel = 40;
constexpr uint32 kJourneymanRidingLevel = 60;

// Preferred mounts per bot, loaded once at startup by LoadPreferredMounts()
std::unordered_map<uint32, PreferredMountCache> CheckMountStateAction::mountCache;

MountData CollectMountData(Player const* bot)
{
    // 12.x keeps only the effects a spell actually defines (vector sized to the last
    // populated slot), while 3.3.5 read the mount speed from the fixed EFFECT_1/EFFECT_2
    // slots. Read whichever slots exist; 1.60 mount data may define neither.
    auto mountSpellSpeed = [](SpellInfo const* spellInfo) -> int32
    {
        std::vector<SpellEffectInfo> const& effects = spellInfo->GetEffects();
        int32 speed = 0;
        if (effects.size() > EFFECT_1)
            speed = std::max(speed, int32(effects[EFFECT_1].BasePoints));
        if (effects.size() > EFFECT_2)
            speed = std::max(speed, int32(effects[EFFECT_2].BasePoints));
        return speed;
    };

    MountData data;
    uint32 const ridingSkill = bot->GetPureSkillValue(SKILL_RIDING);
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!spellInfo || spellInfo->GetEffects().empty() ||
            GetEffectOrDefault(spellInfo, EFFECT_0).ApplyAuraName != SPELL_AURA_MOUNTED)
            continue;

        if (playerSpell.state == PLAYERSPELL_REMOVED || !playerSpell.active || spellInfo->IsPassive())
            continue;

        // The core casts a known mount without checking Riding.
        if (PlayerbotSpellRepository::Instance().GetRequiredRidingSkill(spellInfo) > ridingSkill)
            continue;

        int32 speed = mountSpellSpeed(spellInfo);
        if (speed <= 0)
            continue;  // no readable speed: leave the spell out of the buckets

        // Update max speed if appropriate.
        if (speed > data.maxSpeed)
            data.maxSpeed = speed;  // In BG, clamp max speed to 99 later; here we just store the maximum found.

        // Ground mounts only: mount type 0 is the sole bucket on 1.6x.
        data.allSpells[0][speed].push_back(spellId);
    }
    return data;
}

bool CheckMountStateAction::Execute(Event /*event*/)
{
    // 1.6x ground mounts never leave the ground, so AC's stale-flight-flag and
    // parachute cleanup blocks are not needed.

    // Determine if there are no attackers
    bool noAttackers = !AI_VALUE2(bool, "combat", "self target") || !AI_VALUE(uint8, "attacker count");
    bool enemy = AI_VALUE(Unit*, "enemy player target");
    bool dps = AI_VALUE(Unit*, "dps target");
    bool shouldDismount = false;
    bool shouldMount = false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (currentTarget)
    {
        float dismountDistance = CalculateDismountDistance();
        float mountDistance = CalculateMountDistance();
        float combatReach = bot->GetCombatReach() + currentTarget->GetCombatReach();
        float distanceToTarget = bot->GetExactDist(currentTarget);

        shouldDismount = (distanceToTarget <= dismountDistance + combatReach);
        shouldMount = (distanceToTarget > mountDistance + combatReach);
    }
    else
    {
        shouldMount = true;
    }

    // If should dismount, or master (if any) is no longer in travel form, yet bot still is, remove the shapeshift
    if (shouldDismount ||
        (masterInShapeshiftForm != FORM_TRAVEL_FORM && botInShapeshiftForm == FORM_TRAVEL_FORM))
        botAI->RemoveShapeshift();

    if (shouldDismount && bot->IsMounted())
    {
        Dismount();
        return true;
    }

    bool inBattleground = bot->InBattleground();
    bool const noRealMaster = (!master || master == bot);

    // If there is a master and bot not in BG, follow master's mount state regardless of group leader
    if (!noRealMaster && !inBattleground)
    {
        if (ShouldFollowMasterMountState(master, noAttackers, shouldMount))
            return Mount();

        else if (ShouldDismountForMaster(master) && bot->IsMounted())
        {
            Dismount();
            return true;
        }

        return false;
    }

    // No real master (random bot or SelfBot) OR bot in BG
    if ((noRealMaster || inBattleground) && !bot->IsMounted() &&
        noAttackers && shouldMount && !bot->IsInCombat())
        return Mount();

    if (!bot->IsFlying() && shouldDismount && bot->IsMounted() &&
        (enemy || dps || (!noAttackers && bot->IsInCombat())))
    {
        Dismount();
        return true;
    }

    return false;
}

bool CheckMountStateAction::isUseful()
{
    // Not useful when:
    if (botAI->IsInVehicle() || bot->isDead() || bot->HasUnitState(UNIT_STATE_IN_FLIGHT) ||
        !bot->IsOutdoors() || bot->InArena())
        return false;

    master = GetMaster();

    // Get shapeshift states, only applicable when there's a master
    if (master)
    {
        botInShapeshiftForm = bot->GetShapeshiftForm();
        masterInShapeshiftForm = master->GetShapeshiftForm();
    }

    // Not useful when in combat and not currently mounted / travel formed
    if ((bot->IsInCombat() || botAI->GetState() == BOT_STATE_COMBAT) &&
        !bot->IsMounted() && botInShapeshiftForm != FORM_TRAVEL_FORM)
        return false;

    // In addition to checking IsOutdoors, also check whether bot is clipping below floor slightly because that will
    // cause bot to falsly indicate they are outdoors. This fixes bug where bot tries to mount indoors (which seems
    // to mostly be an issue in tunnels of WSG and AV)
    float posZ = bot->GetPositionZ();
    float groundLevel = bot->GetMapWaterOrGroundLevel(bot->GetPositionX(), bot->GetPositionY(), posZ);
    if (!bot->IsMounted() && !bot->HasAuraType(SPELL_AURA_WATER_WALK) && posZ < groundLevel)
        return false;

    // Not useful when bot does not have mount strat and is not currently mounted
    if (!GET_PLAYERBOT_AI(bot)->HasStrategy("mount", BOT_STATE_NON_COMBAT) && !bot->IsMounted())
        return false;

    // Not useful when level lower than minimum required
    if (bot->GetLevel() < kApprenticeRidingLevel)
        return false;

    // Allow mounting while transformed only if the form allows it
    if (bot->HasAuraType(SPELL_AURA_TRANSFORM) && bot->IsInDisallowedMountForm())
        return false;

    // Mount attempts stop movement, including active jumps.
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == EFFECT_MOTION_TYPE && bot->movespline &&
        !bot->movespline->Finalized())
        return false;

    return true;
}

bool CheckMountStateAction::Mount()
{
    // Remove current Shapeshift if need be
    if (botInShapeshiftForm != FORM_TRAVEL_FORM)
    {
        botAI->RemoveShapeshift();
        botAI->RemoveAura("tree of life");
    }

    if (TryPreferredMount(master))
        return true;

    // Get bot mount data
    MountData mountData = CollectMountData(bot);
    int32 masterMountType = GetMountType(master);
    int32 masterSpeed = CalculateMasterMountSpeed(master);

    // Try shapeshift
    if (TryForms(master, masterMountType, masterSpeed))
        return true;

    // Try random mount
    auto spellsIt = mountData.allSpells.find(masterMountType);
    if (spellsIt != mountData.allSpells.end())
    {
        auto& spells = spellsIt->second;
        if (TryRandomMountFiltered(spells, masterSpeed))
            return true;
    }

    std::vector<Item*> items = AI_VALUE2(std::vector<Item*>, "inventory items", "mount");
    if (!items.empty())
        return UseItemAuto(*items.begin());

    return false;
}

void CheckMountStateAction::Dismount()
{
    if (bot->isMoving())
        bot->StopMoving();

    WorldPacket payload(CMSG_CANCEL_MOUNT_AURA, 0);
    WorldPackets::Spells::CancelMountAura cancelMount(std::move(payload));
    bot->GetSession()->HandleCancelMountAuraOpcode(cancelMount);
}

void CheckMountStateAction::CompleteDismount(Player* bot)
{
    if (!bot || !bot->IsInWorld())
        return;

    float const x = bot->GetPositionX();
    float const y = bot->GetPositionY();
    float const startZ = bot->GetPositionZ();

    float groundZ = startZ;
    bot->UpdateAllowedPositionZ(x, y, groundZ);

    bot->GetMotionMaster()->MoveFall();
    // Anchor the fall at the ground before letting the core resolve it: a start Z
    // taken from above the floor reads as a fall and would deal fall damage.
    bot->SetFallInformation(0, groundZ);
    bot->HandleFall();
    bot->RemoveUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
}

bool CheckMountStateAction::TryForms(Player* master, int32 /*masterMountType*/, int32 /*masterSpeed*/) const
{
    if (!master)
        return false;

    // No flight forms on 1.6x: the only shapeshift that substitutes for a mount
    // is the druid's ground travel form.
    if (masterInShapeshiftForm == FORM_TRAVEL_FORM && botInShapeshiftForm == FORM_TRAVEL_FORM)
        return true;

    if (botAI->CanCastSpell(SPELL_TRAVEL_FORM, bot, true) &&
        masterInShapeshiftForm == FORM_TRAVEL_FORM && botInShapeshiftForm != FORM_TRAVEL_FORM)
    {
        botAI->CastSpell(SPELL_TRAVEL_FORM, bot);
        return true;
    }

    return false;
}

void CheckMountStateAction::LoadPreferredMounts()
{
    mountCache.clear();

    QueryResult result = WorldDatabase.Query("SELECT guid, spellid, type FROM playerbots_preferred_mounts");
    if (!result)
        return;

    uint32 totalResults = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 guid = PlayerbotFieldGet<uint32>(fields[0]);
        uint32 spellId = PlayerbotFieldGet<uint32>(fields[1]);
        uint32 mountType = PlayerbotFieldGet<uint32>(fields[2]);

        // 1.6x has ground mounts only; a row tagged as a flying mount is ignored.
        if (mountType == 0)
            mountCache[guid].groundMounts.push_back(spellId);

        totalResults++;
    } while (result->NextRow());

    TC_LOG_INFO("playerbots", "Preferred mounts initialized | Total records: {}", totalResults);
}

bool CheckMountStateAction::TryPreferredMount(Player* master) const
{
    uint32 botGUID = uint32(bot->GetGUID().GetCounter());

    // mountCache is filled once on the world thread at startup and only read afterwards
    auto itr = mountCache.find(botGUID);
    if (itr == mountCache.end())
        return false;

    PreferredMountCache const& preferred = itr->second;

    // Pick a random preferred mount from the selection, if available
    uint32 chosenMountId = 0;

    // Ground mounts only (1.6x has no flight).
    if (!preferred.groundMounts.empty())
    {
        uint32 index = urand(0, preferred.groundMounts.size() - 1);
        chosenMountId = preferred.groundMounts[index];
    }

    // No suitable preferred mount found
    if (chosenMountId == 0)
        return false;

    // Check if spell exists
    if (!sSpellMgr->GetSpellInfo(chosenMountId, DIFFICULTY_NONE))
    {
        TC_LOG_ERROR("playerbots", "Preferred mount failed: Invalid spell {} | Bot Guid: {}", chosenMountId, botGUID);
        return false;
    }

    // Required here as otherwise bots won't mount in BG's due to them constant moving
    if (bot->isMoving())
        bot->StopMoving();

    // Check if spell can be cast - for now allow all, even if the bot does not have the actual mount
    //if (botAI->CanCastSpell(mountId, botAI->GetBot()))
    //{
    botAI->CastSpell(chosenMountId, botAI->GetBot());
    return true;
    //}

    TC_LOG_DEBUG("playerbots", "Preferred mount failed! | Bot Guid: {}", botGUID);
    return false;
}

bool CheckMountStateAction::TryRandomMountFiltered(std::map<int32, std::vector<uint32>> const& spells, int32 masterSpeed) const
{
    for (auto it = spells.rbegin(); it != spells.rend(); ++it)
    {
        int32 currentSpeed = it->first;

        // Ground tiers only: a master at 100% speed wants a 100% mount, not a 60% one.
        if (masterSpeed > 59 && currentSpeed < 99)
            continue;

        // Pick a random mount from the candidate group.
        auto const& ids = it->second;
        if (!ids.empty())
        {
            // Required here as otherwise bots won't mount in BGs due to them constant moving
            if (bot->isMoving())
                bot->StopMoving();

            uint32 index = urand(0, ids.size() - 1);

            if (botAI->CanCastSpell(ids[index], bot))
            {
                botAI->CastSpell(ids[index], bot);
                return true;
            }
        }
    }
    return false;
}

float CheckMountStateAction::CalculateDismountDistance() const
{
    // Warrior bots should dismount far enough to charge (because it's important for generating some initial rage),
    // a real player would be riding toward enemy mashing the charge key but the bots won't cast charge while mounted.
    bool isMelee = PlayerbotAI::IsMelee(bot);
    float dismountDistance = isMelee ? sPlayerbotAIConfig.MeleeDistance + 2.0f : sPlayerbotAIConfig.SpellDistance + 2.0f;
    return bot->GetClass() == CLASS_WARRIOR ? std::max(18.0f, dismountDistance) : dismountDistance;
}

float CheckMountStateAction::CalculateMountDistance() const
{
    // Mount distance should be >= 21 regardless of class, because when travelling a distance < 21 it takes longer
    // to cast mount-spell than the time saved from the speed increase. At a distance of 21 both approaches take 3
    // seconds:
    // 21 / 7  =  21 / 14 + 1.5  =  3   (7 = dismounted speed  14 = epic-mount speed  1.5 = mount-spell cast time)
    bool isMelee = PlayerbotAI::IsMelee(bot);
    float baseDistance = isMelee ? sPlayerbotAIConfig.MeleeDistance + 10.0f : sPlayerbotAIConfig.SpellDistance + 10.0f;
    return std::max(21.0f, baseDistance);
}

bool CheckMountStateAction::ShouldFollowMasterMountState(Player* master, bool noAttackers, bool shouldMount) const
{
    bool isMasterMounted = master->IsMounted() || masterInShapeshiftForm == FORM_TRAVEL_FORM;
    bool isBotMountedOrForm = bot->IsMounted() || botInShapeshiftForm == FORM_TRAVEL_FORM;
    return isMasterMounted && !isBotMountedOrForm && noAttackers &&
        shouldMount && !bot->IsInCombat() && botAI->GetState() != BOT_STATE_COMBAT;
}

bool CheckMountStateAction::ShouldDismountForMaster(Player* master) const
{
    bool isMasterMounted = master->IsMounted() || masterInShapeshiftForm == FORM_TRAVEL_FORM;
    return !isMasterMounted && bot->IsMounted();
}

int32 CheckMountStateAction::CalculateMasterMountSpeed(Player* master) const
{
    // Ground tiers only: 59 = walk-pace mount (60%), 99 = fast ground mount (100%).
    int32 ridingSkill = bot->GetPureSkillValue(SKILL_RIDING);
    int32 botLevel = bot->GetLevel();

    if (ridingSkill <= 75 && botLevel < static_cast<int32>(kJourneymanRidingLevel))
        return 59;

    // check if bot has master and if master is self
    bool const noRealMaster = (!master || master == bot);

    if (!noRealMaster && !bot->InBattleground())
    {
        auto auraEffects = master->GetAuraEffectsByType(SPELL_AURA_MOUNTED);
        if (!auraEffects.empty())
        {
            SpellInfo const* masterSpell = auraEffects.front()->GetSpellInfo();
            std::vector<SpellEffectInfo> const& effects = masterSpell->GetEffects();
            int32 speed = 0;
            if (effects.size() > EFFECT_1)
                speed = std::max(speed, int32(effects[EFFECT_1].BasePoints));
            if (effects.size() > EFFECT_2)
                speed = std::max(speed, int32(effects[EFFECT_2].BasePoints));
            // 1.60 mount data may not carry a speed effect; fall back to walk pace.
            return speed > 0 ? speed : 59;
        }
        if (masterInShapeshiftForm == FORM_TRAVEL_FORM)
            return 59;
        return 59;  // walk pace
    }

    int32 maxGround = (ridingSkill >= 150) ? 99 : 59;
    if (bot->InBattleground() && maxGround > 99)
        maxGround = 99;
    return maxGround;
}

uint32 CheckMountStateAction::GetMountType(Player* /*master*/) const
{
    // 1.6x has ground mounts only; the return value stays 0 so the callers keep
    // their "mount type -> spell bucket" shape.
    return 0;
}
