/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MageAiObjectContext.h"
#include "ArcaneMageStrategy.h"
#include "FireMageStrategy.h"
#include "FrostMageStrategy.h"
#include "GenericMageNonCombatStrategy.h"
#include "MageActions.h"
#include "MageTriggers.h"
#include "NamedObjectContext.h"
#include "Playerbots.h"
#include "PullStrategy.h"

class MageStrategyFactoryInternal : public NamedObjectContext<Strategy>
{
public:
    MageStrategyFactoryInternal()
    {
        creators["nc"] = &MageStrategyFactoryInternal::nc;
        creators["pull"] = &MageStrategyFactoryInternal::pull;
        creators["aoe"] = &MageStrategyFactoryInternal::aoe;
        creators["cure"] = &MageStrategyFactoryInternal::cure;
        creators["buff"] = &MageStrategyFactoryInternal::buff;
        creators["boost"] = &MageStrategyFactoryInternal::boost;
        creators["cc"] = &MageStrategyFactoryInternal::cc;
        creators["firestarter"] = &MageStrategyFactoryInternal::firestarter;
    }

private:
    static Strategy* nc(PlayerbotAI* botAI) { return new GenericMageNonCombatStrategy(botAI); }
    static Strategy* pull(PlayerbotAI* botAI) { return new PullStrategy(botAI, "shoot"); }
    static Strategy* aoe(PlayerbotAI* botAI) { return new MageAoeStrategy(botAI); }
    static Strategy* cure(PlayerbotAI* botAI) { return new MageCureStrategy(botAI); }
    static Strategy* buff(PlayerbotAI* botAI) { return new MageBuffStrategy(botAI); }
    static Strategy* boost(PlayerbotAI* botAI) { return new MageBoostStrategy(botAI); }
    static Strategy* cc(PlayerbotAI* botAI) { return new MageCcStrategy(botAI); }
    static Strategy* firestarter(PlayerbotAI* botAI) { return new FirestarterStrategy(botAI); }
};

class MageCombatStrategyFactoryInternal : public NamedObjectContext<Strategy>
{
public:
    MageCombatStrategyFactoryInternal() : NamedObjectContext<Strategy>(false, true)
    {
        creators["frost"] = &MageCombatStrategyFactoryInternal::frost;
        creators["fire"] = &MageCombatStrategyFactoryInternal::fire;
        creators["arcane"] = &MageCombatStrategyFactoryInternal::arcane;
    }

private:
    static Strategy* frost(PlayerbotAI* botAI) { return new FrostMageStrategy(botAI); }
    static Strategy* fire(PlayerbotAI* botAI) { return new FireMageStrategy(botAI); }
    static Strategy* arcane(PlayerbotAI* botAI) { return new ArcaneMageStrategy(botAI); }
};

class MageBuffStrategyFactoryInternal : public NamedObjectContext<Strategy>
{
public:
    MageBuffStrategyFactoryInternal() : NamedObjectContext<Strategy>(false, true)
    {
        creators["bmana"] = &MageBuffStrategyFactoryInternal::bmana;
        creators["bdps"] = &MageBuffStrategyFactoryInternal::bdps;
    }

private:
    static Strategy* bmana(PlayerbotAI* botAI) { return new MageBuffManaStrategy(botAI); }
    static Strategy* bdps(PlayerbotAI* botAI) { return new MageBuffDpsStrategy(botAI); }
};

class MageTriggerFactoryInternal : public NamedObjectContext<Trigger>
{
public:
    MageTriggerFactoryInternal()
    {
        creators["fireball"] = &MageTriggerFactoryInternal::fireball;
        creators["pyroblast"] = &MageTriggerFactoryInternal::pyroblast;
        creators["combustion"] = &MageTriggerFactoryInternal::combustion;
        creators["cold snap"] = &MageTriggerFactoryInternal::cold_snap;
        creators["ice barrier"] = &MageTriggerFactoryInternal::ice_barrier;
        creators["arcane intellect"] = &MageTriggerFactoryInternal::arcane_intellect;
        creators["arcane intellect on party"] = &MageTriggerFactoryInternal::arcane_intellect_on_party;
        creators["mage armor"] = &MageTriggerFactoryInternal::mage_armor;
        creators["remove lesser curse"] = &MageTriggerFactoryInternal::remove_lesser_curse;
        creators["remove lesser curse on party"] = &MageTriggerFactoryInternal::remove_lesser_curse_on_party;
        creators["counterspell"] = &MageTriggerFactoryInternal::counterspell;
        creators["polymorph"] = &MageTriggerFactoryInternal::polymorph;
        creators["counterspell on enemy healer"] = &MageTriggerFactoryInternal::counterspell_enemy_healer;
        creators["arcane power"] = &MageTriggerFactoryInternal::arcane_power;
        creators["presence of mind"] = &MageTriggerFactoryInternal::presence_of_mind;
        creators["fire ward"] = &MageTriggerFactoryInternal::fire_ward;
        creators["frost ward"] = &MageTriggerFactoryInternal::frost_ward;
        creators["frost nova on target"] = &MageTriggerFactoryInternal::frost_nova_on_target;
        creators["frostbite on target"] = &MageTriggerFactoryInternal::frostbite_on_target;
        creators["firestarter"] = &MageTriggerFactoryInternal::firestarter;
        creators["improved scorch"] = &MageTriggerFactoryInternal::improved_scorch;
        creators["flamestrike nearby"] = &MageTriggerFactoryInternal::flamestrike_nearby;
        creators["flamestrike active and medium aoe"] = &MageTriggerFactoryInternal::flamestrike_blizzard;
        creators["blizzard channel check"] = &MageTriggerFactoryInternal::blizzard_channel_check;
        creators["blast wave off cd"] = &MageTriggerFactoryInternal::blast_wave_off_cd;
        creators["blast wave off cd and medium aoe"] = &MageTriggerFactoryInternal::blast_wave_off_cd_and_medium_aoe;
        creators["no firestarter strategy"] = &MageTriggerFactoryInternal::no_firestarter_strategy;
        creators["enemy is close and no firestarter strategy"] = &MageTriggerFactoryInternal::enemy_is_close_and_no_firestarter_strategy;
        creators["enemy too close for spell and no firestarter strategy"] = &MageTriggerFactoryInternal::enemy_too_close_for_spell_and_no_firestarter_strategy;
    }

private:
    static Trigger* presence_of_mind(PlayerbotAI* botAI) { return new PresenceOfMindTrigger(botAI); }
    static Trigger* frost_ward(PlayerbotAI* botAI) { return new FrostWardTrigger(botAI); }
    static Trigger* fire_ward(PlayerbotAI* botAI) { return new FireWardTrigger(botAI); }
    static Trigger* arcane_power(PlayerbotAI* botAI) { return new ArcanePowerTrigger(botAI); }
    static Trigger* fireball(PlayerbotAI* botAI) { return new FireballTrigger(botAI); }
    static Trigger* pyroblast(PlayerbotAI* botAI) { return new PyroblastTrigger(botAI); }
    static Trigger* combustion(PlayerbotAI* botAI) { return new CombustionTrigger(botAI); }
    // 1.60: cold snap only resets frost spells (no icy veins to reset) - trigger = own cooldown check.
    static Trigger* cold_snap(PlayerbotAI* botAI) { return new SpellCooldownTrigger(botAI, "cold snap"); }
    static Trigger* ice_barrier(PlayerbotAI* botAI) { return new IceBarrierTrigger(botAI); }
    static Trigger* arcane_intellect(PlayerbotAI* botAI) { return new ArcaneIntellectTrigger(botAI); }
    static Trigger* arcane_intellect_on_party(PlayerbotAI* botAI) { return new ArcaneIntellectOnPartyTrigger(botAI); }
    static Trigger* mage_armor(PlayerbotAI* botAI) { return new MageArmorTrigger(botAI); }
    static Trigger* remove_lesser_curse(PlayerbotAI* botAI) { return new RemoveLesserCurseTrigger(botAI); }
    static Trigger* remove_lesser_curse_on_party(PlayerbotAI* botAI) { return new PartyMemberRemoveLesserCurseTrigger(botAI); }
    static Trigger* counterspell(PlayerbotAI* botAI) { return new CounterspellInterruptSpellTrigger(botAI); }
    static Trigger* polymorph(PlayerbotAI* botAI) { return new PolymorphTrigger(botAI); }
    static Trigger* counterspell_enemy_healer(PlayerbotAI* botAI) { return new CounterspellEnemyHealerTrigger(botAI); }
    static Trigger* frost_nova_on_target(PlayerbotAI* botAI) { return new FrostNovaOnTargetTrigger(botAI); }
    static Trigger* frostbite_on_target(PlayerbotAI* botAI) { return new FrostbiteOnTargetTrigger(botAI); }
    static Trigger* improved_scorch(PlayerbotAI* botAI) { return new ImprovedScorchTrigger(botAI); }
    static Trigger* firestarter(PlayerbotAI* botAI) { return new FirestarterTrigger(botAI); }
    static Trigger* flamestrike_nearby(PlayerbotAI* botAI) { return new FlamestrikeNearbyTrigger(botAI); }
    static Trigger* flamestrike_blizzard(PlayerbotAI* botAI) { return new FlamestrikeBlizzardTrigger(botAI); }
    static Trigger* blizzard_channel_check(PlayerbotAI* botAI) { return new BlizzardChannelCheckTrigger(botAI); }
    static Trigger* blast_wave_off_cd(PlayerbotAI* botAI) { return new BlastWaveOffCdTrigger(botAI); }
    static Trigger* blast_wave_off_cd_and_medium_aoe(PlayerbotAI* botAI) { return new BlastWaveOffCdTriggerAndMediumAoeTrigger(botAI); }
    static Trigger* no_firestarter_strategy(PlayerbotAI* botAI) { return new NoFirestarterStrategyTrigger(botAI); }
    static Trigger* enemy_is_close_and_no_firestarter_strategy(PlayerbotAI* botAI) { return new EnemyIsCloseAndNoFirestarterStrategyTrigger(botAI); }
    static Trigger* enemy_too_close_for_spell_and_no_firestarter_strategy(PlayerbotAI* botAI) { return new EnemyTooCloseForSpellAndNoFirestarterStrategyTrigger(botAI); }
};

class MageAiObjectContextInternal : public NamedObjectContext<Action>
{
public:
    MageAiObjectContextInternal()
    {
        creators["arcane power"] = &MageAiObjectContextInternal::arcane_power;
        creators["presence of mind"] = &MageAiObjectContextInternal::presence_of_mind;
        creators["frostbolt"] = &MageAiObjectContextInternal::frostbolt;
        creators["ice lance"] = &MageAiObjectContextInternal::ice_lance;
        creators["blizzard"] = &MageAiObjectContextInternal::blizzard;
        creators["cone of cold"] = &MageAiObjectContextInternal::cone_of_cold;
        creators["frost nova"] = &MageAiObjectContextInternal::frost_nova;
        creators["arcane intellect"] = &MageAiObjectContextInternal::arcane_intellect;
        creators["arcane intellect on party"] = &MageAiObjectContextInternal::arcane_intellect_on_party;
        creators["conjure water"] = &MageAiObjectContextInternal::conjure_water;
        creators["conjure food"] = &MageAiObjectContextInternal::conjure_food;
        creators["mage armor"] = &MageAiObjectContextInternal::mage_armor;
        creators["ice armor"] = &MageAiObjectContextInternal::ice_armor;
        creators["frost armor"] = &MageAiObjectContextInternal::frost_armor;
        creators["fireball"] = &MageAiObjectContextInternal::fireball;
        creators["pyroblast"] = &MageAiObjectContextInternal::pyroblast;
        creators["flamestrike"] = &MageAiObjectContextInternal::flamestrike;
        creators["fire blast"] = &MageAiObjectContextInternal::fire_blast;
        creators["scorch"] = &MageAiObjectContextInternal::scorch;
        creators["counterspell"] = &MageAiObjectContextInternal::counterspell;
        creators["remove lesser curse"] = &MageAiObjectContextInternal::remove_lesser_curse;
        creators["remove lesser curse on party"] = &MageAiObjectContextInternal::remove_lesser_curse_on_party;
        creators["cold snap"] = &MageAiObjectContextInternal::cold_snap;
        creators["ice barrier"] = &MageAiObjectContextInternal::ice_barrier;
        creators["combustion"] = &MageAiObjectContextInternal::combustion;
        creators["ice block"] = &MageAiObjectContextInternal::ice_block;
        creators["polymorph"] = &MageAiObjectContextInternal::polymorph;
        creators["blast wave"] = &MageAiObjectContextInternal::blast_wave;
        creators["evocation"] = &MageAiObjectContextInternal::evocation;
        creators["arcane missiles"] = &MageAiObjectContextInternal::arcane_missiles;
        creators["counterspell on enemy healer"] = &MageAiObjectContextInternal::counterspell_on_enemy_healer;
        creators["fire ward"] = &MageAiObjectContextInternal::fire_ward;
        creators["frost ward"] = &MageAiObjectContextInternal::frost_ward;
        creators["blink back"] = &MageAiObjectContextInternal::blink_back;
        creators["use mana sapphire"] = &MageAiObjectContextInternal::use_mana_sapphire;
        creators["use mana emerald"] = &MageAiObjectContextInternal::use_mana_emerald;
        creators["use mana ruby"] = &MageAiObjectContextInternal::use_mana_ruby;
        creators["use mana citrine"] = &MageAiObjectContextInternal::use_mana_citrine;
        creators["use mana jade"] = &MageAiObjectContextInternal::use_mana_jade;
        creators["use mana agate"] = &MageAiObjectContextInternal::use_mana_agate;
        creators["mana shield"] = &MageAiObjectContextInternal::mana_shield;
    }

private:
    static Action* presence_of_mind(PlayerbotAI* botAI) { return new CastPresenceOfMindAction(botAI); }
    static Action* frost_ward(PlayerbotAI* botAI) { return new CastFrostWardAction(botAI); }
    static Action* fire_ward(PlayerbotAI* botAI) { return new CastFireWardAction(botAI); }
    static Action* arcane_power(PlayerbotAI* botAI) { return new CastArcanePowerAction(botAI); }
    static Action* arcane_missiles(PlayerbotAI* botAI) { return new CastArcaneMissilesAction(botAI); }
    static Action* frostbolt(PlayerbotAI* botAI) { return new CastFrostboltAction(botAI); }
    static Action* ice_lance(PlayerbotAI* botAI) { return new CastIceLanceAction(botAI); }
    static Action* blizzard(PlayerbotAI* botAI) { return new CastBlizzardAction(botAI); }
    static Action* cone_of_cold(PlayerbotAI* botAI) { return new CastConeOfColdAction(botAI); }
    static Action* frost_nova(PlayerbotAI* botAI) { return new CastFrostNovaAction(botAI); }
    static Action* arcane_intellect(PlayerbotAI* botAI) { return new CastArcaneIntellectAction(botAI); }
    static Action* arcane_intellect_on_party(PlayerbotAI* botAI) { return new CastArcaneIntellectOnPartyAction(botAI); }
    static Action* conjure_water(PlayerbotAI* botAI) { return new CastConjureWaterAction(botAI); }
    static Action* conjure_food(PlayerbotAI* botAI) { return new CastConjureFoodAction(botAI); }
    static Action* mage_armor(PlayerbotAI* botAI) { return new CastMageArmorAction(botAI); }
    static Action* ice_armor(PlayerbotAI* botAI) { return new CastIceArmorAction(botAI); }
    static Action* frost_armor(PlayerbotAI* botAI) { return new CastFrostArmorAction(botAI); }
    static Action* fireball(PlayerbotAI* botAI) { return new CastFireballAction(botAI); }
    static Action* pyroblast(PlayerbotAI* botAI) { return new CastPyroblastAction(botAI); }
    static Action* flamestrike(PlayerbotAI* botAI) { return new CastFlamestrikeAction(botAI); }
    static Action* fire_blast(PlayerbotAI* botAI) { return new CastFireBlastAction(botAI); }
    static Action* scorch(PlayerbotAI* botAI) { return new CastScorchAction(botAI); }
    static Action* counterspell(PlayerbotAI* botAI) { return new CastCounterspellAction(botAI); }
    static Action* remove_lesser_curse(PlayerbotAI* botAI) { return new CastRemoveLesserCurseAction(botAI); }
    static Action* remove_lesser_curse_on_party(PlayerbotAI* botAI) { return new CastRemoveLesserCurseOnPartyAction(botAI); }
    static Action* cold_snap(PlayerbotAI* botAI) { return new CastColdSnapAction(botAI); }
    static Action* ice_barrier(PlayerbotAI* botAI) { return new CastIceBarrierAction(botAI); }
    static Action* combustion(PlayerbotAI* botAI) { return new CastCombustionAction(botAI); }
    static Action* ice_block(PlayerbotAI* botAI) { return new CastIceBlockAction(botAI); }
    static Action* polymorph(PlayerbotAI* botAI) { return new CastPolymorphAction(botAI); }
    static Action* blast_wave(PlayerbotAI* botAI) { return new CastBlastWaveAction(botAI); }
    static Action* evocation(PlayerbotAI* botAI) { return new CastEvocationAction(botAI); }
    static Action* counterspell_on_enemy_healer(PlayerbotAI* botAI) { return new CastCounterspellOnEnemyHealerAction(botAI); }
    static Action* blink_back(PlayerbotAI* botAI) { return new CastBlinkBackAction(botAI); }
    static Action* use_mana_sapphire(PlayerbotAI* botAI) { return new UseManaSapphireAction(botAI); }
    static Action* use_mana_emerald(PlayerbotAI* botAI) { return new UseManaEmeraldAction(botAI); }
    static Action* use_mana_ruby(PlayerbotAI* botAI) { return new UseManaRubyAction(botAI); }
    static Action* use_mana_citrine(PlayerbotAI* botAI) { return new UseManaCitrineAction(botAI); }
    static Action* use_mana_jade(PlayerbotAI* botAI) { return new UseManaJadeAction(botAI); }
    static Action* use_mana_agate(PlayerbotAI* botAI) { return new UseManaAgateAction(botAI); }
    static Action* mana_shield(PlayerbotAI* botAI) { return new CastManaShieldAction(botAI); }
};

SharedNamedObjectContextList<Strategy> MageAiObjectContext::sharedStrategyContexts;
SharedNamedObjectContextList<Action> MageAiObjectContext::sharedActionContexts;
SharedNamedObjectContextList<Trigger> MageAiObjectContext::sharedTriggerContexts;
SharedNamedObjectContextList<UntypedValue> MageAiObjectContext::sharedValueContexts;

MageAiObjectContext::MageAiObjectContext(PlayerbotAI* botAI)
    : AiObjectContext(botAI, sharedStrategyContexts, sharedActionContexts, sharedTriggerContexts, sharedValueContexts)
{
}

void MageAiObjectContext::BuildSharedContexts()
{
    BuildSharedStrategyContexts(sharedStrategyContexts);
    BuildSharedActionContexts(sharedActionContexts);
    BuildSharedTriggerContexts(sharedTriggerContexts);
    BuildSharedValueContexts(sharedValueContexts);
}

void MageAiObjectContext::BuildSharedStrategyContexts(SharedNamedObjectContextList<Strategy>& strategyContexts)
{
    AiObjectContext::BuildSharedStrategyContexts(strategyContexts);
    strategyContexts.Add(new MageStrategyFactoryInternal());
    strategyContexts.Add(new MageCombatStrategyFactoryInternal());
    strategyContexts.Add(new MageBuffStrategyFactoryInternal());
}

void MageAiObjectContext::BuildSharedActionContexts(SharedNamedObjectContextList<Action>& actionContexts)
{
    AiObjectContext::BuildSharedActionContexts(actionContexts);
    actionContexts.Add(new MageAiObjectContextInternal());
}

void MageAiObjectContext::BuildSharedTriggerContexts(SharedNamedObjectContextList<Trigger>& triggerContexts)
{
    AiObjectContext::BuildSharedTriggerContexts(triggerContexts);
    triggerContexts.Add(new MageTriggerFactoryInternal());
}

void MageAiObjectContext::BuildSharedValueContexts(SharedNamedObjectContextList<UntypedValue>& valueContexts)
{
    AiObjectContext::BuildSharedValueContexts(valueContexts);
}
